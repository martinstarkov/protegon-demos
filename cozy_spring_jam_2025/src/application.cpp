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
#include "core/game_object.h"
#include "core/manager.h"
#include "core/resource_manager.h"
#include "core/time.h"
#include "core/tween.h"
#include "math/noise.h"
#include "math/vector2.h"
#include "physics/collision/collider.h"
#include "physics/rigid_body.h"
#include "protegon/protegon.h"
#include "rendering/api/color.h"
#include "rendering/api/origin.h"
#include "scene/camera.h"
#include "scene/scene.h"
#include "scene/scene_manager.h"
#include "ui/button.h"

using namespace ptgn;

constexpr V2_int window_size{ 1280, 720 };
constexpr float camera_zoom{ 4.0f };
constexpr Color bg_color{ 126, 206, 120, 255 };

constexpr int walk_volume{ 30 };
constexpr int repair_volume{ 30 };
constexpr int music_volume{ 50 };
constexpr int wind_volume{ 5 };
constexpr int walk_sound_frequency{ 2 }; // every second repeat of the walk animation

constexpr CollisionCategory player_category{ 0 };
constexpr CollisionCategory interaction_category{ 1 };
constexpr CollisionCategory flower_category{ 2 };

struct WalkRepeats : public ArithmeticComponent<int> {
	using ArithmeticComponent::ArithmeticComponent;
};

struct Inventory : public GameObject {
	Inventory() = default;

	Sprite inventory;
	Sprite selector;

	int selected_slot{ 0 };

	V2_float selector_position{ -59.5f, -10.5f }; // Relative to inventory position.

	V2_float selector_offset{ 17, 0 };			  // Relative to previous selector.

	std::vector<Entity> slots;

	Inventory(
		Manager& manager, const V2_float& position, Origin origin, std::size_t slot_count,
		int selected_slot = 0
	) :
		GameObject{ manager } {
		inventory = Sprite{ manager, "inventory" };
		inventory.SetParent(*this);
		selector = Sprite{ manager, "selector" };
		selector.SetParent(*this);
		selector.SetDepth(1);
		selector.SetOrigin(Origin::Center);

		slots.resize(slot_count, Entity{});
		PTGN_ASSERT(selected_slot >= 0);
		this->selected_slot = selected_slot;

		SetPosition(position);
		SetOrigin(origin);

		UpdateSelectorPosition();
	}

	void UpdateSelectorPosition() {
		selector.SetPosition(GetSlotPosition(selected_slot));
	}

	[[nodiscard]] V2_float GetSlotPosition(int slot) const {
		PTGN_ASSERT(slot >= 0 && static_cast<std::size_t>(slot) < slots.size());
		return selector_position + slot * selector_offset;
	}

	void IncrementSlot(int amount) {
		PTGN_ASSERT(amount != 0);
		selected_slot -= amount;
		selected_slot  = Mod(selected_slot, static_cast<int>(slots.size()));
		PTGN_ASSERT(selected_slot >= 0);
		UpdateSelectorPosition();
	}

	[[nodiscard]] bool IsSlotTaken(int slot) const {
		PTGN_ASSERT(slot >= 0 && static_cast<std::size_t>(slot) < slots.size());
		return slots[static_cast<std::size_t>(slot)] != Entity{};
	}

	void SetSlot(int slot, Entity entity) {
		PTGN_ASSERT(slot >= 0 && static_cast<std::size_t>(slot) < slots.size());
		PTGN_ASSERT(!IsSlotTaken(slot));
		slots[static_cast<std::size_t>(slot)] = entity;
	}

	void UnsetSlot(int slot) {
		PTGN_ASSERT(slot >= 0 && static_cast<std::size_t>(slot) < slots.size());
		slots[static_cast<std::size_t>(slot)] = Entity{};
	}
};

struct Player : public GameObject {
	Player() = default;

