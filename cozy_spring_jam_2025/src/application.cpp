#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <memory>
#include <random>
#include <set>
#include <vector>

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
#include "math/rng.h"
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

constexpr V2_int window_size{ 500, 500 }; //{ 1280, 720 };

struct Box;

struct EndPoint {
	float value{ 0.0f };
	bool isMin{ false };
	Box* box{ nullptr };
	EndPoint(float value_, bool isMin_);
};

struct AABB;

struct Box {
	std::array<EndPoint*, 2> minEndPoints;
	std::array<EndPoint*, 2> maxEndPoints;
	AABB* userData{ nullptr };

	Box(EndPoint* minX, EndPoint* minY, EndPoint* maxX, EndPoint* maxY);

	bool overlaps(Box* box) const {
		float l1X = minEndPoints[0]->value;
		float u1X = maxEndPoints[0]->value;
		float l1Y = minEndPoints[1]->value;
		float u1Y = maxEndPoints[1]->value;
		float l2X = box->minEndPoints[0]->value;
		float u2X = box->maxEndPoints[0]->value;
		float l2Y = box->minEndPoints[1]->value;
		float u2Y = box->maxEndPoints[1]->value;
		return !(l2X > u1X || u2X < l1X || u2Y < l1Y || l2Y > u1Y);
	}
};

EndPoint::EndPoint(float value_, bool isMin_) {
	box	  = nullptr;
	value = value_;
	isMin = isMin_;
}

Box::Box(EndPoint* minX, EndPoint* minY, EndPoint* maxX, EndPoint* maxY) {
	minEndPoints = { minX, minY };
	maxEndPoints = { maxX, maxY };
	userData	 = nullptr;
}

struct AABB {
	V2_float min;
	V2_float max;
	V2_float velocity;
	std::int64_t index = -1;
	Box* sapBox{ nullptr };

	AABB(V2_float min_, V2_float max_) : min(min_), max(max_), velocity(randomVelocity()) {}

private:
	static V2_float randomVelocity() {
		V2_float dir{ V2_float::Random(-0.5f, 0.5f) };
		float speed = 60.0f;

		if (dir.x != 0 || dir.y != 0) {
			return dir.Normalized() * speed;
		} else {
			return V2_float{ speed, 0.0f };
		}
	}
};

class SweepAndPrune {
public:
	std::vector<std::unique_ptr<Box>> boxes;
	std::array<std::vector<EndPoint*>, 2> endPoints;

	std::function<void(Box*, Box*)> onAdd;
	std::function<void(Box*, Box*)> onRemove;

	SweepAndPrune() {
		boxes.clear();
		endPoints = {};
		onAdd	  = [](Box*, Box*) {
		};
		onRemove = [](Box*, Box*) {
		};
	};

	Box* addObject(const V2_float& v0, const V2_float& v1, AABB* userData) {
		auto minX = new EndPoint(v0.x, true);
		auto maxX = new EndPoint(v1.x, false);
		auto minY = new EndPoint(v0.y, true);
		auto maxY = new EndPoint(v1.y, false);

		auto box	  = std::make_unique<Box>(minX, minY, maxX, maxY);
		box->userData = userData;
		minX->box = maxX->box = minY->box = maxY->box = box.get();

		Box* boxPtr = box.get();
		boxes.push_back(std::move(box));

		insertSorted(endPoints[0], minX, true);
		insertSorted(endPoints[0], maxX, false);
		endPoints[1].push_back(minY);
		endPoints[1].push_back(maxY);

		for (int axis = 0; axis < 2; ++axis) {
			sortFull(endPoints[axis]);
		}

		return boxPtr;
	}

	void updateObject(Box* box, V2_float v0, V2_float v1) {
		float newPos[2][2] = { { v0.x, v1.x }, { v0.y, v1.y } };
		for (int axis = 0; axis < 2; ++axis) {
			auto& axisVec = endPoints[axis];

			box->minEndPoints[axis]->value = newPos[axis][0];
			sortMinDown(axisVec, indexOf(axisVec, box->minEndPoints[axis]));

			box->maxEndPoints[axis]->value = newPos[axis][1];
			sortMaxUp(axisVec, indexOf(axisVec, box->maxEndPoints[axis]));

			sortMinUp(axisVec, indexOf(axisVec, box->minEndPoints[axis]));
			sortMaxDown(axisVec, indexOf(axisVec, box->maxEndPoints[axis]));
		}
	}

