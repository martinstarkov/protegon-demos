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

constexpr V2_int window_size{ 1280, 720 };
constexpr float camera_zoom{ 4.0f };
constexpr Color bg_color{ 126, 206, 120, 255 };

constexpr V2_int grid_size{ 30, 30 };
constexpr V2_int tile_size{ 15, 15 };
constexpr V2_int world_size{ grid_size * tile_size };

constexpr int walk_volume{ 90 };
constexpr int repair_volume{ 30 };
constexpr int pick_volume{ 60 };
constexpr int plant_volume{ 15 };
constexpr int music_volume{ 8 };
constexpr int wind_volume{ 2 };
constexpr int walk_sound_frequency{ 2 }; // every second repeat of the walk animation

constexpr CollisionCategory player_category{ 0 };
constexpr CollisionCategory interaction_category{ 1 };
constexpr CollisionCategory flower_category{ 2 };
constexpr CollisionCategory wall_category{ 3 };

struct WalkRepeats : public ArithmeticComponent<int> {
	using ArithmeticComponent::ArithmeticComponent;
};

struct Item {};

struct Hidden {};

struct InventoryComponent {
	Sprite inventory;
	Sprite selector;

	int selected_slot{ 0 };

	V2_float selector_position{ -59.5f, -10.5f }; // Relative to inventory position.

	V2_float selector_offset{ 17, 0 };			  // Relative to previous selector.

	std::vector<Entity> slots;
};

struct Inventory : public Entity, public Drawable<Inventory> {
	Inventory() = default;

	Inventory(
		Manager& manager, const V2_float& position, Origin origin, std::size_t slot_count,
		int selected_slot = 0
	) :
		Entity{ manager } {
		SetDraw<Inventory>();
		auto& i		= Add<InventoryComponent>();
		i.inventory = CreateSprite(manager, "inventory");
		i.inventory.SetParent(*this);
		i.selector = CreateSprite(manager, "selector");
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
			if (item != Entity{} && !item.Has<Hidden>()) {
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
			if (!inv.slots[i]) {
				return static_cast<int>(i);
			}
		}
		return -1;
	}

	// @return True if an empty slot index exists, false otherwise.
	[[nodiscard]] bool HasEmptySlot() const {
		return GetEmptySlot() != -1;
	}

	void AddEntity(Entity&& entity) {
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

	[[nodiscard]] int GetSelectedSlot() const {
		const auto& inv = Get<InventoryComponent>();
		return inv.selected_slot;
	}

	[[nodiscard]] Entity PopSelectedEntity() {
		auto& inv = Get<InventoryComponent>();
		PTGN_ASSERT(
			inv.selected_slot >= 0 && static_cast<std::size_t>(inv.selected_slot) < inv.slots.size()
		);
		Entity obj{ std::move(inv.slots[inv.selected_slot]) };
		inv.slots[inv.selected_slot] = Entity{};
		return obj;
	}

	[[nodiscard]] Entity GetSlotEntity(int slot) const {
		const auto& inv = Get<InventoryComponent>();
		PTGN_ASSERT(slot >= 0 && static_cast<std::size_t>(slot) < inv.slots.size());
		return inv.slots[slot];
	}

	void MoveEntity(int from, int to) {
		if (from == to) {
			return;
		}
		auto& inv		  = Get<InventoryComponent>();
		auto& from_entity = inv.slots[from];
		auto& to_entity	  = inv.slots[to];
		PTGN_ASSERT(from_entity);
		// PTGN_ASSERT(!to_entity);
		from_entity.SetPosition(GetSlotPosition(to));
		std::swap(from_entity, to_entity);
	}

	[[nodiscard]] Entity GetSelectedEntity() const {
		const auto& inv = Get<InventoryComponent>();
		PTGN_ASSERT(
			inv.selected_slot >= 0 && static_cast<std::size_t>(inv.selected_slot) < inv.slots.size()
		);
		return inv.slots[inv.selected_slot];
	}

	[[nodiscard]] bool IsSlotTaken(int slot) const {
		auto& inv = Get<InventoryComponent>();
		PTGN_ASSERT(slot >= 0 && static_cast<std::size_t>(slot) < inv.slots.size());
		return inv.slots[static_cast<std::size_t>(slot)] != Entity{};
	}

	void SetSlot(int slot, Entity&& entity) {
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
		inv.slots[static_cast<std::size_t>(slot)] = Entity{};
	}
};

struct Tooltip : public Entity, public Drawable<Tooltip> {
	Tooltip() = default;

