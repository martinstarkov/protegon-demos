#include "protegon/protegon.h"

using namespace ptgn;

constexpr const V2_int resolution{ 960, 540 };

struct RectangleComponent {
	RectangleComponent(const V2_int& pos, const V2_int& size) : r{ pos, size } {}

	Rectangle<float> r;
};

struct VelocityComponent {
	VelocityComponent(const V2_float& velocity) : v{ velocity } {}

	V2_float v;
};

struct GridComponent {
	GridComponent(const V2_int& cell) : cell{ cell } {}

	V2_int cell;
};

class GameScene : public Scene {
public:
	ecs::Manager manager;

	V2_int tile_size{ 16, 16 };
	V2_int grid_size{ 60, 34 };

	ecs::Entity main_tl;
	ecs::Entity main_tr;
	ecs::Entity main_bl;
	ecs::Entity main_br;

	GameScene() {
		main_tl = manager.CreateEntity();
		main_tr = manager.CreateEntity();
		main_bl = manager.CreateEntity();
		main_br = manager.CreateEntity();

		main_tl.Add<GridComponent>(V2_int{ 0, 0 });
		main_tr.Add<GridComponent>(V2_int{ grid_size.x - 1, 0 });
		main_bl.Add<GridComponent>(V2_int{ 0, grid_size.y - 1 });
		main_br.Add<GridComponent>(V2_int{ grid_size.x - 1, grid_size.y - 1 });

		manager.Refresh();
	}

	void Update(float dt) final {
		V2_int ws = game.window.GetSize();

		V2_float size{ tile_size };

		Rectangle<float> r1{ main_tl.Get<GridComponent>().cell * tile_size, size, Origin::TopLeft };
		Rectangle<float> r2{ main_tr.Get<GridComponent>().cell * tile_size, size, Origin::TopLeft };
		Rectangle<float> r3{ main_bl.Get<GridComponent>().cell * tile_size, size, Origin::TopLeft };
		Rectangle<float> r4{ main_br.Get<GridComponent>().cell * tile_size, size, Origin::TopLeft };

		game.renderer.DrawRectangleFilled(r1, color::Red);
		game.renderer.DrawRectangleFilled(r2, color::Green);
		game.renderer.DrawRectangleFilled(r3, color::Blue);
		game.renderer.DrawRectangleFilled(r4, color::Yellow);
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

		game.scene.Load<MainMenu>(Hash("main_menu"));
		game.scene.SetActive(Hash("main_menu"));
	}
};

int main() {
	game.Start<SetupScene>();
	return 0;
}
