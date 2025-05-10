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
#include "tile/grid.h"
#include "ui/button.h"

using namespace ptgn;

constexpr V2_int window_size{ 1280, 720 };
constexpr float camera_zoom{ 4.0f };
constexpr Color bg_color{ 126, 206, 120, 255 };

constexpr V2_int grid_size{ 30, 30 };
constexpr V2_int tile_size{ 15, 15 };
constexpr V2_int world_size{ grid_size * tile_size };

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

struct Item {};

struct InventoryComponent {
	Sprite inventory;
	Sprite selector;

	int selected_slot{ 0 };

	V2_float selector_position{ -59.5f, -10.5f }; // Relative to inventory position.

	V2_float selector_offset{ 17, 0 };			  // Relative to previous selector.

	std::vector<GameObject> slots;
};

struct Inventory : public GameObject, public Drawable<Inventory> {
	Inventory() = default;

	Inventory(
		Manager& manager, const V2_float& position, Origin origin, std::size_t slot_count,
		int selected_slot = 0
	) :
		GameObject{ manager } {
		SetDraw<Inventory>();
		auto& i		= Add<InventoryComponent>();
		i.inventory = Sprite{ manager, "inventory" };
		i.inventory.SetParent(*this);
		i.selector = Sprite{ manager, "selector" };
		i.inventory.Hide();
		i.selector.Hide();
		i.selector.SetParent(*this);
		i.selector.SetDepth(1);
		i.selector.SetOrigin(Origin::Center);

		i.slots.resize(slot_count);
		PTGN_ASSERT(selected_slot >= 0);
		i.selected_slot = selected_slot;

		SetPosition(position);
		SetOrigin(origin);

		UpdateSelectorPosition();
	}

	static void Draw(impl::RenderData& ctx, const Entity& entity) {
		const auto& i = entity.Get<InventoryComponent>();
		Sprite::Draw(ctx, i.inventory);
		Sprite::Draw(ctx, i.selector);
		for (const auto& item : i.slots) {
			if (item != Entity{}) {
				Sprite::Draw(ctx, item);
			}
		}
	}

	void UpdateSelectorPosition() {
		auto& i = Get<InventoryComponent>();
		i.selector.SetPosition(GetSlotPosition(i.selected_slot));
	}

	[[nodiscard]] V2_float GetSlotPosition(int slot) const {
		auto& i = Get<InventoryComponent>();
		PTGN_ASSERT(slot >= 0 && static_cast<std::size_t>(slot) < i.slots.size());
		return i.selector_position + slot * i.selector_offset;
	}

	// @return First open slot index, or -1 if no empty slot is open.
	[[nodiscard]] int GetEmptySlot() const {
		auto& inv = Get<InventoryComponent>();
		for (std::size_t i{ 0 }; i < inv.slots.size(); i++) {
			if (inv.slots[i] == Entity{}) {
				return static_cast<int>(i);
			}
		}
		return -1;
	}

	// @return True if an empty slot index exists, false otherwise.
	[[nodiscard]] bool HasEmptySlot() const {
		return GetEmptySlot() != -1;
	}

	void AddEntity(GameObject&& entity) {
		PTGN_ASSERT(HasEmptySlot());
		auto slot = GetEmptySlot();
		SetSlot(slot, std::move(entity));
	}

	void IncrementSlot(int amount) {
		auto& inv = Get<InventoryComponent>();
		PTGN_ASSERT(amount != 0);
		inv.selected_slot -= amount;
		inv.selected_slot  = Mod(inv.selected_slot, static_cast<int>(inv.slots.size()));
		PTGN_ASSERT(inv.selected_slot >= 0);
		UpdateSelectorPosition();
	}

	[[nodiscard]] bool GetSelectedSlot() const {
		const auto& inv = Get<InventoryComponent>();
		return inv.selected_slot;
	}

	[[nodiscard]] GameObject PopSelectedEntity() {
		auto& inv = Get<InventoryComponent>();
		PTGN_ASSERT(
			inv.selected_slot >= 0 && static_cast<std::size_t>(inv.selected_slot) < inv.slots.size()
		);
		GameObject obj{ std::move(inv.slots[inv.selected_slot]) };
		inv.slots[inv.selected_slot] = GameObject{};
		return obj;
	}

	[[nodiscard]] Entity GetSelectedEntity() const {
		const auto& inv = Get<InventoryComponent>();
		PTGN_ASSERT(
			inv.selected_slot >= 0 && static_cast<std::size_t>(inv.selected_slot) < inv.slots.size()
		);
		return inv.slots[inv.selected_slot].GetEntity();
	}

	[[nodiscard]] bool IsSlotTaken(int slot) const {
		auto& inv = Get<InventoryComponent>();
		PTGN_ASSERT(slot >= 0 && static_cast<std::size_t>(slot) < inv.slots.size());
		return inv.slots[static_cast<std::size_t>(slot)] != Entity{};
	}

