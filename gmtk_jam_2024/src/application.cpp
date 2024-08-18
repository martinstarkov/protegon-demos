#include "protegon/protegon.h"

using namespace ptgn;

constexpr const V2_int resolution{ 960, 540 };
constexpr const V2_int center{ resolution / 2 };
constexpr const V2_float scale{ 2.0f, 2.0f };

struct WallComponent {};

struct SpriteSheet {
	SpriteSheet(
		const Texture& texture, const V2_int& source_pos = {}, const V2_int& animation_count = {}
	) :
		texture{ texture },
		source_pos{ source_pos },
		animation_count{ animation_count },
		source_size{ [&]() {
			if (animation_count.IsZero()) {
				return V2_int{};
			}
			return texture.GetSize() / animation_count;
		}() }

	{}

	int row{ 0 };
	V2_int animation_count; // in each direction of the sprite sheet.
	V2_int source_pos;
	// Empty vector defaults to entire texture size.
	V2_int source_size;
	Texture texture;
};

struct ItemComponent {
	ItemComponent() = default;
	bool held{ false };
	float weight_factor{ 1.0f };
};

struct SortByZ {};

struct Position {
	Position(const V2_float& pos) : p{ pos } {}

	/*V2_float GetPosition(ecs::Entity e) const {
		V2_float offset;
		if (e.Has<SpriteSheet>() && e.Has<Origin>()) {
			offset = GetDrawOffset(e.Get<SpriteSheet>().source_size / scale, e.Get<Origin>());
		}
		return p + offset;
	}*/

	V2_float p;
};

struct Size {
	Size(const V2_float& size) : s{ size } {}

	V2_float s;
};

struct Direction {
	Direction(const V2_int& dir) : dir{ dir } {}

	V2_int dir;
};

struct Hitbox {
	Hitbox(ecs::Entity parent, const V2_float& size, const V2_float& offset = {}) :
		parent{ parent }, size{ size }, offset{ offset } {}

	V2_float GetPosition() const {
		PTGN_ASSERT(parent.IsAlive());
		PTGN_ASSERT(parent.Has<Position>());
		return parent.Get<Position>().p + offset;
	}

	Color color{ color::Blue };
	ecs::Entity parent;
	V2_float size;
	V2_float offset;
};

struct PickupHitbox : public Hitbox {
	using Hitbox::Hitbox;
};

struct HandComponent {
	HandComponent(float radius, const V2_float& offset) : radius{ radius }, offset{ offset } {}

	V2_float GetPosition(ecs::Entity e) const {
		const auto& pos = e.Get<Position>().p;
		const auto& d	= e.Get<Direction>();

		return pos + V2_float{ d.dir.x * offset.x, offset.y };
	}

	bool HasItem() const {
		return current_item != ecs::Entity{};
	}

	float GetWeightFactor() const {
		return HasItem() ? weight_factor : 1.0f;
	};

	ecs::Entity current_item;
	V2_float offset;
	// How much slower the player acceleration is when holding an item.
	float weight_factor{ 1.0f };
	float radius{ 0.0f };
};

struct AnimationComponent {
	AnimationComponent(std::size_t tween_key, int column = 0) :
		tween_key{ tween_key }, column{ column } {}

	void Pause() {
		game.tween.Get(tween_key).Pause();
	}

	void Resume() {
		game.tween.Get(tween_key).Resume();
	}

	int column{ 0 };
	std::size_t tween_key;
};

struct Velocity {
	Velocity() = default;

	Velocity(const V2_float& velocity, const V2_float& max) : current{ velocity }, max{ max } {}

	V2_float current;
	V2_float max;
};

struct Acceleration {
	Acceleration(const V2_float& current, const V2_float& max) : current{ current }, max{ max } {}

	V2_float current;
	V2_float max;
};

struct ZIndex {
	ZIndex(float z_index) : z_index{ z_index } {}

	float z_index{ 0.0f };
};

struct GridComponent {
	GridComponent(const V2_int& cell) : cell{ cell } {}

	V2_int cell;
};

class GameScene : public Scene {
public:
	ecs::Manager manager;

	V2_int tile_size{ 32, 32 };
	V2_int grid_size{ 30, 17 };

	bool draw_hitboxes{ false };

	Key item_interaction_key{ Key::E };