	Tooltip(
		Manager& manager, std::string_view content, const Color& text_color = color::Black,
		std::string_view font_key = ""
	) :
		Entity{ manager } {
		auto& text = Add<Text>(CreateText(manager, content, text_color, font_key));
		text.SetParent(*this);
		SetDraw<Tooltip>();
	}

	static void Draw(impl::RenderData& ctx, const Entity& entity) {
		auto& text = entity.Get<Text>();
		auto size  = text.GetSize(text);
		ctx.AddQuad(
			text.GetAbsoluteTransform().position, size, Origin::Center, -1, text.GetDepth(),
			text.GetBlendMode(), color::White.Normalized(), 0.0f, false
		);
		Text::Draw(ctx, text);
	}
};

enum class ActionType {
	None,
	GroundPick,
	GroundPlace,
	OpenAnalyzer
};

struct ActionComponent {
	ActionComponent() = default;

	Tooltip tooltip;

	void ShowTooltip(Entity analyzer) {
		tooltip =
			Tooltip{ analyzer.GetManager(), "Hold 'E' to open analyzer", color::Black, "ui_font" };
		// auto pos{ V2_int{ entity.Get<Tile>() } * tile_size };
		tooltip.SetPosition({ -window_size.x / 2.0f + 190, window_size.y / 2.0f - 35 });
		tooltip.SetOrigin(Origin::Center);
		tooltip.Get<Text>().SetFontSize(30);
		// auto& scene = game.scene.Get<GameScene>("game");
		// pos			= scene.camera.primary.TransformToScreen(pos);
		// pos			= scene.screen_follow.Get<Camera>().TransformToCamera(pos);
		// tooltip.SetPosition(pos);
		tooltip.Hide();
	}

	ActionComponent(Inventory* inventory, Grid<Entity>* grid, Entity parent);

	Animation action_indicator;
	Animation ground_selector;

	Inventory* inventory{ nullptr };
	Grid<Entity>* grid{ nullptr };

	void UpdateTile(const V2_int& new_tile);

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
		action_indicator.Start(true);
	}

	void CancelPreviousAction() {
		type = ActionType::None;
		action_indicator.Hide();
		ground_selector.Show();
		ground_selector.Start(false);
	}

	ActionType type{ ActionType::None };

	Key action_key{ Key::E };

private:
	V2_int tile;
};

struct Player : public Entity {
	Player() = default;

	Player(Manager& manager) : Entity{ manager } {
		V2_float player_starting_position{ world_size / 2.0f };

		Add<Transform>(player_starting_position);
		auto& rb = Add<RigidBody>();
		Add<Enabled>();
		Add<Depth>(1);

		V2_float hitbox_size{ 10, 6 };
		V2_float hitbox_offset{ 0, 8 };

		auto body_hitbox = manager.CreateEntity();
		body_hitbox.Add<BoxCollider>(hitbox_size, Origin::CenterBottom);
		body_hitbox.Add<Transform>(hitbox_offset);
		body_hitbox.Add<Enabled>();
		body_hitbox.Add<RigidBody>();

		auto interaction_hitbox = manager.CreateEntity();
		auto& interaction_collider =
			interaction_hitbox.Add<BoxCollider>(V2_float{ 28, 28 }, Origin::Center);
		interaction_collider.overlap_only = true;
		interaction_hitbox.Add<Transform>(V2_float{});
		interaction_hitbox.Enable();

		AddChild(body_hitbox, "body");
		AddChild(interaction_hitbox, "interaction");

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
			CreateAnimation(
				manager, "player_anim", animation_duration, animation_count.x, animation_size
			)
		);
		auto& a0 = anim_map.GetActive();
		auto& a1 = anim_map.Load(
			"right", CreateAnimation(
						 manager, "player_anim", animation_duration, animation_count.x,
						 animation_size, -1, V2_float{ 0, animation_size.y }
					 )
		);
		auto& a2 = anim_map.Load(
			"up", CreateAnimation(
					  manager, "player_anim", animation_duration, animation_count.x, animation_size,
					  -1, V2_float{ 0, 2.0f * animation_size.y }
				  )
		);