	void SetSlot(int slot, GameObject&& entity) {
		auto& inv = Get<InventoryComponent>();
		PTGN_ASSERT(slot >= 0 && static_cast<std::size_t>(slot) < inv.slots.size());
		PTGN_ASSERT(!IsSlotTaken(slot));
		entity.SetParent(*this);
		entity.SetPosition(GetSlotPosition(slot));
		entity.SetOrigin(Origin::Center);
		entity.Hide();
		inv.slots[static_cast<std::size_t>(slot)] = std::move(entity);
	}

	void UnsetSlot(int slot) {
		auto& inv = Get<InventoryComponent>();
		PTGN_ASSERT(slot >= 0 && static_cast<std::size_t>(slot) < inv.slots.size());
		inv.slots[static_cast<std::size_t>(slot)] = GameObject{};
	}
};

enum class ActionType {
	None,
	GroundPick,
	GroundPlace
};

struct ActionComponent {
	ActionComponent() = default;

	ActionComponent(Inventory* inventory, Grid<GameObject>* grid, Entity parent);

	Animation action_indicator;
	Animation ground_selector;

	Inventory* inventory{ nullptr };
	Grid<GameObject>* grid{ nullptr };

	void UpdateTile(const V2_int& new_tile) {
		tile = new_tile;
		auto tile_center{ new_tile * tile_size + tile_size / 2.0f };
		ground_selector.SetPosition(tile_center);
		action_indicator.SetPosition(tile_center);
	}

	void Update(const Entity& player) {
		auto player_pos{ player.GetAbsoluteTransform().position };
		V2_int player_tile{ player_pos / tile_size };
		V2_int facing_tile = player_tile + player.Get<TopDownMovement>().facing_direction;

		if (facing_tile != tile) {
			UpdateTile(facing_tile);
			CancelPreviousAction();
		} else {
			UpdateAction();
		}
	}

	void UpdateAction();

	bool CurrentlyDoingAction(ActionType action) const {
		return type == action;
	}

	void TryStartAction(ActionType action) {
		if (CurrentlyDoingAction(action)) {
			return;
		}
		action_indicator.Show();
		ground_selector.Hide();
		type = action;
		action_indicator.Get<Tween>().Start(true);
	}

	void CancelPreviousAction() {
		type = ActionType::None;
		action_indicator.Hide();
		ground_selector.Show();
		ground_selector.Get<Tween>().Start(false);
	}

	ActionType type{ ActionType::None };

	Key action_key{ Key::E };

private:
	V2_int tile;
};

struct Player : public GameObject {
	Player() = default;