	Player(Manager& manager) : GameObject{ manager } {
		V2_float player_starting_position{ 0.0f, 0.0f };

		Add<Transform>(player_starting_position);
		auto& rb = Add<RigidBody>();
		Add<Enabled>();

		V2_float hitbox_size{ 10, 6 };
		V2_float hitbox_offset{ 0, 8 };

		auto body_hitbox = manager.CreateEntity();
		body_hitbox.Add<BoxCollider>(hitbox_size, Origin::CenterBottom);
		body_hitbox.Add<Transform>(hitbox_offset);

		auto interaction_hitbox = manager.CreateEntity();
		auto& interaction_collider =
			interaction_hitbox.Add<BoxCollider>(V2_float{ 28, 28 }, Origin::Center);
		interaction_collider.overlap_only = true;
		interaction_hitbox.Add<Transform>(V2_float{});
		interaction_hitbox.Enable();

		AddChild("body", body_hitbox);
		AddChild("interaction", interaction_hitbox);

		auto& movement = Add<TopDownMovement>();

		// Maximum movement speed.
		movement.max_speed = 0.7f * 60.0f;
		// How fast to reach max speed.
		movement.max_acceleration = 20.0f * 60.0f;
		// How fast to stop after letting go.
		movement.max_deceleration = 20.0f * 60.0f;
		// How fast to stop when changing direction.
		movement.max_turn_speed = 60.0f * 60.0f;

		movement.friction = 1.0f;

		V2_uint animation_count{ 4, 3 };
		V2_int player_size		= { 16, 17 };
		V2_float animation_size = player_size;
		milliseconds animation_duration{ 1000 };

		Add<WalkRepeats>();

		auto& anim_map = Add<AnimationMap>(
			"down",
			Animation(manager, "player_anim", animation_count.x, animation_size, animation_duration)
		);
		auto& a0 = anim_map.GetActive();
		auto& a1 = anim_map.Load(
			"right", Animation(
						 manager, "player_anim", animation_count.x, animation_size,
						 animation_duration, V2_float{ 0, animation_size.y }
					 )
		);
		auto& a2 = anim_map.Load(
			"up", Animation(
					  manager, "player_anim", animation_count.x, animation_size, animation_duration,
					  V2_float{ 0, 2.0f * animation_size.y }
				  )
		);

		auto on_repeat = [entity = GetEntity()]() {
			auto& repeats{ entity.Get<WalkRepeats>() };
			++repeats.GetValue();
			bool repeat{ repeats % walk_sound_frequency == 0 };
			if (!repeat) {
				return;
			}
			game.sound.Play("walk");
		};

		a0.SetParent(*this);
		a1.SetParent(*this);
		a2.SetParent(*this);

		a0.Add<callback::AnimationRepeat>(on_repeat);
		a1.Add<callback::AnimationRepeat>(on_repeat);
		a2.Add<callback::AnimationRepeat>(on_repeat);

		movement.on_move_start = [entity = GetEntity()]() {
			entity.Get<AnimationMap>().GetActive().Get<Tween>().Start(false);
		};
		movement.on_direction_change = [entity = GetEntity()](MoveDirection) {
			auto& a{ entity.Get<AnimationMap>() };
			auto dir{ entity.Get<TopDownMovement>().GetDirection() };
			auto& prev_active{ a.GetActive() };
			bool active_changed{ false };

			switch (dir) {
				case MoveDirection::Down:	   active_changed = a.SetActive("down"); break;
				case MoveDirection::Up:		   active_changed = a.SetActive("up"); break;
				case MoveDirection::Left:	   [[fallthrough]];
				case MoveDirection::DownLeft:  [[fallthrough]];
				case MoveDirection::UpLeft:	   [[fallthrough]];
				case MoveDirection::UpRight:   [[fallthrough]];
				case MoveDirection::DownRight: [[fallthrough]];
				case MoveDirection::Right:	   active_changed = a.SetActive("right"); break;
				default:					   break;
			}
			if (active_changed) {
				prev_active.Get<Tween>().Reset();
			}
			auto& current_active{ a.GetActive() };
			current_active.Get<Tween>().Start(false);
		};
		movement.on_move_stop = [entity = GetEntity()]() {
			entity.Get<AnimationMap>().GetActive().Get<Tween>().Reset();
		};
	}
};

struct Flower : public Sprite {
	Flower() = default;