		auto on_repeat = [](auto entity) {
			auto parent{ entity.GetParent() };
			PTGN_ASSERT(parent.template Has<WalkRepeats>());
			auto& repeats{ parent.template Get<WalkRepeats>() };
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

		// TODO: Fix.
		/*a0.Add<callback::AnimationRepeat>(on_repeat);
		a1.Add<callback::AnimationRepeat>(on_repeat);
		a2.Add<callback::AnimationRepeat>(on_repeat);*/

		movement.on_move_start = [*this]() {
			Get<AnimationMap>().GetActive().Start(false);
		};
		movement.on_direction_change = [*this](MoveDirection) {
			auto& a{ Get<AnimationMap>() };
			auto dir{ Get<TopDownMovement>().GetDirection() };
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
				prev_active.Reset();
			}
			auto& current_active{ a.GetActive() };
			current_active.Start(false);
		};
		movement.on_move_stop = [*this]() {
			Get<AnimationMap>().GetActive().Reset();
		};
	}
};

struct Tile : public Vector2Component<int> {
	using Vector2Component::Vector2Component;
};

struct EntrySlot : public ArithmeticComponent<int> {
	using ArithmeticComponent::ArithmeticComponent;
};

struct FlowerComponent {
	RNG<int> stat_rng{ 0, 10 };

	FlowerComponent() {
		invasiveness = stat_rng();
		spread_rate	 = stat_rng();
		longevity	 = stat_rng();
	}

	int longevity{ 0 };
	int spread_rate{ 0 };
	int invasiveness{ 0 };
};

struct AnalyzerComponent {
	Sprite analyzer;
	Sprite entry;
	Text stat1;
	Text stat2;
	Text stat3;

	V2_float analyzer_scale{ 1.5f, 1.5f };

	AnalyzerComponent(Manager& manager) : analyzer{ CreateSprite(manager, "analyzer") } {
		analyzer.Hide();
		analyzer.SetScale(analyzer_scale);
	}

	void Open(Inventory& inv) {
		open = true;
	}

	void Close(Inventory& inv) {
		open	= false;
		auto& i = inv.Get<InventoryComponent>();
		for (auto& e : i.slots) {
			e.Remove<Hidden>();
		}
		auto slot = entry.Has<EntrySlot>() ? entry.Get<EntrySlot>() : EntrySlot{ 0 };
		HideInfo(inv, slot);
	}

	bool IsOpen() const {
		return open;
	}

	void ShowInfo(Entity& entity, int selected_slot) {
		const auto& flower{ entity.Get<FlowerComponent>() };
		stat1 = CreateText(
			entity.GetManager(), "Longevity: " + std::to_string(flower.longevity), color::Black,
			"ui_font"
		);
		stat2 = CreateText(
			entity.GetManager(), "Invasiveness: " + std::to_string(flower.invasiveness),
			color::Black, "ui_font"
		);
		stat3 = CreateText(
			entity.GetManager(), "Spread Rate: " + std::to_string(flower.spread_rate), color::Black,
			"ui_font"
		);
		stat1.Hide();
		stat2.Hide();
		stat3.Hide();
		stat1.SetFontSize(30);
		stat2.SetFontSize(30);
		stat3.SetFontSize(30);
		stat1.SetOrigin(Origin::CenterLeft);
		stat2.SetOrigin(Origin::CenterLeft);
		stat3.SetOrigin(Origin::CenterLeft);
		stat1.SetTextJustify(TextJustify::Left);
		stat2.SetTextJustify(TextJustify::Left);
		stat3.SetTextJustify(TextJustify::Left);
		stat1.SetDepth(2);
		stat2.SetDepth(2);
		stat3.SetDepth(2);
		stat1.SetPosition({ -15, -50 });
		stat2.SetPosition({ -15, 0 });
		stat3.SetPosition({ -15, 50 });
		entry = CreateSprite(entity.GetManager(), entity.Get<TextureHandle>());
		entry.SetScale(analyzer_scale);
		entry.Hide();
		entry.SetDepth(2);
		entry.Add<EntrySlot>(selected_slot);
		entry.SetPosition({ -43, -12 });
	}

