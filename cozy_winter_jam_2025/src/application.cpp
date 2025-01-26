#include "protegon/protegon.h"

using namespace ptgn;

constexpr V2_int window_size{ 1280, 720 };
constexpr V2_int tile_size{ 8, 8 };
constexpr float camera_zoom{ 4.0f };

constexpr CollisionCategory ground_category{ 1 };
constexpr CollisionCategory tree_category{ 2 };

struct Tree {};

Tween CreateFadingTween(
	const std::function<void(float)>& fade_function,
	const std::function<void(float)>& update_function,
	const std::function<void()>& start_function	   = nullptr,
	const std::function<void()>& complete_function = nullptr
) {
	Tween tween{ game.tween.Load()
					 .During(milliseconds{ 150 })
					 .OnStart(start_function)
					 .OnUpdate(fade_function)
					 .During(seconds{ 1 })
					 .OnStart([](float f) { PTGN_ASSERT(f == 0.0f); })
					 .Yoyo()
					 .Repeat(-1)
					 .Ease(TweenEase::InOutSine)
					 .OnUpdate(update_function)
					 .During(milliseconds{ 150 })
					 .Reverse()
					 .OnUpdate(fade_function)
					 .OnComplete(complete_function) };
	return tween;
}

struct Tooltip {
	Tooltip() = default;

	Tooltip(Text tooltip_text, const V2_float& static_offset) {
		auto draw_tooltip = [this, tooltip_text,
							 static_offset](float alpha, float v_offset) mutable {
			vertical_offset = v_offset;
			PTGN_LOG(vertical_offset);
			auto old_cam{ game.camera.GetPrimary() };
			Rect r{ old_cam.TransformToScreen(
						anchor_position + static_offset + V2_float{ 0.0f, vertical_offset }
					),
					{},
					Origin::Center };
			Color color{ tooltip_text.GetColor() };
			color.a = static_cast<std::uint8_t>(alpha);
			tooltip_text.SetColor(color);
			game.renderer.Flush();
			game.camera.SetPrimary({});
			tooltip_text.Draw(r);
			game.renderer.Flush();
			game.camera.SetPrimary(old_cam);
		};

		tween = CreateFadingTween(
			[=](float f) mutable { std::invoke(draw_tooltip, 128.0f * f, vertical_offset); },
			[=](float f) mutable {
				// How much distance above the og_position the tween text moves up.
				float up_distance{ 15.0f / camera_zoom };
				PTGN_LOG("f: ", f);
				PTGN_LOG("up_distance: ", up_distance);
				std::invoke(draw_tooltip, 255.0f, -f * up_distance);
			},
			[&]() { vertical_offset = 0.0f; }, [&]() { vertical_offset = 0.0f; }
		);
	}

	// @return True if the tooltip is currently visible.
	bool IsShowing() const {
		return tween.IsRunning();
	}

	void FadeIn() {
		tween.StartIfNotRunning();
	}

	void FadeOut() {
		tween.IncrementTweenPoint();
	}

	V2_float anchor_position;

private:
	float vertical_offset{ 0.0f };
	// Static offset of tooltip compared to anchor position.
	Tween tween;
};

struct Waypoint {
	Waypoint() = default;

	Waypoint(const Texture& texture) {
		auto draw_waypoint = [&](float alpha, float vertical_offset) {
			PTGN_ASSERT(texture.IsValid());
			Rect r{ anchor_position + offset + V2_float{ 0.0f, vertical_offset },
					{},
					Origin::Center };
			Color color{ color::White };
			color.a = static_cast<std::uint8_t>(alpha);
			texture.Draw(r, { color });
		};

		tween = CreateFadingTween(
			[=](float f) mutable { std::invoke(draw_waypoint, 128.0f * f, -f * up_distance); },
			[=](float f) mutable { std::invoke(draw_waypoint, 255.0f, -f * up_distance); }
		);
	}

	// @return True if the waypoint is currently visible.
	bool IsShowing() const {
		return tween.IsRunning();
	}

	void FadeIn() {
		tween.StartIfNotRunning();
	}