	Surface level;
	Texture background;

	ecs::Entity player;
	ecs::Entity bowl;

	GameScene() {
		level	   = Surface{ "resources/level/level0.png" };
		background = Texture{ "resources/ui/background.png" };
	}

	void CreatePlayer() {
		player = manager.CreateEntity();

		auto& ppos = player.Add<Position>(V2_float{ 215, 290 });
		player.Add<Velocity>(V2_float{}, V2_float{ 1500.0f });
		player.Add<Acceleration>(V2_float{}, V2_float{ 2400.0f });
		player.Add<Origin>(Origin::CenterBottom);
		player.Add<Flip>(Flip::None);
		player.Add<Direction>(V2_int{ 0, 1 });

		V2_int player_animation_count{ 4, 3 };

		// Must be added before AnimationComponent as it pauses the animation immediately.
		player.Add<SpriteSheet>(
			Texture{ "resources/entity/player.png" }, V2_int{}, player_animation_count
		);

		milliseconds animation_duration{ 400 };

		TweenConfig animation_config;
		animation_config.repeat = -1;

		animation_config.on_pause = [&](auto& t, auto v) {
			player.Get<AnimationComponent>().column = 0;
		};
		animation_config.on_update = [&](auto& t, auto v) {
			player.Get<AnimationComponent>().column = static_cast<int>(std::floorf(v));
		};
		animation_config.on_repeat = [&](auto& t, auto v) {
			player.Get<AnimationComponent>().column = 0;
		};

		auto tween = game.tween.Load(
			Hash("player_movement_animation"), 0.0f, static_cast<float>(player_animation_count.x),
			animation_duration, animation_config
		);

		auto& anim = player.Add<AnimationComponent>(Hash("player_movement_animation"));
		anim.Pause();

		V2_int size{ tile_size.x, 2 * tile_size.y };

		auto& s = player.Add<Size>(size);
		V2_float hitbox_size{ size * V2_float{ 0.75f, 0.28f } };
		player.Add<Hitbox>(player, hitbox_size, V2_float{ 0.0f, 0.0f });
		player.Add<HandComponent>(8.0f, V2_float{ 8.0f, -s.s.y * 0.3f });
		player.Add<GridComponent>(ppos.p / grid_size);
		player.Add<DynamicCollisionShape>(DynamicCollisionShape::Rectangle);
		player.Add<SortByZ>();
		player.Add<ZIndex>(0.0f);

		manager.Refresh();
	}

	ecs::Entity CreateItem(
		const V2_float& pos, const path& texture, float hitbox_scale = 1.0f,
		float weight_factor = 1.0f
	) {
		auto item = manager.CreateEntity();

		Texture t{ texture };
		V2_int texture_size{ t.GetSize() };

		auto& i			= item.Add<ItemComponent>();
		i.weight_factor = weight_factor;
		V2_float size{ scale * texture_size };
		item.Add<Size>(size);
		item.Add<PickupHitbox>(item, size * hitbox_scale);
		item.Add<Hitbox>(item, size);
		item.Add<Origin>(Origin::Center);
		item.Add<Position>(pos);
		item.Add<SpriteSheet>(t, V2_int{}, V2_int{});
		item.Add<SortByZ>();
		item.Add<ZIndex>(0.0f);
		item.Add<Velocity>(V2_float{}, V2_float{ 1500.0f });
		item.Add<Acceleration>(V2_float{}, V2_float{ 3000.0f });
		item.Add<DynamicCollisionShape>(DynamicCollisionShape::Rectangle);

		manager.Refresh();

		return item;
	}

	void CreateWall(const V2_int& cell) {
		auto wall = manager.CreateEntity();
		wall.Add<WallComponent>();
		wall.Add<GridComponent>(cell);
		wall.Add<Size>(tile_size);
		wall.Add<Hitbox>(wall, tile_size);
		wall.Add<Position>(cell * tile_size);
		wall.Add<Origin>(Origin::TopLeft);
		wall.Add<DynamicCollisionShape>(DynamicCollisionShape::Rectangle);

		manager.Refresh();
	}

	void Init() final {
		CreatePlayer();

		bowl = CreateItem({ 300, 300 }, "resources/entity/bowl.png", 2.0f, 0.8f);

		level.ForEachPixel([&](const V2_int& cell, const Color& color) {
			if (color == color::Black) {
				CreateWall(cell);
			}
		});
	}

