#include "protegon/protegon.h"

using namespace ptgn;

constexpr const V2_int resolution{ 960, 540 };
constexpr const V2_int center{ resolution / 2 };

struct Position {
	Position(const V2_float& pos) : p{ pos } {}

	V2_float p;
};

struct Size {
	Size(const V2_float& size) : s{ size } {}

	V2_float s;
};

struct Velocity {
	Velocity() = default;

	Velocity(const V2_float& velocity) : v{ velocity } {}

	V2_float v;
};

struct TextureComponent {
	TextureComponent(const Texture& t) : t{ t } {}

	Texture t;
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

	ecs::Entity player;

	GameScene() {
		player	   = manager.CreateEntity();
		auto& ppos = player.Add<Position>(center);
		player.Add<Velocity>();
		player.Add<Origin>(Origin::Center);
		player.Add<Flip>(Flip::None);
		player.Add<Size>(V2_int{ tile_size.x, 2 * tile_size.y });
		player.Add<TextureComponent>(Texture{ "resources/entity/player_front.png" });
		player.Add<GridComponent>(ppos.p / grid_size);

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

		game.renderer.DrawTexture(
			player.Get<Position>().p, player.Get<Size>().s, player.Get<TextureComponent>().t, {},
			{}, 0.0f, { 0.5f, 0.5f }, player.Get<Flip>(), player.Get<Origin>()
		);
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

		std::size_t initial_scene{ Hash("game") };
		game.scene.Load<GameScene>(initial_scene);
		game.scene.SetActive(initial_scene);
	}
};

int main() {
	game.Start<SetupScene>();
	return 0;
}