	void FadeOut() {
		tween.IncrementTweenPoint();
	}

	V2_float GetAnchorPosition() const {
		return anchor_position;
	}

	void SetAnchorPosition(const V2_float& position) {
		anchor_position = position;
	}

	// Offset of the texture from the anchor position.
	void SetStaticOffset(const V2_float& static_offset) {
		offset = static_offset;
	}

private:
	V2_float anchor_position;

	// How much distance above the og_position the tween text moves up.
	float up_distance{ 15.0f / camera_zoom };
	// Static offset of tooltip compared to anchor position.
	V2_float offset;
	Tween tween;
};

class GameScene : public Scene {
	FractalNoise fractal_noise;

	Texture player_animation{ "resources/entity/player.png" };
	Texture snow_texture{ "resources/tile/snow.png" };
	Texture tree_texture{ "resources/tile/tree.png" };
	Texture house_texture{ "resources/tile/house.png" };
	Texture waypoint_texture{ "resources/ui/waypoint.png" };
	Texture arrow_texture{ "resources/ui/arrow.png" };

	Waypoint waypoint{ waypoint_texture };

	ecs::Entity CreateWall(const Rect& r) {
		ecs::Entity entity = manager.CreateEntity();
		entity.Add<Transform>(r.position, r.rotation);
		auto& box = entity.Add<BoxCollider>(entity, r.size, r.origin);
		box.SetCollisionCategory(ground_category);
		entity.Add<DrawColor>(color::Purple);
		return entity;
	}

	ecs::Entity CreatePlayer() {
		ecs::Entity entity = manager.CreateEntity();

		V2_float player_starting_position{ -305.0f, 0.0f };
		entity.Add<Transform>(player_starting_position);
		auto& rb = entity.Add<RigidBody>();

		V2_float hitbox_size{ 14, 14 };

		auto& b = entity.Add<BoxColliderGroup>(entity, manager);
		b.AddBox(
			"body", {}, 0.0f, hitbox_size, Origin::Center, true, 0, {}, nullptr, nullptr, nullptr,
			nullptr, false, true
		);
		b.AddBox(
			"interaction", {}, 0.0f, hitbox_size * 2.0f, Origin::Center, false, 0, {},
			[&](const Collision& collision) {
				if (collision.entity2.Has<Tree>() && !tree_tooltip.IsShowing()) {
					tree_tooltip.FadeIn();
					tree_tooltip.anchor_position =
						collision.entity2.Get<BoxCollider>().GetAbsoluteRect().GetPosition(
							Origin::CenterTop
						);
				}
			},
			nullptr,
			[&](const Collision& collision) {
				if (collision.entity2.Has<Tree>() && tree_tooltip.IsShowing() &&
					tree_tooltip.anchor_position ==
						collision.entity2.Get<BoxCollider>().GetAbsoluteRect().GetPosition(
							Origin::CenterTop
						)) {
					tree_tooltip.FadeOut();
				}
			},
			nullptr, true, false
		);

		// entity.Add<Sprite>(player_animation);

		auto& movement = entity.Add<TopDownMovement>();

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
		V2_float animation_size{ 16, 17 };
		milliseconds animation_duration{ 500 };

		auto& anim_map = entity.Add<AnimationMap>(
			"down", player_animation, animation_count.x, animation_size, animation_duration
		);
		anim_map.Load(
			"right", player_animation, animation_count.x, animation_size, animation_duration,
			V2_float{ 0, animation_size.y }
		);
		anim_map.Load(
			"up", player_animation, animation_count.x, animation_size, animation_duration,
			V2_float{ 0, 2.0f * animation_size.y }
		);
		movement.on_move_start = [=]() {
			player.Get<AnimationMap>().GetActive().StartIfNotRunning();
		};
		movement.on_direction_change = [=](MoveDirection) {
			auto& a{ player.Get<AnimationMap>() };
			auto dir{ player.Get<TopDownMovement>().GetDirection() };
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
			a.GetActive().StartIfNotRunning();
		};
		movement.on_move_stop = [=]() {
			player.Get<AnimationMap>().GetActive().Reset();
		};
		return entity;
	}

