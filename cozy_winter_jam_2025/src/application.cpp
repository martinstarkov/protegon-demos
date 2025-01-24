#include "protegon/protegon.h"

using namespace ptgn;

constexpr V2_int window_size{ 960, 540 };

constexpr CollisionCategory ground_category{ 1 };

class GameScene : public Scene {
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
		auto& m	 = entity.Add<TopDownMovement>();
		// Maximum movement speed.
		m.max_speed = 2.0f * 60.0f;
		// How fast to reach max speed.
		m.max_acceleration = 20.0f * 60.0f;
		// How fast to stop after letting go.
		m.max_deceleration = 20.0f * 60.0f;
		// How fast to stop when changing direction.
		m.max_turn_speed = 60.0f * 60.0f;

		m.friction	 = 1.0f;
		auto& b		 = entity.Add<BoxCollider>(entity, V2_float{ 20, 40 }, Origin::Center);
		b.continuous = true;

		entity.Add<DrawColor>(color::DarkGreen);
		entity.Add<DrawLineWidth>(-1.0f);

		return entity;
	}

	ecs::Entity player;

	void Enter() override {
		manager.Clear();

		V2_float ws{ window_size };

		player = CreatePlayer();
		CreateWall({ { 0, ws.y - 10 }, { ws.x, 10 }, Origin::TopLeft });
		CreateWall({ { 0, ws.y / 2.0f }, { 200, 10 }, Origin::TopLeft });
		CreateWall({ { ws.x, ws.y / 2.0f }, { 200, 10 }, Origin::TopRight });
		CreateWall({ { ws.x - 200, ws.y / 2.0f + 140 }, { ws.x - 400, 10 }, Origin::TopRight });
		manager.Refresh();
	}

	void Exit() override {
		manager.Clear();
	}

	void Update() override {
		for (auto [e, b] : manager.EntitiesWith<BoxCollider>()) {
			DrawRect(e, b.GetAbsoluteRect());
		}
		game.camera.GetPrimary().SetPosition(player.Get<Transform>().position);
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

	void Enter() override {
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

	void Exit() override {
		PTGN_LOG("Exited text scene");
	}

	~TextScene() override {
		PTGN_LOG("Unloaded text scene");
	}

	void Update() override {
		Rect::Fullscreen().Draw(bg_color);
		Text text{ content, text_color };
		text.Draw({ game.window.GetCenter(), {}, Origin::Center });
		continue_text.Draw({ game.window.GetCenter() + V2_float{ 0, 30 + text.GetSize().y },
							 {},
							 Origin::CenterTop });
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
				seconds{ 5 }, "Can you read this text in 5 seconds?", color::White
			);
		});
		play.Set<ButtonProperty::BackgroundColor>(color::DarkGray);
		play.Set<ButtonProperty::BackgroundColor>(color::Gray, ButtonState::Hover);
		Text text{ "Play", color::Black };
		play.Set<ButtonProperty::Text>(text);
		play.Set<ButtonProperty::TextSize>(V2_float{ 0.0f, 0.0f });
		play.SetRect({ game.window.GetCenter(), { 200, 100 }, Origin::CenterTop });
	}

	void Exit() override {
		PTGN_LOG("Exited main menu");
	}

	~MainMenu() override {
		PTGN_LOG("Unloaded main menu");
	}

	void Update() override {
		background.Draw();
		play.Draw();
	}
};

int main([[maybe_unused]] int c, [[maybe_unused]] char** v) {
	game.Init("Cozy Winter Jam", window_size, color::Transparent);
	game.Start<MainMenu>(
		"main_menu", SceneTransition{ TransitionType::FadeThroughColor, milliseconds{ 500 } }
	);
	PTGN_LOG("The end");
	return 0;
}