	void HideInfo(Inventory& inventory, int selected_slot) {
		if (entry) {
			inventory.MoveEntity(entry.Get<EntrySlot>(), selected_slot);
		}
		auto selected = inventory.GetSelectedEntity();
		selected.Remove<Hidden>();
		entry.Destroy();
		entry = {};
		stat1.Destroy();
		stat1 = {};
		stat2.Destroy();
		stat2 = {};
		stat3.Destroy();
		stat3 = {};
	}

	void Update(ActionComponent& action, Entity analyzer_entity, Inventory& inventory) {
		if (game.input.KeyDown(Key::ESCAPE)
			/*game.input.KeyDown(Key::E)*/ /* || TODO: hit button to exit analyzer */) {
			Close(inventory);
			action.CancelPreviousAction();
			action.ShowTooltip(analyzer_entity);
		}

		if (open && game.input.KeyDown(Key::E)) {
			auto selected = inventory.GetSelectedEntity();
			if (!selected || selected.Has<Hidden>()) { // Empty slot selected
				if (entry) {						   // Analyzer has entry
					auto selected_slot = inventory.GetSelectedSlot();
					HideInfo(inventory, selected_slot);
				}
			} else {		 // Flower selected
				auto selected_slot = inventory.GetSelectedSlot();
				if (entry) { // Analyzer has entry.
					// Restore previous entry back to inventory.
					auto slot_entity = inventory.GetSlotEntity(entry.Get<EntrySlot>());
					slot_entity.Remove<Hidden>();
					selected.Add<Hidden>();
					ShowInfo(selected, selected_slot);
				} else {
					selected.Add<Hidden>();
					ShowInfo(selected, selected_slot);
				}
			}
		}
	}

private:
	bool open{ false };
};

class GameScene : public Scene {
public:
	FractalNoise fractal_noise;

	GameScene() {
		LoadResources("resources/data/resources.json");
	}

	Grid<Entity> flowers{ grid_size };

	Player player;
	Inventory inventory;
	RenderTarget ui;
	RenderTarget screen;
	RenderTarget screen_follow;
	Entity shed;

	Entity CreateWall(const V2_float& pos, const V2_float& size, Origin origin) {
		auto entity = CreateEntity();
		entity.Add<Transform>(pos);
		entity.Add<Enabled>();
		auto& box = entity.Add<BoxCollider>(size, origin);
		box.SetCollisionCategory(wall_category);
		return entity;
	}

	Entity CreateShed() {
		auto house_size{ game.texture.GetSize("shed") };
		auto house_pos = world_size / 2.0f + V2_float{ house_size.x, -house_size.y / 2.0f };
		Sprite s{ CreateSprite(manager, "shed") };
		s.SetPosition(house_pos);
		s.SetOrigin(Origin::TopLeft);
		const auto& house_hitboxes{ game.json.Get("shed_data").at("hitboxes") };
		for (const auto& obj : house_hitboxes) {
			PTGN_ASSERT(obj.contains("size"));
			PTGN_ASSERT(obj.contains("position"));
			CreateWall(
				house_pos + V2_float{ obj.at("position") }, V2_float{ obj.at("size") },
				Origin::TopLeft
			);
		}
		return s;
	}

	Entity CreateFlower(const V2_int& tile, const V2_int& position, const TextureHandle& key) {
		Sprite s{ CreateSprite(manager, key) };
		s.SetPosition(position);
		s.Add<Tile>(tile);
		auto& flower = s.Add<FlowerComponent>();
		// TODO: Add lifetimes.
		// milliseconds lifetime{ milliseconds{ 1000 } + flower.longevity * milliseconds{ 1000 } };
		// s.Add<Lifetime>(lifetime);
		return s;
	}