	ecs::Entity player;

	void Exit() override {
		game.tween.Reset();
		manager.Clear();
	}

	V2_float camera_intro_offset{ -10, 0 };
	float camera_intro_start_zoom{ camera_zoom };

	Tooltip wasd_tooltip{ Text{ "'WASD' to move", color::Black }.SetSize(20), { 0, -5 } };

	Tooltip tree_tooltip{ Text{ "'E' to chop", color::Black }.SetSize(20), { 0, -5 } };

	void EnablePlayerInteraction(bool enable = true) {
		player.Get<BoxColliderGroup>().GetBox("interaction").enabled = enable;
	}

	void PlayIntro() {
		player.Get<TopDownMovement>().keys_enabled = false;

		game.tween.Load()
			.During(seconds{ 5 })
			.Reverse()
			.OnUpdate([&](float f) {
				auto& cam{ game.camera.GetPrimary() };
				// Tween is reversed so zoom is lerped backward.
				cam.SetZoom(Lerp(camera_zoom, camera_intro_start_zoom, f));
				cam.SetPosition(player.Get<Transform>().position - camera_intro_offset * f);
				player.Get<TopDownMovement>().Move(MoveDirection::Right);
			})
			.OnComplete([&]() {
				player.Get<TopDownMovement>().keys_enabled = true;
				EnablePlayerInteraction();
				ShowIntroTooltip();
			})
			.Start();
	}

	void ShowIntroTooltip() {
		auto func = [&]() {
			wasd_tooltip.FadeIn();
		};
		game.tween.Load()
			.During(seconds{ 10 })
			.OnStart(func)
			.OnUpdate([&]() {
				V2_float pos{
					player.Get<BoxColliderGroup>().GetBox("body").GetAbsoluteRect().GetPosition(
						Origin::CenterTop
					)
				};
				wasd_tooltip.anchor_position = pos;
			})
			.OnComplete([&]() {
				PTGN_LOG("Fading out wasd tooltip");
				wasd_tooltip.FadeOut();
			})
			.Start();
	}

	Rect house_rect;
	Rect house_perimeter;

	void Enter() override {
		game.renderer.SetClearColor(color::White);

		PTGN_ASSERT(manager.Size() == 0);

		V2_float ws{ window_size };

		fractal_noise.SetOctaves(2);
		fractal_noise.SetFrequency(0.055f);
		fractal_noise.SetLacunarity(5);
		fractal_noise.SetPersistence(3);

		house_rect			  = Rect{ { 0, 0 }, house_texture.GetSize(), Origin::Center };
		house_perimeter		  = house_rect;
		house_perimeter.size *= 1.5f;

		player = CreatePlayer();
		manager.Refresh();

		game.camera.GetPrimary().SetZoom(camera_zoom);

		auto draw_waypoint_arrow = [=](float alpha, float waypoint_scale) {
			Color color{ waypoint_arrow_color };
			color.a = static_cast<std::uint8_t>(alpha);

			const auto& player_pos{ player.Get<Transform>().position };
			const auto& waypoint_pos{ waypoint.GetAnchorPosition() };

			V2_float dir{ waypoint_pos - player_pos };

			const float arrow_pixels_from_player{ 18.0f };

			V2_float arrow_pos{ player_pos + dir.Normalized() * arrow_pixels_from_player };

			float arrow_rotation{ dir.Angle() };

			V2_float arrow_size{ arrow_texture.GetSize() * waypoint_scale };

			arrow_texture.Draw(
				Rect{ arrow_pos, arrow_size, Origin::Center, arrow_rotation }, { color }
			);
		};

		waypoint_arrow_tween = CreateFadingTween(
			[=](float f) { std::invoke(draw_waypoint_arrow, 128.0f * f, 1.0f); },
			[=](float f) mutable {
				std::invoke(
					draw_waypoint_arrow, 128.0f + 128.0f * f,
					Lerp(waypoint_arrow_start_scale, waypoint_arrow_end_scale, f)
				);
			}
		);

		Light ambient{ game.window.GetCenter(), color::Orange };
		ambient.ambient_color_	   = color::DarkBlue;
		ambient.ambient_intensity_ = 0.3f;
		ambient.radius_			   = 400.0f;
		ambient.compression_	   = 50.0f;
		ambient.SetIntensity(0.6f);
		game.light.Load("ambient_light", ambient);

		PlayIntro();
		// EnablePlayerInteraction();
		// ShowIntroTooltip();
	}