	Player(Manager& manager) : GameObject{ manager } {
		V2_float player_starting_position{ world_size / 2.0f };

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
						 animation_duration, -1, V2_float{ 0, animation_size.y }
					 )
		);
		auto& a2 = anim_map.Load(
			"up", Animation(
					  manager, "player_anim", animation_count.x, animation_size, animation_duration,
					  -1, V2_float{ 0, 2.0f * animation_size.y }
				  )
		);

		auto on_repeat = [](auto entity) {
			auto parent{ entity.GetParent() };
			PTGN_ASSERT(parent.Has<WalkRepeats>());
			auto& repeats{ parent.Get<WalkRepeats>() };
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

struct Tile : public Vector2Component<int> {
	using Vector2Component::Vector2Component;
};

struct FlowerComponent {
	FlowerComponent() = default;
};

class GameScene : public Scene {
public:
	FractalNoise fractal_noise;

	GameScene() {
		LoadResources("resources/data/resources.json");
	}

	Grid<GameObject> flowers{ grid_size };

	Player player;
	Inventory inventory;
	RenderTarget ui;

	GameObject CreateShed(const V2_int& position) {}

	GameObject CreateFlower(const V2_int& tile, const V2_int& position, const TextureKey& key) {
		Sprite s{ manager, key };
		s.SetPosition(position);
		s.Add<Tile>(tile);
		return s;
	}

	void Enter() final {
		// SetColliderVisibility(true);

		fractal_noise.SetOctaves(2);
		fractal_noise.SetFrequency(0.055f);
		fractal_noise.SetLacunarity(5);
		fractal_noise.SetPersistence(3);

		RNG<int> flower_type_rng{ 0, 9 };
		RNG<int> flower_rng{ 0, 1 };

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

		player = Player{ manager };

		camera.primary.SetZoom(camera_zoom);
		camera.primary.StartFollow(player);
		camera.primary.SetBounds({ 0, 0 }, world_size);
		physics.SetBounds({ 0, 0 }, world_size);

		game.sound.SetVolume("wind", wind_volume);
		game.sound.Play("wind", 0);
		game.sound.SetVolume("walk", walk_volume);
		game.sound.SetVolume("repair", repair_volume);

		ui = RenderTarget{ manager, window_size };
		auto& ui_camera{ ui.Get<Camera>() };
		ui_camera.SetZoom(camera_zoom);
		ui_camera.SetPosition(V2_float{});
		auto inventory_origin{ Origin::CenterBottom };
		auto inventory_position{ ui_camera.GetPosition(inventory_origin) };
		inventory = Inventory{ manager, -inventory_position, inventory_origin, 8 };

		player.Add<ActionComponent>(&inventory, &flowers, player);
	}

	static constexpr std::array<V2_int, 8> neighbor_tiles{ V2_int{ -1, -1 }, V2_int{ 0, -1 },
														   V2_int{ 1, -1 },	 V2_int{ -1, 0 },
														   V2_int{ 1, 0 },	 V2_int{ -1, 1 },
														   V2_int{ 0, 1 },	 V2_int{ 1, 1 } };

	void Update() override {
		auto scroll{ game.input.GetMouseScroll() };
		if (scroll != 0) {
			inventory.IncrementSlot(Sign(scroll));
		}

		auto player_pos{ player.GetAbsoluteTransform().position };
		V2_int player_tile{ player_pos / tile_size };

		player.Get<ActionComponent>().Update(player);

		ui.Draw(inventory);

		/*flowers.ForEachCoordinate([=](auto tile) {
			DrawDebugRect(tile * tile_size, tile_size, color::Black, Origin::TopLeft, 1.0f);
		});*/

		// DrawDebugRect(player_tile * tile_size, tile_size, color::Gold, Origin::TopLeft, 1.0f);

		/*auto shortest_distance2{ std::numeric_limits<float>::max() };
		Entity candidate_flower;
		for (const auto& rel_neighbor : neighbor_tiles) {
			auto neighbor{ rel_neighbor + player_tile };
			if (!flowers.Has(neighbor)) {
				continue;
			}
			auto flower = flowers.Get(neighbor).GetEntity();
			if (flower != Entity{}) {
				auto flower_pos{ flower.GetAbsoluteTransform().position };
				float dist2{ (player_pos - flower_pos).MagnitudeSquared() };
				if (dist2 <= shortest_distance2) {
					shortest_distance2 = dist2;
					candidate_flower   = flower;
				}
			}
		}
		if (candidate_flower) {
			DrawDebugCircle(
				candidate_flower->GetAbsoluteTransform().position, 3.0f, color::Orange, -1.0f
			);
		}*/

		/*
		const auto& interaction_collider{ player.GetChild("interaction").Get<BoxCollider>() };
		auto shortest_distance2{ std::numeric_limits<float>::max() };
		Entity candidate_flower;
		for (const auto& c : interaction_collider.collisions) {
			if (c.entity2.Has<CircleCollider>() &&
				c.entity2.Get<CircleCollider>().GetCollisionCategory() == flower_category) {

			}
		}

		if (candidate_flower) {
			DrawDebugCircle(
				candidate_flower->GetAbsoluteTransform().position, 3.0f, color::Orange, -1.0f
			);
		}*/
		// DrawDebugCircle({ f1.GetPosition().x, f1.GetLowestY() }, 1.0f, color::Orange, -1.0f);
		// DrawDebugCircle({ player.GetPosition().x, player.GetLowestY() }, 1.0f, color::Blue,
		// -1.0f);
	}
};

ActionComponent::ActionComponent(Inventory* inventory, Grid<GameObject>* grid, Entity parent) :
	inventory{ inventory }, grid{ grid } {
	ground_selector = Animation{ parent.GetManager(), "ground_selector",	2,
								 { 15, 15 },		  milliseconds{ 1000 }, -1 };
	ground_selector.Hide();
	action_indicator =
		Animation{ parent.GetManager(), "pickup_anim", 6, { 15, 15 }, milliseconds{ 500 }, 1 };
	action_indicator.Add<callback::AnimationComplete>([=](auto entity) {
		auto& action = parent.Get<ActionComponent>();
		if (game.input.KeyReleased(action.action_key)) {
			action.type = ActionType::None;
			return;
		}
		if (action.type == ActionType::None) {
			return;
		} else {
			PTGN_ASSERT(action.inventory);
			if (action.type == ActionType::GroundPlace) {
				auto&& selected_entity = action.inventory->PopSelectedEntity();
				auto& grid_entity	   = action.grid->Get(action.tile);
				grid_entity			   = std::move(selected_entity);
				auto position{ action.tile * tile_size + tile_size / 2.0f };
				grid_entity.SetPosition(position);
				grid_entity.RemoveParent();
				grid_entity.Show();
			} else if (action.type == ActionType::GroundPick) {
				auto ground_entity = action.grid->Pop(action.tile);
				action.inventory->AddEntity(std::move(ground_entity));
			}
		}
	});
	action_indicator.Hide();
}

void ActionComponent::UpdateAction() {
	if (!grid->Has(tile) || game.input.KeyReleased(action_key)) {
		CancelPreviousAction();
		return;
	}

	PTGN_ASSERT(inventory);

	auto& grid_entity = grid->Get(tile);

	if (grid_entity == Entity{}) {
		auto selected_entity = inventory->GetSelectedEntity();
		if (selected_entity == Entity{}) {
			CancelPreviousAction();
			return;
		}
		TryStartAction(ActionType::GroundPlace);
	} else {
		auto has_slot = inventory->HasEmptySlot();
		if (!has_slot) {
			CancelPreviousAction();
			return;
		}
		TryStartAction(ActionType::GroundPick);
	}
}

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