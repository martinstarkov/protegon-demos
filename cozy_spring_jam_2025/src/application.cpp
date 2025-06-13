#include <functional>

#include "audio/audio.h"
#include "components/common.h"
#include "components/draw.h"
#include "components/generic.h"
#include "components/input.h"
#include "components/movement.h"
#include "components/transform.h"
#include "core/entity.h"
#include "core/game.h"
#include "core/manager.h"
#include "core/resource_manager.h"
#include "core/time.h"
#include "math/noise.h"
#include "math/vector2.h"
#include "physics/collision/collider.h"
#include "physics/rigid_body.h"
#include "player/player_controller.h"
#include "protegon/protegon.h"
#include "rendering/api/color.h"
#include "rendering/api/origin.h"
#include "scene/camera.h"
#include "scene/scene.h"
#include "scene/scene_manager.h"
#include "tile/grid.h"
#include "tweening/tween.h"
#include "ui/button.h"

using namespace ptgn;

// Axis-aligned bounding box
struct AABB {
	V2_float center;
	float halfWidth;
	float halfHeight;

	AABB(V2_float center, float hw, float hh) : center(center), halfWidth(hw), halfHeight(hh) {}

	bool contains(const V2_float& point) const {
		return (
			point.x >= center.x - halfWidth && point.x <= center.x + halfWidth &&
			point.y >= center.y - halfHeight && point.y <= center.y + halfHeight
		);
	}

	bool intersects(const AABB& other) const {
		return !(
			other.center.x - other.halfWidth > center.x + halfWidth ||
			other.center.x + other.halfWidth < center.x - halfWidth ||
			other.center.y - other.halfHeight > center.y + halfHeight ||
			other.center.y + other.halfHeight < center.y - halfHeight
		);
	}
};

template <typename Entity>
class Quadtree {
public:
	static constexpr int CAPACITY = 8;
	AABB boundary;
	std::vector<Entity> entities;
	bool divided = false;

	std::unique_ptr<Quadtree> northeast;
	std::unique_ptr<Quadtree> northwest;
	std::unique_ptr<Quadtree> southeast;
	std::unique_ptr<Quadtree> southwest;

	Quadtree(const AABB& boundary) : boundary(boundary) {}

	bool insert(const Entity& entity) {
		V2_float pos = entity.GetPosition();
		if (!boundary.contains(pos)) {
			return false;
		}

		if (entities.size() < CAPACITY) {
			entities.push_back(entity);
			return true;
		}

		if (!divided) {
			subdivide();
		}

		return (
			northeast->insert(entity) || northwest->insert(entity) || southeast->insert(entity) ||
			southwest->insert(entity)
		);
	}

	std::vector<Entity> query(const Entity& entity, float radius = 50.0f) const {
		std::vector<Entity> found;
		V2_float pos = entity.GetPosition();
		AABB range(pos, radius, radius);
		queryRange(range, found);
		return found;
	}

	void clear() {
		entities.clear();
		if (divided) {
			northeast->clear();
			northwest->clear();
			southeast->clear();
			southwest->clear();
			northeast.reset();
			northwest.reset();
			southeast.reset();
			southwest.reset();
			divided = false;
		}
	}

private:
	void subdivide() {
		float x	 = boundary.center.x;
		float y	 = boundary.center.y;
		float hw = boundary.halfWidth / 2;
		float hh = boundary.halfHeight / 2;

		northeast = std::make_unique<Quadtree>(AABB({ x + hw, y - hh }, hw, hh));
		northwest = std::make_unique<Quadtree>(AABB({ x - hw, y - hh }, hw, hh));
		southeast = std::make_unique<Quadtree>(AABB({ x + hw, y + hh }, hw, hh));
		southwest = std::make_unique<Quadtree>(AABB({ x - hw, y + hh }, hw, hh));
		divided	  = true;
	}

	void queryRange(const AABB& range, std::vector<Entity>& found) const {
		if (!boundary.intersects(range)) {
			return;
		}

		for (const auto& entity : entities) {
			if (range.contains(entity.GetPosition())) {
				found.push_back(entity);
			}
		}

		if (divided) {
			northeast->queryRange(range, found);
			northwest->queryRange(range, found);
			southeast->queryRange(range, found);
			southwest->queryRange(range, found);
		}
	}
};

constexpr V2_int window_size{ 1280, 720 };
constexpr float camera_zoom{ 4.0f };
constexpr Color bg_color{ 126, 206, 120, 255 };

constexpr V2_int grid_size{ 30, 30 };
constexpr V2_int tile_size{ 15, 15 };
constexpr V2_int world_size{ grid_size * tile_size };

class Inventory {
public:
	struct OnPickup : public Script<OnPickup> {
		OnPickup() = default;

		OnPickup(Entity o) : other{ o } {}

		Entity other;

		void OnCollisionStart(Collision collision) override {
			if (collision.entity == other) {
				other.SetTint(color::Red);
			}
		}

		void OnCollisionStop(Collision collision) override {
			if (collision.entity == other) {
				other.SetTint(color::White);
			}
		}
	};

	static void MakePickupable(Entity player, Entity entity) {
		entity.Add<BoxCollider>(Sprite{ entity }.GetDisplaySize()).SetOverlapOnly();
		entity.Enable();
		// player.GetChild("interaction").AddScript<OnPickup>(entity);
	}
};

class GameScene : public Scene {
public:
	GameScene() {
		LoadResources("resources/data/resources.json");
	}

	FractalNoise fractal_noise;

	Grid<Entity> flowers{ grid_size };

	Entity flower;
	Entity player;

	Entity CreateFlower(const V2_int& tile, const V2_int& position, const TextureHandle& key) {
		Sprite s{ CreateSprite(*this, key) };
		s.SetPosition(position);
		Inventory::MakePickupable(player, s);
		return s;
	}

	void Enter() final {
		camera.window.SetZoom(camera_zoom);
		camera.window.SetPosition(window_size / 2.0f / camera_zoom);

		camera.primary.SetZoom(camera_zoom);
		camera.primary.SetBounds({ 0, 0 }, world_size);
		physics.SetBounds({ 0, 0 }, world_size);
		// SetColliderVisibility(true);

		fractal_noise.SetOctaves(2);
		fractal_noise.SetFrequency(0.055f);
		fractal_noise.SetLacunarity(5);
		fractal_noise.SetPersistence(3);

		RNG<int> flower_type_rng{ 0, 9 };
		RNG<int> flower_rng{ 0, 1 };

		player = CreateTopDownPlayer(*this, world_size / 2.0f);

		flowers.ForEachCoordinate([&](auto coordinate) {
			if (flower_rng()) {
				flowers.Set(
					coordinate, CreateFlower(
									coordinate, coordinate * tile_size + tile_size / 2,
									"flower_" + std::to_string(flower_type_rng())
								)
				);
			}
		});

		camera.primary.StartFollow(player);
	}
};

/*
class MainMenu : public Scene {
public:
	Button play;

	MainMenu() {}

	void Enter() override {}

	void Update() override {}
};
*/

int main([[maybe_unused]] int c, [[maybe_unused]] char** v) {
	game.Init("Test Jam", window_size, bg_color);
	game.scene.Enter<GameScene>("game");
	return 0;
}