	void Update() override {
		auto& player_pos = player.Get<Transform>().position;

		if (player.Get<TopDownMovement>().keys_enabled) {
			game.camera.GetPrimary().SetPosition(player_pos);
		}

		if (game.input.MouseDown(Mouse::Left)) {
			auto mouse_pos = game.input.GetMousePosition();
			waypoint.SetAnchorPosition(mouse_pos);
			waypoint.FadeIn();
		}
		if (game.input.MouseDown(Mouse::Right)) {
			waypoint.FadeOut();
		}

		Draw();
	}

	// Minimum pixels of separation between tree trunk centers.
	float tree_separation_dist{ 100.0f };

	void GenerateTree(const Rect& rect) {
		auto trees{ manager.EntitiesWith<Tree, Transform, BoxCollider>() };
		V2_float center{ rect.Center() };
		if (rect.Overlaps(house_perimeter)) {
			return;
		}
		float tree_dist2{ tree_separation_dist * tree_separation_dist };
		for (auto [e, t, transform, b] : trees) {
			float dist2{ (center - transform.position).MagnitudeSquared() };
			if (dist2 < tree_dist2) {
				// Cannot generate tree, too close to another one.
				return;
			}
		}
		auto tree = manager.CreateEntity();
		tree.Add<Transform>(center);
		tree.Add<Tree>();
		V2_float tree_hitbox_size{ 3 * tile_size.x, 4 * tile_size.y };
		auto& box = tree.Add<BoxCollider>(tree, tree_hitbox_size, Origin::Center);
		box.SetCollisionCategory(tree_category);
		tree.Add<DrawColor>(color::Red);
		tree.Add<DrawLineWidth>(3.0f);
		tree.Add<Sprite>(tree_texture);
		manager.Refresh();
	}

	void GenerateTerrain() {
		const auto& cam{ game.camera.GetPrimary() };

		auto cam_rect{ cam.GetRect() };

		V2_int min{ cam_rect.Min() / tile_size - V2_int{ 1 } };
		V2_int max{ cam_rect.Max() / tile_size + V2_int{ 1 } };

		for (int i{ min.x }; i < max.x; i++) {
			for (int j{ min.y }; j < max.y; j++) {
				V2_int p{ i, j };
				float noise_value{ fractal_noise.Get((float)i, (float)j) };

				// Color color{ color::Black };
				Rect r{ p * tile_size, tile_size, Origin::TopLeft };
				int divisions{ 3 };

				float opacity_range{ 1.0f / static_cast<float>(divisions) };

				auto range{ static_cast<int>(noise_value / opacity_range) };

				// color.a = static_cast<std::uint8_t>(255.0f * static_cast<float>(range) *
				// opacity_range);

				if (range == 1) {
					GenerateTree(r);
				}
				snow_texture.Draw(r);
			}
		}

		game.renderer.Flush();

		manager.Refresh();
	}

	Tween waypoint_arrow_tween;
	Color waypoint_arrow_color{ color::Gold };
	float waypoint_arrow_start_scale{ 1.2f };
	float waypoint_arrow_end_scale{ 0.8f };

	float sky_opacity{ 128.0f };

