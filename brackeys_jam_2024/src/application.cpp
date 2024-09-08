#include "protegon/protegon.h"

using namespace ptgn;

constexpr const V2_int resolution{ 960, 540 };
constexpr const V2_int center{ resolution / 2 };

class GameScene : public Scene {
public:
	GameScene() {}

	void Init() final {}

	void Update(float dt) final {}
};

struct TextButton {
	TextButton(const std::shared_ptr<Button>& button, const Text& text) :
		button{ button }, text{ text } {}

	std::shared_ptr<Button> button;
	Text text;
};

const int button_y_offset{ 14 };
const V2_int button_size{ 250, 50 };
const V2_int first_button_coordinate{ 250, 220 };

TextButton CreateMenuButton(
	const std::string& content, const Color& text_color, const ButtonActivateFunction& f,
	const Color& color, const Color& hover_color
) {
	ColorButton b;
	b.SetOnActivate(f);
	b.SetColor(color);
	b.SetHoverColor(hover_color);
	Text text{ Hash("menu_font"), content, color };
	return TextButton{ std::make_shared<ColorButton>(b), text };
}

class LevelSelect : public Scene {
public:
	std::vector<TextButton> buttons;

	LevelSelect() {}

	void StartGame() {
		game.scene.RemoveActive(Hash("level_select"));
		game.scene.Load<GameScene>(Hash("game"));
		game.scene.SetActive(Hash("game"));
	}

	OrthographicCamera camera;

	void Init() final {
		camera.SetSizeToWindow();
		camera.SetPosition(game.window.GetCenter());
		game.camera.SetPrimary(camera);

		buttons.clear();
		buttons.push_back(CreateMenuButton(
			"Easy", color::Blue, [&]() { StartGame(); }, color::Blue, color::Black
		));
		buttons.push_back(CreateMenuButton(
			"Medium", color::Green, [&]() { StartGame(); }, color::Gold, color::Black
		));
		buttons.push_back(CreateMenuButton(
			"Hard", color::Red, [&]() { StartGame(); }, color::Red, color::Black
		));
		buttons.push_back(CreateMenuButton(
			"Back", color::Black,
			[]() {
				game.scene.RemoveActive(Hash("level_select"));
				game.scene.SetActive(Hash("main_menu"));
			},
			color::LightGrey, color::Black
		));

		for (int i = 0; i < (int)buttons.size(); i++) {
			buttons[i].button->SetRectangle({ V2_int{ first_button_coordinate.x,
													  first_button_coordinate.y +
														  i * (button_size.y + button_y_offset) },
											  button_size, Origin::CenterTop });
			buttons[i].button->SubscribeToMouseEvents();
		}
	}

	void Shutdown() final {
		for (int i = 0; i < (int)buttons.size(); i++) {
			buttons[i].button->UnsubscribeFromMouseEvents();
		}
	}

	void Update() final {
		game.scene.Get(Hash("level_select"))->camera.SetPrimary(camera);

		game.renderer.DrawTexture(
			game.texture.Get(Hash("menu_background")), game.window.GetCenter(), resolution, {}, {},
			Origin::Center, Flip::None, 0.0f, {}, -1.0f
		);
		for (std::size_t i = 0; i < buttons.size(); i++) {
			buttons[i].button->DrawHollow(6.0f);
			auto rect = buttons[i].button->GetRectangle();
			rect.size.x =
				buttons[i]
					.text.GetSize(Hash("menu_font"), std::string(buttons[i].text.GetContent()))
					.x *
				0.5f;
			buttons[i].text.Draw(rect);
		}
	}
};

class MainMenu : public Scene {
public:
	std::vector<TextButton> buttons;

	MainMenu() {
		game.font.Load(Hash("menu_font"), "resources/font/retro_gaming.ttf", button_size.y);
		game.texture.Load(Hash("menu_background"), "resources/ui/background.png");

		// TODO: Readd.
		// game.music.Load(Hash("background_music"),
		// "resources/sound/background_music.ogg").Play(-1);

		game.scene.Load<LevelSelect>(Hash("level_select"));
	}

	void Init() final {
		buttons.clear();
		buttons.push_back(CreateMenuButton(
			"Play", color::Blue,
			[]() {
				game.scene.RemoveActive(Hash("main_menu"));
				game.scene.SetActive(Hash("level_select"));
			},
			color::Blue, color::Black
		));
		// buttons.push_back(CreateMenuButton(
		//	"Settings", color::Red,
		//	[]() {
		//		/*game.scene.RemoveActive(Hash("main_menu"));
		//		game.scene.SetActive(Hash("game"));*/
		//	},
		//	color::Red, color::Black
		//));

		for (int i = 0; i < (int)buttons.size(); i++) {
			buttons[i].button->SetRectangle({ V2_int{ first_button_coordinate.x,
													  first_button_coordinate.y +
														  i * (button_size.y + button_y_offset) },
											  button_size, Origin::CenterTop });
			buttons[i].button->SubscribeToMouseEvents();
		}
	}

	void Shutdown() final {
		for (int i = 0; i < (int)buttons.size(); i++) {
			buttons[i].button->UnsubscribeFromMouseEvents();
		}
	}

	void Update() final {
		game.renderer.DrawTexture(
			game.texture.Get(Hash("menu_background")), game.window.GetCenter(), resolution, {}, {},
			Origin::Center, Flip::None, 0.0f, {}, -1.0f
		);
		for (std::size_t i = 0; i < buttons.size(); i++) {
			buttons[i].button->DrawHollow(7.0f);
			auto rect = buttons[i].button->GetRectangle();
			rect.size.x =
				buttons[i]
					.text.GetSize(Hash("menu_font"), std::string(buttons[i].text.GetContent()))
					.x *
				0.5f;
			buttons[i].text.Draw(rect);
		}
		// TODO: Make this a texture and global (perhaps run in the start scene?).
		// Draw Mouse Cursor.
		// game.renderer.DrawCircleFilled(game.input.GetMousePosition(), 5.0f, color::Red);
	}
};

class SetupScene : public Scene {
public:
	SetupScene() {}

	void Init() final {
		game.renderer.SetClearColor(color::Silver);
		game.window.SetSize(resolution);

		std::size_t initial_scene{ Hash("main_menu") };
		game.scene.Load<MainMenu>(initial_scene);
		game.scene.SetActive(initial_scene);
	}
};

int main() {
	game.Start<SetupScene>();
	return 0;
}
