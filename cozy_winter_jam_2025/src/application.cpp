#include "protegon/protegon.h"

using namespace ptgn;

constexpr V2_int window_size{ 960, 540 };
constexpr V2_int tile_size{ 8, 8 };
constexpr float camera_zoom{ 3.0f };

constexpr CollisionCategory ground_category{ 1 };
constexpr CollisionCategory tree_category{ 2 };

struct Tree {};

class GameScene : public Scene {
	FractalNoise fractal_noise;

	Texture player_animation{ "resources/entity/player.png" };
	Texture snow_texture{ "resources/tile/snow.png" };
	Texture tree_texture{ "resources/tile/tree.png" };

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

		entity.Add<Transform>(window_size / 2.0f + V2_float{ 100, 100 });
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
			"down",
			Animation{ player_animation, animation_count.x, animation_size, animation_duration }
		);
		anim_map.Load(
			"right", Animation{ player_animation,
								animation_count.x,
								animation_size,
								animation_duration,
								{ 0, animation_size.y } }
		);
		anim_map.Load(
			"up", Animation{ player_animation,
							 animation_count.x,
							 animation_size,
							 animation_duration,
							 { 0, 2.0f * animation_size.y } }
		);
		movement.on_move_start = [=]() {
			// PTGN_LOG("Started moving");
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
			// PTGN_LOG("Stopped moving");
			player.Get<AnimationMap>().GetActive().Reset();
		};

		return entity;
	}

	ecs::Entity player;

	void Exit() override {
		game.tween.Reset();
		manager.Clear();
	}

	RenderTarget ui{ color::Transparent };

	struct Tooltip {
		Tooltip() = default;

		Tooltip(const Text& tooltip_text, const V2_float& static_offset, const RenderTarget& ui) :
			text{ tooltip_text }, offset{ static_offset } {
			fade_in.During(fade_in_time)
				.OnStart([&]() {
					// PTGN_LOG("Started fading in");
					text.SetVisibility(true);
					fade_out.Stop();
					start_text_color = text.GetColor();
				})
				.OnUpdate([&](float f) {
					// PTGN_LOG("Fading in");
					Color color{ start_text_color };
					color.a = static_cast<std::uint8_t>(255.0f * f);
					text.SetColor(color);
				});
			jump_animation.During(up_down_time)
				.Yoyo()
				.Ease(TweenEase::InOutSine)
				.Repeat(-1)
				.OnUpdate([&](float f) {
					Rect r{ anchor_position + offset + V2_float{ 0.0f, -f * up_distance },
							{},
							Origin::CenterBottom };
					auto old_r = game.renderer.GetRenderTarget();
					game.renderer.SetRenderTarget(ui);
					Rect rect{ ui.TransformToTarget(old_r.TransformToScreen(r.Center())),
							   ui.ScaleToTarget(old_r.ScaleToScreen(r.size)), Origin::Center };
					text.Draw(rect);
					game.renderer.SetRenderTarget(old_r);
					// PTGN_LOG("Going up and down");
				});
			fade_out.During(fade_out_time)
				.Reverse()
				.OnStart([&]() {
					// PTGN_LOG("Starting fade out");
					fade_in.Stop();
					start_text_color = text.GetColor();
				})
				.OnComplete([&]() {
					// PTGN_LOG("Completed fade out");
					text.SetVisibility(false);
					jump_animation.Stop();
				})
				.OnUpdate([&](float f) {
					// PTGN_LOG("Fading out");
					Color color{ start_text_color };
					color.a = static_cast<std::uint8_t>(255.0f * f);
					text.SetColor(color);
				});

			game.tween.Add(jump_animation);
			game.tween.Add(fade_in);
			game.tween.Add(fade_out);

			fade_out.KeepAlive(true);
			fade_in.KeepAlive(true);
			jump_animation.KeepAlive(true);
		}

		~Tooltip() {
			game.tween.Remove(jump_animation);
			game.tween.Remove(fade_in);
			game.tween.Remove(fade_out);
		}

		// @return True if the tooltip is currently visible.
		bool IsShowing() const {
			return jump_animation.IsRunning() && text.GetVisibility();
		}

		void FadeIn() {
			fade_in.Start();
			jump_animation.Start();
		}

		void FadeOut() {
			fade_out.Start();
		}

		V2_float anchor_position;

	private:
		// How much time it takes in total for the text to go up and back down.
		seconds up_down_time{ 1 };

		milliseconds fade_in_time{ 400 };
		milliseconds fade_out_time{ 400 };

		// How much distance above the og_position the tween text moves up.
		float up_distance{ 15.0f / camera_zoom };
		Text text;
		// Static offset of tooltip compared to anchor position.
		V2_float offset;
		Tween jump_animation;
		Tween fade_in;
		Tween fade_out;
		Color start_text_color;
	};

	V2_float camera_intro_offset;
	float camera_intro_start_zoom{ camera_zoom };

	Tooltip wasd_tooltip{ Text{ "'WASD' to move", color::Black }.SetSize(20),
						  { 0, -25 / camera_zoom },
						  ui };

	Tooltip tree_tooltip{ Text{ "'E' to chop", color::Black }.SetSize(20),
						  { 0, -25 / camera_zoom },
						  ui };

	void PlayIntro() {
		player.Get<TopDownMovement>().keys_enabled = false;

		camera_intro_offset.x = 300;
		Tween intro;
		intro.During(seconds{ 5 })
			.Reverse()
			.OnUpdate([&](float f) {
				auto& cam{ game.camera.GetPrimary() };
				// Tween is reversed so zoom is lerped backward.
				cam.SetZoom(Lerp(camera_zoom, camera_intro_start_zoom, f));
				cam.SetPosition(player.Get<Transform>().position - camera_intro_offset * f);
				player.Get<TopDownMovement>().Move(MoveDirection::Right);
			})
			.OnComplete([&]() {
				player.Get<TopDownMovement>().keys_enabled					 = true;
				player.Get<BoxColliderGroup>().GetBox("interaction").enabled = true;
				ShowIntroTooltip();
			});
		game.tween.Add(intro).Start();
	}

	void ShowIntroTooltip() {
		Tween tooltip_tween;
		tooltip_tween.During(seconds{ 10 })
			.OnStart([&]() { wasd_tooltip.FadeIn(); })
			.OnUpdate([&]() {
				wasd_tooltip.anchor_position =
					player.Get<BoxColliderGroup>().GetBox("body").GetAbsoluteRect().GetPosition(
						Origin::CenterTop
					);
			})
			.OnComplete([&]() { wasd_tooltip.FadeOut(); });
		game.tween.Add(tooltip_tween).Start();
	}

	void Enter() override {
		game.renderer.SetClearColor(color::White);

		PTGN_ASSERT(manager.Size() == 0);

		V2_float ws{ window_size };

		fractal_noise.SetOctaves(2);
		fractal_noise.SetFrequency(0.055f);
		fractal_noise.SetLacunarity(5);
		fractal_noise.SetPersistence(3);

		player = CreatePlayer();
		manager.Refresh();

		game.camera.GetPrimary().SetZoom(camera_zoom);

		PlayIntro();
		// ShowIntroTooltip();
	}

	void Update() override {
		if (player.Get<TopDownMovement>().keys_enabled) {
			game.camera.GetPrimary().SetPosition(player.Get<Transform>().position);
		}
		Draw();
		DrawUI();
	}

	// Minimum pixels of separation between tree trunk centers.
	float tree_separation_dist{ 100.0f };

	void GenerateTree(const Rect& rect) {
		auto trees{ manager.EntitiesWith<Tree, Transform, BoxCollider>() };
		V2_float center{ rect.Center() };
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
		V2_float tree_hitbox_size{ tile_size.x, 3 * tile_size.y };
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

	void Draw() {
		GenerateTerrain();

		for (auto [e, t, s] : manager.EntitiesWith<Transform, Sprite>()) {
			s.Draw(e);
		}
		/*for (auto [e, b] : manager.EntitiesWith<BoxCollider>()) {
			DrawRect(e, b.GetAbsoluteRect());
		}*/
		for (const auto [e, anim_map] : manager.EntitiesWith<AnimationMap>()) {
			anim_map.Draw(e);
		}
	}

	void DrawUI() {
		game.renderer.SetRenderTarget(ui);

		// Draw UI here.

		game.renderer.SetRenderTarget({});
		ui.Draw();
	}
};

class TextScene : public Scene {
public:
	std::string_view content;
	Color text_color;
	Color bg_color{ color::Black };

	Tween reading_timer;

	TextScene(seconds reading_duration, std::string_view content, const Color& text_color) :
		content{ content }, text_color{ text_color }, reading_timer{ reading_duration } {}

	Text continue_text{ "Press any key to continue", color::Red };
	Text text;

	void Enter() override {
		text = Text{ content, text_color };
		text.SetWrapAfter(300);
		text.SetSize(20);

		continue_text.SetVisibility(false);

		reading_timer.OnComplete([&]() {
			game.event.key.Subscribe(KeyEvent::Down, this, std::function([&](const KeyDownEvent&) {
										 game.event.key.Unsubscribe(this);
										 game.scene.Enter<GameScene>(
											 "game",
											 SceneTransition{ TransitionType::FadeThroughColor,
															  milliseconds{ 1000 } }
												 .SetFadeColorDuration(milliseconds{ 100 })
										 );
									 }));
			continue_text.SetVisibility(true);
		});
		game.tween.Add(reading_timer);
		reading_timer.Start();
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
				seconds{ 0 },
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