	void Draw() {
		auto& player_pos{ player.Get<Transform>().position };

		GenerateTerrain();

		for (auto [e, t, s] : manager.EntitiesWith<Transform, Sprite>()) {
			s.Draw(e);
		}

		house_texture.Draw(house_rect);

		if (waypoint.IsShowing()) {
			waypoint_arrow_tween.StartIfNotRunning();
		} else {
			waypoint_arrow_tween.IncrementTweenPoint();
		}

		for (auto [e, b] : manager.EntitiesWith<BoxCollider>()) {
			DrawRect(e, b.GetAbsoluteRect());
		}

		Color dusk{ color::DarkBlue };

		if (game.input.KeyPressed(Key::UP)) {
			sky_opacity += 10.0f * game.dt();
		}
		if (game.input.KeyPressed(Key::DOWN)) {
			sky_opacity -= 10.0f * game.dt();
		}
		sky_opacity = std::clamp(sky_opacity, 0.0f, 255.0f);

		dusk.a = static_cast<std::uint8_t>(sky_opacity);
		// game.camera.GetPrimary().GetRect().Draw(dusk);

		// game.light.Get("ambient_light").SetPosition(player_pos);
		// game.light.Draw();

		for (const auto [e, anim_map] : manager.EntitiesWith<AnimationMap>()) {
			anim_map.Draw(e);
		}
	}
};

class TextScene : public Scene {
public:
	std::string_view content;
	Color text_color;
	Color bg_color{ color::Black };

	TextScene(std::string_view content, const Color& text_color) :
		content{ content }, text_color{ text_color } {}

	Text continue_text{ "Press any key to continue", color::Red };
	Text text;
	seconds reading_duration{ 1 };

	void Enter() override {
		text = Text{ content, text_color };
		text.SetWrapAfter(400);
		text.SetSize(30);

		continue_text.SetVisibility(false);

		game.tween.Load()
			.During(reading_duration)
			.OnComplete([&]() {
				game.event.key.Subscribe(
					KeyEvent::Down, this, std::function([&](const KeyDownEvent&) {
						game.event.key.Unsubscribe(this);
						game.scene.Enter<GameScene>(
							"game", SceneTransition{ TransitionType::FadeThroughColor,
													 milliseconds{ 1000 } }
										.SetFadeColorDuration(milliseconds{ 100 })
						);
					})
				);
				continue_text.SetVisibility(true);
			})
			.Start();
	}

	void Update() override {
		Rect::Fullscreen().Draw(bg_color);
		Rect text_rect{ game.window.GetCenter(), text.GetSize(), Origin::Center };
		text.Draw(text_rect);
		continue_text.Draw(
			{ { text_rect.Center().x, text_rect.Max().y + 30 }, {}, Origin::CenterTop }
		);
	}
};

class MainMenu : public Scene {
public:
	Button play;
	Texture background{ "resources/ui/background.png" };

	void Enter() override {
		play.Set<ButtonProperty::OnActivate>([]() {
			game.scene.Enter<TextScene>(
				"text_scene",
				SceneTransition{ TransitionType::FadeThroughColor,
								 milliseconds{ milliseconds{ 1000 } } }
					.SetFadeColorDuration(milliseconds{ 500 }),
				"In your busy life full of work and stress you make time once a year to get away "
				"from it all. Your cabin awaits you in the quiet wilderness of Alaska...",
				color::White
			);
		});
		play.Set<ButtonProperty::BackgroundColor>(color::DarkGray);
		play.Set<ButtonProperty::BackgroundColor>(color::Gray, ButtonState::Hover);
		Text text{ "Play", color::Black };
		play.Set<ButtonProperty::Text>(text);
		play.Set<ButtonProperty::TextSize>(V2_float{ 0.0f, 0.0f });
		play.SetRect({ game.window.GetCenter(), { 200, 100 }, Origin::CenterTop });
	}

	void Update() override {
		background.Draw();
		play.Draw();
	}
};

int main([[maybe_unused]] int c, [[maybe_unused]] char** v) {
	game.Init("Cozy Winter Jam", window_size, color::Transparent);
	if (false) {
		game.Start<GameScene>(
			"game", SceneTransition{ TransitionType::FadeThroughColor, milliseconds{ 1000 } }
		);
	} else {
		game.Start<MainMenu>(
			"main_menu", SceneTransition{ TransitionType::FadeThroughColor, milliseconds{ 500 } }
		);
	}
	return 0;
}