	void PlayerMovementInput(float dt) {
		auto& v	   = player.Get<Velocity>();
		auto& a	   = player.Get<Acceleration>();
		auto& f	   = player.Get<Flip>();
		auto& t	   = player.Get<SpriteSheet>();
		auto& hand = player.Get<HandComponent>();
		auto& anim = player.Get<AnimationComponent>();
		auto& dir  = player.Get<Direction>().dir;

		bool up{ game.input.KeyPressed(Key::W) };
		bool down{ game.input.KeyPressed(Key::S) };
		bool right{ game.input.KeyPressed(Key::D) };
		bool left{ game.input.KeyPressed(Key::A) };

		bool movement{ up || down || right || left };

		if (movement) {
			anim.Resume();
		} else {
			anim.Pause();
		}

		if (up) {
			a.current.y = -1;
		} else if (down) {
			a.current.y = 1;
		} else {
			a.current.y = 0;
			v.current.y = 0;
		}

		if (left) {
			a.current.x = -1;
			f			= Flip::Horizontal;
		} else if (right) {
			a.current.x = 1;
			f			= Flip::None;
		} else {
			a.current.x = 0;
			v.current.x = 0;
		}

		// Store previous direction.
		if (a.current.x != 0 || a.current.y != 0) {
			dir = V2_int{ a.current };
		}

		int front_row{ 0 };
		int side_row{ 1 };
		int back_row{ 2 };

		/*if (t.row != 2) {
			t.row = 0;
		}*/

		// Sideways movement / animation prioritized over up and down.

		if (dir.x != 0) {
			t.row = 1;
		} else if (dir.y == 1) {
			t.row = 0;
		} else if (dir.y == -1) {
			t.row = 2;
		}
		// PTGN_INFO("Animation frame: ", anim.column);
		t.source_pos.x = t.source_size.x * anim.column;

		t.source_pos.y = t.source_size.y * t.row;

		a.current = a.current.Normalized() * a.max * hand.GetWeightFactor();
	}

	void ResolveWallCollisions(
		float dt, V2_float& position, ecs::Entity entity, bool reset_velocity = false
	) {
		V2_float adjust = game.collision.dynamic.Sweep(
			dt, entity,
			manager.EntitiesWith<Position, Hitbox, Origin, DynamicCollisionShape, WallComponent>(),
			[](ecs::Entity e) { return e.Get<Hitbox>().GetPosition(); },
			[](ecs::Entity e) { return e.Get<Hitbox>().size; },
			[](ecs::Entity e) {
				if (e.Has<Velocity>()) {
					return e.Get<Velocity>().current;
				}
				return V2_float{};
			},
			[](ecs::Entity e) {
				if (e.Has<Origin>()) {
					return e.Get<Origin>();
				}
				return Origin::Center;
			},
			[](ecs::Entity e) {
				PTGN_ASSERT(e.Has<DynamicCollisionShape>());
				return e.Get<DynamicCollisionShape>();
			},
			DynamicCollisionResponse::Slide
		);
		position += adjust;
		if (reset_velocity && !adjust.IsZero()) {
			entity.Get<Velocity>().current = {};
		}
	}

	void UpdatePhysics(float dt) {
		float drag{ 10.0f };

		for (auto [e, p, v, a] : manager.EntitiesWith<Position, Velocity, Acceleration>()) {
			v.current += a.current * dt;

			v.current.x = std::clamp(v.current.x, -v.max.x, v.max.x);
			v.current.y = std::clamp(v.current.y, -v.max.y, v.max.y);

			v.current.x -= drag * v.current.x * dt;
			v.current.y -= drag * v.current.y * dt;

			if (e.Has<ItemComponent>()) {
				auto& item = e.Get<ItemComponent>();
				if (!item.held) {
					ResolveWallCollisions(dt, p.p, e, true);
				}
			} else if (e == player) {
				ResolveWallCollisions(dt, p.p, e);
			}

			p.p += v.current * dt;
		}
	}