	Entity CreateAnalyzer(const V2_int& tile, const V2_int& position) {
		Entity s{ manager };
		s.SetPosition(position);
		s.Add<Tile>(tile);
		s.Add<AnalyzerComponent>(manager);
		return s;
	}

	void ClearFlowersUnderShed() {
		auto shed_position{ shed.GetPosition() };
		V2_int shed_tile_min{ shed_position / tile_size };
		shed_tile_min -= V2_int{ 1, 1 };
		V2_int shed_tile_max{ (shed_position + Sprite{ shed }.GetTextureSize()) / tile_size };
		shed_tile_max += V2_int{ 1, 1 };
		for (auto i = shed_tile_min.x; i < shed_tile_max.x; i++) {
			for (auto j = shed_tile_min.y; j < shed_tile_max.y; j++) {
				flowers.Get({ i, j }).Destroy();
				flowers.Set({ i, j }, Entity{ manager });
			}
		}
		V2_int analyzer_tile{ shed_tile_min + V2_int{ 3, 3 } };
		flowers.Set(
			analyzer_tile,
			CreateAnalyzer(analyzer_tile, analyzer_tile * tile_size + tile_size / 2.0f)
		);
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
		shed   = CreateShed();

		ClearFlowersUnderShed();

		camera.primary.SetZoom(camera_zoom);
		camera.primary.StartFollow(player);
		camera.primary.SetBounds({ 0, 0 }, world_size);
		physics.SetBounds({ 0, 0 }, world_size);

		game.sound.SetVolume("wind", wind_volume);
		game.sound.Play("wind", 0, -1);
		game.sound.SetVolume("music", music_volume);
		game.sound.Play("music", 1, -1);
		game.sound.SetVolume("walk", walk_volume);
		game.sound.SetVolume("repair", repair_volume);

		ui			  = CreateRenderTarget(manager, window_size);
		screen		  = CreateRenderTarget(manager, window_size);
		screen_follow = CreateRenderTarget(manager, window_size);
		screen_follow.Get<Camera>().StartFollow(player);
		auto& ui_camera{ ui.Get<Camera>() };
		ui_camera.SetZoom(camera_zoom);
		ui_camera.SetPosition(V2_float{});
		screen.Get<Camera>().SetPosition(V2_float{});
		auto inventory_origin{ Origin::CenterBottom };
		auto inventory_position{ ui_camera.GetPosition(inventory_origin) };
		inventory = Inventory{ manager, -inventory_position, inventory_origin, 8 };
		ui.SetDepth(2);
		screen_follow.SetDepth(1);
		screen.SetDepth(3);

		player.Add<ActionComponent>(&inventory, &flowers, player);
	}

	static constexpr std::array<V2_int, 8> neighbor_tiles{ V2_int{ -1, -1 }, V2_int{ 0, -1 },
														   V2_int{ 1, -1 },	 V2_int{ -1, 0 },
														   V2_int{ 1, 0 },	 V2_int{ -1, 1 },
														   V2_int{ 0, 1 },	 V2_int{ 1, 1 } };

