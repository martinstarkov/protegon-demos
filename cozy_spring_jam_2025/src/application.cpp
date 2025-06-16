#include "components/draw.h"
#include "core/game.h"
#include "protegon/protegon.h"
#include "rendering/resources/texture.h"
#include "scene/scene.h"
#include "scene/scene_manager.h"

using namespace ptgn;

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
	game.Init("GameScene", window_size);
	game.scene.Enter<GameScene>();
	return 0;
}