	Flower(Manager& manager, const V2_float& position, std::string_view texture_key) :
		Sprite{ manager, texture_key } {
		SetPosition(position);
		auto& collider = Add<CircleCollider>(Max(GetSize() / 2.0f));
		Enable();
		collider.SetCollisionCategory(flower_category);
		collider.overlap_only = true;
	}
};

class InventoryScene : public Scene {
public:
	Inventory inventory;

	void Enter() final {
		camera.primary.SetZoom(camera_zoom);
		camera.primary.SetPosition(V2_float{});
		auto inventory_origin{ Origin::CenterBottom };
		auto inventory_position{ camera.primary.GetPosition(inventory_origin) };
		inventory = Inventory{ manager, -inventory_position, inventory_origin, 8 };
	}

	void Update() final {
		auto scroll{ game.input.GetMouseScroll() };
		if (scroll != 0) {
			inventory.IncrementSlot(Sign(scroll));
		}
	}
};

class GameScene : public Scene {
public:
	FractalNoise fractal_noise;

	GameScene() {
		LoadResources("resources/data/resources.json");
	}

	Player player;
	Flower f1;
	Flower f2;
	Flower f3;
	Flower f4;
	Flower f5;
	Flower f6;
	Flower f7;
	Flower f8;
	Flower f9;
	Flower f10;

	void Enter() final {
		SetColliderVisibility(true);

		fractal_noise.SetOctaves(2);
		fractal_noise.SetFrequency(0.055f);
		fractal_noise.SetLacunarity(5);
		fractal_noise.SetPersistence(3);

		player = Player{ manager };

		f1	= Flower{ manager, { 30, 30 }, "flower_0" };
		f2	= Flower{ manager, { 40, 30 }, "flower_1" };
		f3	= Flower{ manager, { 50, 30 }, "flower_2" };
		f4	= Flower{ manager, { 60, 30 }, "flower_3" };
		f5	= Flower{ manager, { 70, 30 }, "flower_4" };
		f6	= Flower{ manager, { 80, 30 }, "flower_5" };
		f7	= Flower{ manager, { 90, 30 }, "flower_6" };
		f8	= Flower{ manager, { 100, 30 }, "flower_7" };
		f9	= Flower{ manager, { 110, 30 }, "flower_8" };
		f10 = Flower{ manager, { 120, 30 }, "flower_9" };

		camera.primary.SetZoom(camera_zoom);
		camera.primary.StartFollow(player);

		game.sound.SetVolume("wind", wind_volume);
		game.sound.Play("wind", 0);
		game.sound.SetVolume("walk", walk_volume);
		game.sound.SetVolume("repair", repair_volume);

		game.scene.Enter<InventoryScene>("inventory");
	}

	void Update() override {
		auto posA{ player.GetAbsoluteTransform().position };
		const auto& interaction_collider{ player.GetChild("interaction").Get<BoxCollider>() };
		auto shortest_distance2{ std::numeric_limits<float>::max() };
		Entity candidate_flower;
		for (const auto& c : interaction_collider.collisions) {
			if (c.entity2.Has<CircleCollider>() &&
				c.entity2.Get<CircleCollider>().GetCollisionCategory() == flower_category) {
				auto posB{ c.entity2.GetAbsoluteTransform().position };
				float dist2{ (posA - posB).MagnitudeSquared() };
				if (dist2 <= shortest_distance2) {
					shortest_distance2 = dist2;
					candidate_flower   = c.entity2;
				}
			}
		}

		if (candidate_flower != Entity{}) {
			DrawDebugCircle(
				candidate_flower.GetAbsoluteTransform().position, 3.0f, color::Orange, -1.0f
			);
		}
		// DrawDebugCircle({ f1.GetPosition().x, f1.GetLowestY() }, 1.0f, color::Orange, -1.0f);
		// DrawDebugCircle({ player.GetPosition().x, player.GetLowestY() }, 1.0f, color::Blue,
		// -1.0f);
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
	game.Init("One Bit Jam", window_size, bg_color);
	game.scene.Enter<GameScene>("game");
	return 0;
}