	void Update() override {
		ui.Clear();
		screen.Clear();
		screen_follow.Clear();

		auto scroll{ game.input.GetMouseScroll() };
		if (scroll != 0) {
			inventory.IncrementSlot(Sign(scroll));
		}
		if (game.input.KeyDown(Key::LEFT)) {
			inventory.IncrementSlot(1);
		}
		if (game.input.KeyDown(Key::RIGHT)) {
			inventory.IncrementSlot(-1);
		}

		auto& action{ player.Get<ActionComponent>() };
		for (auto [e, a] : manager.EntitiesWith<AnalyzerComponent>()) {
			a.Update(action, e, inventory);
			player.Get<TopDownMovement>().keys_enabled = !a.IsOpen();
			if (a.IsOpen()) {
				a.analyzer.SetPosition(ui.Get<Camera>().GetPosition());
				ui.Draw(a.analyzer);
				if (a.entry) {
					ui.Draw(a.entry);
				}
				if (a.stat1) {
					screen.Draw(a.stat1);
				}
				if (a.stat2) {
					screen.Draw(a.stat2);
				}
				if (a.stat3) {
					screen.Draw(a.stat3);
				}
			}
		}

		auto player_pos{ player.GetAbsoluteTransform().position };
		V2_int player_tile{ player_pos / tile_size };

		action.Update(player);

		if (action.tooltip) {
			screen.Draw(action.tooltip);
		}

		ui.Draw(inventory);

		/*flowers.ForEachCoordinate([=](auto tile) {
			DrawDebugRect(tile * tile_size, tile_size, color::Black, Origin::TopLeft, 1.0f);
		});*/

		// DrawDebugRect(player_tile * tile_size, tile_size, color::Gold,
		// Origin::TopLeft, 1.0f);

		/*auto shortest_distance2{ std::numeric_limits<float>::max() };
		Entity candidate_flower;
		for (const auto& rel_neighbor : neighbor_tiles) {
			auto neighbor{ rel_neighbor + player_tile };
			if (!flowers.Has(neighbor)) {
				continue;
			}
			auto flower = flowers.Get(neighbor).GetEntity();
			if (flower) {
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

ActionComponent::ActionComponent(Inventory* inventory, Grid<Entity>* grid, Entity parent) :
	inventory{ inventory }, grid{ grid } {
	ground_selector = CreateAnimation(
		parent.GetManager(), "ground_selector", milliseconds{ 1000 }, 2, { 15, 15 }, -1
	);
	ground_selector.Hide();
	action_indicator =
		CreateAnimation(parent.GetManager(), "pickup_anim", milliseconds{ 500 }, 6, { 15, 15 }, 1);
	// TODO: Fix.
	/*
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
				game.sound.SetVolume("plant", plant_volume);
				game.sound.Play("plant");
			} else if (action.type == ActionType::GroundPick) {
				auto ground_entity = action.grid->Pop(action.tile);
				action.inventory->AddEntity(std::move(ground_entity));
				game.sound.SetVolume("pick", pick_volume);
				game.sound.Play("pick");
			} else if (action.type == ActionType::OpenAnalyzer) {
				action.tooltip.Get<Text>().SetContent("Press 'ESC' to exit analyzer");
				auto& grid_entity = action.grid->Get(action.tile);
				auto& analyzer	  = grid_entity.Get<AnalyzerComponent>();
				if (!analyzer.IsOpen()) {
					analyzer.Open(*action.inventory);
				}
			}
		}
	});
	*/
	action_indicator.Hide();
}

void ActionComponent::UpdateAction() {
	if (!grid->Has(tile)) {
		CancelPreviousAction();
		return;
	}

	if (!game.input.KeyDown(action_key)) {
		return;
	}

	PTGN_ASSERT(inventory);

	auto& grid_entity = grid->Get(tile);

	if (!grid_entity) {
		auto selected_entity = inventory->GetSelectedEntity();
		if (!selected_entity) {
			CancelPreviousAction();
			return;
		}
		TryStartAction(ActionType::GroundPlace);
	} else if (grid_entity.Has<FlowerComponent>()) {
		auto has_slot = inventory->HasEmptySlot();
		if (!has_slot) {
			CancelPreviousAction();
			return;
		}
		TryStartAction(ActionType::GroundPick);
	} else if (grid_entity.Has<AnalyzerComponent>()) {
		TryStartAction(ActionType::OpenAnalyzer);
	}
}

void ActionComponent::UpdateTile(const V2_int& new_tile) {
	tile = new_tile;
	if (grid->Has(tile)) {
		auto& entity = grid->Get(tile);
		if (entity.Has<AnalyzerComponent>()) {
			ShowTooltip(entity);
		} else {
			tooltip.Destroy();
			tooltip = {};
		}
	} else {
		tooltip.Destroy();
		tooltip = {};
	}
	auto tile_center{ new_tile * tile_size + tile_size / 2.0f };
	ground_selector.SetPosition(tile_center);
	action_indicator.SetPosition(tile_center);
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