	void UpdatePlayerHand() {
		auto& hand	 = player.Get<HandComponent>();
		auto& hitbox = player.Get<Hitbox>();
		Circle<float> circle{
			hand.GetPosition(player),
			hand.radius,
		};
		if (game.input.KeyDown(item_interaction_key)) {
			if (!hand.HasItem()) {
				for (auto [e, p, h, o, i] :
					 manager.EntitiesWith<Position, PickupHitbox, Origin, ItemComponent>()) {
					Rectangle<float> r{ h.GetPosition(), h.size, o };
					if (draw_hitboxes) {
						game.renderer.DrawRectangleHollow(r, color::Red);
					}
					if (game.collision.overlap.CircleRectangle(circle, r)) {
						// hitbox.color	  = color::Red;
						// h.color			  = color::Red;
						hand.current_item = e;
					}
				}
				if (hand.HasItem()) {
					auto& item{ hand.current_item.Get<ItemComponent>() };
					hand.weight_factor = item.weight_factor;
					item.held		   = true;
				}
			} else {
				PTGN_ASSERT(hand.current_item.Has<ItemComponent>());
				hand.current_item.Get<ItemComponent>().held = false;
				const auto& dir								= player.Get<Direction>().dir;
				auto& h_item{ hand.current_item.Get<Hitbox>() };
				auto& o_item{ hand.current_item.Get<Origin>() };

				// If item is not in the wall, throw it, otherwise push it out of the wall.

				Rectangle<float> r_item{ h_item.GetPosition(), h_item.size, o_item };

				bool wall{ false };
				IntersectCollision max;
				for (auto [e, p, h, o, s, w] :
					 manager.EntitiesWith<
						 Position, Hitbox, Origin, DynamicCollisionShape, WallComponent>()) {
					Rectangle<float> r{ h.GetPosition(), h.size, o };
					IntersectCollision c;
					if (game.collision.intersect.RectangleRectangle(r_item, r, c)) {
						wall = true;
						if (c.depth > max.depth) {
							max = c;
						}
					}
				}
				if (wall) {
					hand.current_item.Get<Position>().p += max.depth * max.normal;
				} else {
					auto& item_vel{ hand.current_item.Get<Velocity>() };
					auto& vel{ player.Get<Velocity>() };
					item_vel.current = vel.current.Normalized() * vel.max * 0.65f;
				}
				// auto& item_accel{ hand.current_item.Get<Acceleration>() };
				// item_accel.current = item_accel.max * dir;
				//  TODO: Add effect to throw item in direction player is facing.
				hand.current_item = {};
			}
		}
		if (hand.HasItem()) {
			hand.current_item.Get<Position>().p = circle.center;
			if (player.Get<Direction>().dir.y == -1) {
				player.Get<ZIndex>().z_index			= 0.1f;
				hand.current_item.Get<ZIndex>().z_index = 0.0f;
			} else {
				player.Get<ZIndex>().z_index			= 0.0f;
				hand.current_item.Get<ZIndex>().z_index = 0.1f;
			}
		}
	}

	void ResetHitboxColors() {
		for (auto [e, h] : manager.EntitiesWith<Hitbox>()) {
			h.color = color::Blue;
		}
	}

	/*void UpdateZIndices() {
		auto entities				= manager.EntitiesWith<Position, Hitbox, ZIndex, SortByZ>();
		std::vector<ecs::Entity> ev = entities.GetVector();

		std::sort(ev.begin(), ev.end(), [&](ecs::Entity a, ecs::Entity b) {
			return a.Get<Position>().p.y < b.Get<Position>().p.y;
		});

		const float z_delta{ 1.0f / (ev.size() - 1) };

		for (std::size_t i = 0; i < ev.size(); ++i) {
			ev[i].Get<ZIndex>().z_index = z_delta * static_cast<float>(i);
			Print(ev[i].GetId(), ", ");
		}
		PrintLine();
	}*/

	void Update(float dt) final {
		ResetHitboxColors();

		PlayerMovementInput(dt);

		UpdatePhysics(dt);

		UpdatePlayerHand();

		// UpdateZIndices();

		Draw();
	}

	void DrawWalls() {
		for (auto [e, p, s, h, origin, w] :
			 manager.EntitiesWith<Position, Size, Hitbox, Origin, WallComponent>()) {
			game.renderer.DrawRectangleHollow(
				p.p, s.s, h.color, 0.0f, { 0.5f, 0.5f }, 1.0f, origin
			);
		}
	}