	void removeObject(Box* box) {
		box->minEndPoints[1]->value = std::numeric_limits<float>::max() - 1;
		box->maxEndPoints[1]->value = std::numeric_limits<float>::max();
		sortFull(endPoints[1]);

		boxes.erase(
			std::remove_if(
				boxes.begin(), boxes.end(),
				[box](const std::unique_ptr<Box>& b) { return b.get() == box; }
			),
			boxes.end()
		);

		for (int axis = 0; axis < 2; ++axis) {
			auto& axisVec = endPoints[axis];
			remove(axisVec, box->minEndPoints[axis]);
			remove(axisVec, box->maxEndPoints[axis]);
		}
	}

private:
	void insertSorted(std::vector<EndPoint*>& vec, EndPoint* ep, bool isMin) {
		auto it = vec.begin();
		while (it != vec.end() && (*it)->value < ep->value) {
			++it;
		}
		vec.insert(it, ep);
	}

	int indexOf(const std::vector<EndPoint*>& vec, EndPoint* value) {
		auto it = std::find(vec.begin(), vec.end(), value);
		return (it != vec.end()) ? (int)std::distance(vec.begin(), it) : -1;
	}

	void remove(std::vector<EndPoint*>& vec, EndPoint* value) {
		vec.erase(std::remove(vec.begin(), vec.end(), value), vec.end());
	}

	void sortFull(std::vector<EndPoint*>& axis) {
		for (auto j = 1; j < axis.size(); ++j) {
			EndPoint* keyElement = axis[j];
			float key			 = keyElement->value;
			int i				 = j - 1;
			while (i >= 0 && axis[i]->value > key) {
				EndPoint* swapper = axis[i];
				if (keyElement->isMin && !swapper->isMin &&
					swapper->box->overlaps(keyElement->box)) {
					onAdd(swapper->box, keyElement->box);
				} else if (!keyElement->isMin && swapper->isMin) {
					onRemove(swapper->box, keyElement->box);
				}
				axis[i + 1] = swapper;
				--i;
			}
			axis[i + 1] = keyElement;
		}
	}

	void sortMinDown(std::vector<EndPoint*>& axis, int j) {
		auto keyElement = axis[j];
		float key		= keyElement->value;
		int i			= j - 1;
		while (i >= 0 && axis[i]->value > key) {
			auto swapper = axis[i];
			if (keyElement->isMin && !swapper->isMin && swapper->box->overlaps(keyElement->box)) {
				onAdd(swapper->box, keyElement->box);
			}
			axis[i + 1] = swapper;
			--i;
		}
		axis[i + 1] = keyElement;
	}

	void sortMinUp(std::vector<EndPoint*>& axis, int j) {
		auto keyElement = axis[j];
		float key		= keyElement->value;
		int i			= j + 1;
		while (i < static_cast<int>(axis.size()) && axis[i]->value < key) {
			auto swapper = axis[i];
			if (keyElement->isMin && !swapper->isMin) {
				onRemove(swapper->box, keyElement->box);
			}
			axis[i - 1] = swapper;
			++i;
		}
		axis[i - 1] = keyElement;
	}

	void sortMaxDown(std::vector<EndPoint*>& axis, int j) {
		auto keyElement = axis[j];
		float key		= keyElement->value;
		int i			= j - 1;
		while (i >= 0 && axis[i]->value > key) {
			auto swapper = axis[i];
			if (!keyElement->isMin && swapper->isMin) {
				onRemove(swapper->box, keyElement->box);
			}
			axis[i + 1] = swapper;
			--i;
		}
		axis[i + 1] = keyElement;
	}

	void sortMaxUp(std::vector<EndPoint*>& axis, int j) {
		auto keyElement = axis[j];
		float key		= keyElement->value;
		int i			= j + 1;
		while (i < static_cast<int>(axis.size()) && axis[i]->value < key) {
			auto swapper = axis[i];
			if (!keyElement->isMin && swapper->isMin && swapper->box->overlaps(keyElement->box)) {
				onAdd(swapper->box, keyElement->box);
			}
			axis[i - 1] = swapper;
			++i;
		}
		axis[i - 1] = keyElement;
	}
};

