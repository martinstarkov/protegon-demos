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

	Velocity(const V2_float& velocity, const V2_float& max) : current{ velocity }, max{ max } {}

	V2_float current;
	V2_float max;
};

struct Acceleration {
	Acceleration(const V2_float& current, const V2_float& max) : current{ current }, max{ max } {}

	V2_float current;
	V2_float max;
};

struct DirectionalTextureComponent {
	DirectionalTextureComponent(const Texture& front, const Texture& back, const Texture& side) :
		current{ front }, front{ front }, back{ back }, side{ side } {}

	Texture current;
	Texture front;
	Texture back;
	Texture side;
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

	ecs::Entity main_tl;
	ecs::Entity main_tr;
	ecs::Entity main_bl;
	ecs::Entity main_br;

	ecs::Entity player;

	GameScene() {}

	void CreatePlayer() {
		player = manager.CreateEntity();

		auto& ppos = player.Add<Position>(center);
		player.Add<Velocity>(V2_float{}, V2_float{ 1500.0f });
		player.Add<Acceleration>(V2_float{}, V2_float{ 3000.0f });
		player.Add<Origin>(Origin::Center);
		player.Add<Flip>(Flip::None);
		player.Add<Size>(V2_int{ tile_size.x, 2 * tile_size.y });
		player.Add<DirectionalTextureComponent>(
			Texture{ "resources/entity/player_front.png" },
			Texture{ "resources/entity/player_back.png" },
			Texture{ "resources/entity/player_side.png" }
		);
		player.Add<GridComponent>(ppos.p / grid_size);

		manager.Refresh();
	}

	void Init() final {
		CreatePlayer();

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

	void PlayerMovementInput(float dt) {
		auto& v = player.Get<Velocity>();
		auto& a = player.Get<Acceleration>();
		auto& f = player.Get<Flip>();
		auto& t = player.Get<DirectionalTextureComponent>();

		bool up{ game.input.KeyPressed(Key::W) };
		bool down{ game.input.KeyPressed(Key::S) };
		bool right{ game.input.KeyPressed(Key::D) };
		bool left{ game.input.KeyPressed(Key::A) };

		if (t.current != t.back) {
			t.current = t.front;
		}

		if (left) {
			a.current.x = -1;
			f			= Flip::Horizontal;
			t.current	= t.side;
		} else if (right) {
			a.current.x = 1;
			f			= Flip::None;
			t.current	= t.side;
		} else {
			a.current.x = 0;
			v.current.x = 0;
		}

		if (up) {
			a.current.y = -1;
			t.current	= t.back;
		} else if (down) {
			a.current.y = 1;
			t.current	= t.front;
		} else {
			a.current.y = 0;
			v.current.y = 0;
		}

		a.current = a.current.Normalized() * a.max;
	}

	void UpdatePhysics(float dt) {
		float drag{ 10.0f };

		for (auto [e, p, v, a] : manager.EntitiesWith<Position, Velocity, Acceleration>()) {
			v.current += a.current * dt;

			v.current.x = std::clamp(v.current.x, -v.max.x, v.max.x);
			v.current.y = std::clamp(v.current.y, -v.max.y, v.max.y);

			v.current.x -= drag * v.current.x * dt;
			v.current.y -= drag * v.current.y * dt;

			p.p += v.current * dt;
		}
	}

	void Update(float dt) final {
		PlayerMovementInput(dt);

		UpdatePhysics(dt);

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
			player.Get<Position>().p, player.Get<Size>().s,
			player.Get<DirectionalTextureComponent>().current, {}, {}, 0.0f, { 0.5f, 0.5f },
			player.Get<Flip>(), player.Get<Origin>()
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