	void DrawItems() {
		for (auto [e, p, s, h, o, ss, item] :
			 manager.EntitiesWith<Position, Size, Hitbox, Origin, SpriteSheet, ItemComponent>()) {
			if (draw_hitboxes) {
				game.renderer.DrawRectangleHollow(
					p.p + h.offset, h.size, h.color, 0.0f, {}, 1.0f, o
				);
			}
			game.renderer.DrawTexture(
				p.p, s.s, ss.texture, ss.source_pos, ss.source_size, 0.0f, { 0.5f, 0.5f },
				Flip::None, Origin::Center, e.Has<ZIndex>() ? e.Get<ZIndex>().z_index : 0.0f
			);
		}
	}

	void DrawPlayer() {
		const auto& pos	   = player.Get<Position>().p;
		const auto& size   = player.Get<Size>().s;
		const auto& hitbox = player.Get<Hitbox>();
		const auto& hand   = player.Get<HandComponent>();
		const auto origin  = player.Get<Origin>();
		const auto& t	   = player.Get<SpriteSheet>();
		const auto& flip   = player.Get<Flip>();
		const auto& dir	   = player.Get<Direction>();

		if (draw_hitboxes) {
			game.renderer.DrawCircleHollow(hand.GetPosition(player), hand.radius, hitbox.color);
			game.renderer.DrawRectangleHollow(
				pos + hitbox.offset, hitbox.size, color::Blue, 0.0f, { 0.5f, 0.5f }, 1.0f, origin
			);
		}
		game.renderer.DrawTexture(
			pos, size, t.texture, t.source_pos, t.source_size, 0.0f, { 0.5f, 0.5f }, flip, origin,
			player.Has<ZIndex>() ? player.Get<ZIndex>().z_index : 0.0f
		);
	}

	void Draw() {
		// game.renderer.DrawTexture(game.window.GetCenter(), resolution, background);
		// if (draw_hitboxes) {
		DrawWalls();
		//}
		DrawPlayer();
		DrawItems();
	}
};

class MainMenu : public Scene {
public:
	std::vector<std::shared_ptr<Button>> buttons;

	Texture background;

	MainMenu() {}

	void Init() final {
		game.scene.Load<GameScene>(Hash("game"));

		const int button_y_offset{ 14 };
		const V2_int button_size{ 192, 48 };
		const V2_int first_button_coordinate{ 161, 193 };

		auto add_solid_button = [&](const ButtonActivateFunction& f, const Color& color,
									const Color& hover_color) {
			SolidButton b;
			b.SetOnActivate(f);
			b.SetColor(color);
			b.SetHoverColor(hover_color);
			buttons.emplace_back(std::make_shared<SolidButton>(b));
		};

		add_solid_button(
			[]() { game.scene.SetActive(Hash("game")); }, color::Blue, color::DarkBlue
		);
		add_solid_button(
			[]() { game.scene.SetActive(Hash("game")); }, color::Green, color::DarkGreen
		);
		add_solid_button([]() { game.scene.SetActive(Hash("game")); }, color::Red, color::DarkRed);

		for (int i = 0; i < (int)buttons.size(); i++) {
			buttons[i]->SetRectangle({ V2_int{ first_button_coordinate.x,
											   first_button_coordinate.y +
												   i * (button_size.y + button_y_offset) },
									   button_size, Origin::CenterTop });
			buttons[i]->SubscribeToMouseEvents();
		}

		background = Texture{ "resources/ui/background.png" };
	}

	void Update() final {
		game.renderer.DrawTexture(game.window.GetCenter(), resolution, background);
		for (std::size_t i = 0; i < buttons.size(); i++) {
			buttons[i]->Draw();
		}
	}
};

class SetupScene : public Scene {
public:
	SetupScene() {}

	void Init() final {
		game.renderer.SetClearColor(color::Silver);
		game.window.SetSize(resolution);

		/*std::size_t initial_scene{ Hash("game") };
		game.scene.Load<GameScene>(initial_scene);*/
		std::size_t initial_scene{ Hash("main_menu") };
		game.scene.Load<MainMenu>(initial_scene);
		game.scene.SetActive(initial_scene);
	}
};

int main() {
	game.Start<SetupScene>();
	return 0;
}