void moveAABBs(
	std::vector<std::unique_ptr<AABB>>& aabbs, float deltaTimeSeconds, float canvasWidth,
	float movingPercent
) {
	auto movingCount = static_cast<int>(aabbs.size() * movingPercent / 100.0f);
	float boundary	 = canvasWidth;

	for (auto i = 0; i < movingCount; ++i) {
		auto aabb	   = aabbs[i].get();
		V2_float delta = aabb->velocity * deltaTimeSeconds;

		aabb->min = aabb->min + delta;
		aabb->max = aabb->max + delta;

		for (int dim = 0; dim < 2; ++dim) {
			float minVal = (dim == 0 ? aabb->min.x : aabb->min.y);
			float maxVal = (dim == 0 ? aabb->max.x : aabb->max.y);

			if (minVal < 0.0f || maxVal > boundary) {
				if (dim == 0) {
					aabb->velocity.x *= -1.0f;
				} else {
					aabb->velocity.y *= -1.0f;
				}
			}
		}
	}
}

void updateSAP(std::vector<std::unique_ptr<AABB>>& aabbs, SweepAndPrune& sap, float movingPercent) {
	auto movingCount = static_cast<int>(aabbs.size() * movingPercent / 100.0f);

	for (auto i = 0; i < movingCount; ++i) {
		auto aabb = aabbs[i].get();
		sap.updateObject(aabb->sapBox, aabb->min, aabb->max);
	}
}

void addAABB(
	std::vector<std::unique_ptr<AABB>>& aabbs, SweepAndPrune& sap, float size, float canvasWidth
) {
	auto getRandomPosition = [size, canvasWidth]() -> float {
		static std::random_device rd;
		static std::mt19937 gen(rd());
		std::uniform_real_distribution<float> dist(size, canvasWidth - size);
		return dist(gen);
	};

	float x0 = getRandomPosition() - size;
	float y0 = getRandomPosition() - size;
	float x1 = x0 + size;
	float y1 = y0 + size;

	auto aabb	= std::make_unique<AABB>(V2_float{ x0, y0 }, V2_float{ x1, y1 });
	aabb->index = static_cast<int>(aabbs.size());

	// Register with SAP
	aabb->sapBox = sap.addObject(aabb->min, aabb->max, aabb.get());

	aabbs.push_back(std::move(aabb));
}

void removeAABB(std::vector<std::unique_ptr<AABB>>& aabbs, SweepAndPrune& sap) {
	auto& aabb = aabbs[aabbs.size() - 1];
	sap.removeObject(aabb->sapBox);
	aabbs.pop_back();
}

class SceneTest : public Scene {
public:
	float movingPercent = 50.0f;

	float size = 20.0f;

	std::unordered_set<std::int64_t> pairs;

	std::vector<std::unique_ptr<AABB>> aabbs;

	SweepAndPrune sap;

	void Enter() {
		sap.onAdd = [&](Box* boxA, Box* boxB) {
			auto i = boxA->userData->index;
			auto j = boxB->userData->index;
			PTGN_ASSERT(i != j);
			if (i > j) {
				auto tmp = j;
				j		 = i;
				i		 = tmp;
			}
			pairs.insert((i << 16) | j);
		};
		sap.onRemove = [&](Box* boxA, Box* boxB) {
			auto i = boxA->userData->index;
			auto j = boxB->userData->index;
			if (i > j) {
				auto tmp = j;
				j		 = i;
				i		 = tmp;
			}
			pairs.erase((i << 16) | j);
		};
		for (auto i = 0; i < 128; i++) {
			addAABB(aabbs, sap, size, (float)window_size.x);
		}
	}

	void Update() {
		moveAABBs(aabbs, game.dt(), (float)window_size.x, movingPercent);
		updateSAP(aabbs, sap, movingPercent);

		for (auto& aabb : aabbs) {
			DrawDebugRect(aabb->min, aabb->max - aabb->min, color::Green, Origin::TopLeft, 1.0f);
		}
		for (auto& key : pairs) {
			auto i		= (key >> 16) & 0xffff;
			auto j		= key & 0xffff;
			auto& aabbi = aabbs[i];
			auto& aabbj = aabbs[j];
			DrawDebugLine(
				(aabbi->min + aabbi->max) / 2.0f, (aabbj->min + aabbj->max) / 2.0f, color::DarkRed,
				1.0f
			);
		}
	}
};

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
	game.Init("Test Jam", window_size);
	game.scene.Enter<SceneTest>("game");
	return 0;
}