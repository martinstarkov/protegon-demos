#include "protegon/protegon.h"

using namespace ptgn;

constexpr V2_int resolution{ 960, 540 };

constexpr int button_y_offset{ 14 };
constexpr V2_int button_size{ 250, 50 };
constexpr V2_int first_button_coordinate{ 250, 220 };

enum class Difficulty {
	Easy,
	Medium,
	Hard,
};

class GameScene : public Scene {
public:
	ecs::Manager manager;

	constexpr static float zoom{ 2.0f };
	constexpr static V2_int tile_size{ V2_int{ 16, 16 } };
	constexpr static V2_int grid_size{ V2_int{ 60, 34 } };

	constexpr static float player_accel{ 1000.0f };
	constexpr static float player_max_speed{ 70.0f };

	~GameScene() {
		game.tween.Clear();
	}

	GameScene(Difficulty difficulty) {}

	void BackToMenu() {
		game.scene.TransitionActive(
			"game", "level_select",
			SceneTransition{ TransitionType::FadeThroughColor, milliseconds{ 1000 } }
		);
		game.scene.Unload("game");
	}

	void Update() override {
		PTGN_LOG("Game scene");
	}

	/*
	void CreatePlayer() {
		player = manager.CreateEntity();

		auto& ppos		= player.Add<Transform>(V2_float{ 215, 290 });
		auto& rb		= player.Add<RigidBody>();
		rb.max_velocity = player_max_speed;

		V2_uint animation_count{ 4, 3 };
		V2_int animation_size{ 16, 32 };

		auto& anim_map = player.Add<AnimationMap>(
			"down",
			Animation{ player_texture, animation_count.x, animation_size, milliseconds{ 400 } }
		);
		anim_map.Load(
			"right", Animation{ player_texture,
								animation_count.x,
								animation_size,
								milliseconds{ 400 },
								{ 0.0f, static_cast<float>(animation_size.y) } }
		);
		anim_map.Load(
			"up", Animation{ player_texture,
							 animation_count.x,
							 animation_size,
							 milliseconds{ 400 },
							 { 0.0f, 2.0f * animation_size.y } }
		);

		auto& box = player.Add<BoxCollider>(player, V2_int{ 8, 8 });
		player.Add<HandComponent>(8.0f, V2_float{ 8.0f, -2.0f * tile_size.y * 0.3f });
		// player.Add<SortByZ>();
		// player.Add<LayerInfo>(0.0f, 0);

		manager.Refresh();
	}*/
};

static Button CreateMenuButton(
	const std::string& content, const Color& text_color, const ButtonCallback& f,
	const Color& color, const Color& hover_color
) {
	Button b;
	b.Set<ButtonProperty::OnActivate>(f);
	b.Set<ButtonProperty::BackgroundColor>(color);
	b.Set<ButtonProperty::BackgroundColor>(hover_color, ButtonState::Hover);
	Text text{ content, color, Hash("menu_font") };
	b.Set<ButtonProperty::Text>(text);
	b.Set<ButtonProperty::TextSize>(V2_float{
		static_cast<float>(b.Get<ButtonProperty::Text>().GetSize().x), 0.0f });
	b.Set<ButtonProperty::LineThickness>(7.0f);
	return b;
}

class LevelSelect : public Scene {
public:
	std::vector<Button> buttons;

	void StartGame(Difficulty difficulty) {
		game.scene.Load<GameScene>("game", difficulty);
		game.scene.TransitionActive("level_select", "game");
	}

	void Init() final {
		buttons.clear();
		buttons.push_back(CreateMenuButton(
			"Easy", color::Blue, [&]() { StartGame(Difficulty::Easy); }, color::Blue, color::Black
		));
		buttons.push_back(CreateMenuButton(
			"Medium", color::Green, [&]() { StartGame(Difficulty::Medium); }, color::Gold,
			color::Black
		));
		buttons.push_back(CreateMenuButton(
			"Hard", color::Red, [&]() { StartGame(Difficulty::Hard); }, color::Red, color::Black
		));
		buttons.push_back(CreateMenuButton(
			"Back", color::Black,
			[]() { game.scene.TransitionActive("level_select", "main_menu"); }, color::LightGray,
			color::Black
		));
		for (std::size_t i = 0; i < buttons.size(); i++) {
			buttons[i].SetRect({ V2_int{ first_button_coordinate.x,
										 first_button_coordinate.y +
											 (int)i * (button_size.y + button_y_offset) },
								 button_size, Origin::CenterTop });
		}
	}

	void Shutdown() final {
		for (auto& b : buttons) {
			b.Disable();
		}
	}

	void Update() final {
		game.texture.Get("menu_background").Draw();
		for (auto& b : buttons) {
			b.Draw();
		}
	}
};

class MainMenu : public Scene {
public:
	std::vector<Button> buttons;

	void Init() final {
		buttons.clear();
		buttons.push_back(CreateMenuButton(
			"Play", color::Blue, []() { game.scene.TransitionActive("main_menu", "level_select"); },
			color::Blue, color::Black
		));
		for (int i = 0; i < buttons.size(); i++) {
			buttons[i].SetRect({ V2_int{ first_button_coordinate.x,
										 first_button_coordinate.y +
											 (int)i * (button_size.y + button_y_offset) },
								 button_size, Origin::CenterTop });
		}
	}

	void Shutdown() final {
		for (auto& b : buttons) {
			b.Disable();
		}
	}

	void Update() final {
		game.texture.Get("menu_background").Draw();
		for (auto& b : buttons) {
			b.Draw();
		}
	}
};

class SetupScene : public Scene {
public:
	SetupScene() {}

	void Init() final {
		game.font.Load("menu_font", "resources/font/retro_gaming.ttf", button_size.y);
		game.texture.Load("menu_background", "resources/ui/background.png");
		game.music.Load("background_music", "resources/sound/background_music.ogg").Play(-1);

		game.scene.Load<MainMenu>("main_menu");
		game.scene.Load<LevelSelect>("level_select");

		game.scene.TransitionActive("setup_scene", "main_menu");
	}
};

int main([[maybe_unused]] int c, [[maybe_unused]] char** v) {
	game.Init("Barkin' Madness", resolution);
	game.scene.LoadActive<SetupScene>("setup_scene");
	return 0;
}
