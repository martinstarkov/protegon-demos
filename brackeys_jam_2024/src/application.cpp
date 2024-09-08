#include "protegon/protegon.h"

using namespace ptgn;

constexpr const V2_int resolution{ 960, 540 };
constexpr const V2_int center{ resolution / 2 };

struct Transform {
	V2_float position;
	float rotation{ 0.0f };
};

struct Size {
	V2_float size;
};

struct RigidBody {
	V2_float velocity;
	V2_float acceleration;
	V2_float max_velocity;
};

struct VehicleComponent {
	float forward_thrust{ 0.0f };
	float backward_thrust{ 0.0f };
	float turn_speed{ 0.0f };
};

class GameScene : public Scene {
public:
	ecs::Manager manager;

	ecs::Entity player;

	GameScene() {}

	void Init() final {
		CreatePlayer();
	}

	void Update(float dt) final {
		PlayerInput(dt);

		PlayerPhysics(dt);

		Draw();
	}

	void Draw() {
		DrawPlayer();
	}

	// Init functions.

	void CreatePlayer() {
		player = manager.CreateEntity();

		player.Add<Texture>(Texture{ "resources/entity/car.png" });

		auto& transform	   = player.Add<Transform>();
		transform.position = center;

		player.Add<Size>(V2_float{ 16, 30 });

		auto& rigid_body		= player.Add<RigidBody>();
		rigid_body.max_velocity = { 500.0f, 500.0f };

		auto& vehicle			= player.Add<VehicleComponent>();
		vehicle.forward_thrust	= 300.0f;
		vehicle.backward_thrust = 100.0f;
		vehicle.turn_speed		= 10.0f;
	}

	// Update functions.

	void PlayerInput(float dt) {
		PTGN_ASSERT(player.Has<RigidBody>());
		PTGN_ASSERT(player.Has<VehicleComponent>());
		PTGN_ASSERT(player.Has<Transform>());

		auto& rigid_body = player.Get<RigidBody>();
		auto& vehicle	 = player.Get<VehicleComponent>();
		auto& transform	 = player.Get<Transform>();

		V2_float unit_direction{ V2_float{ 1.0f, 0.0f }.Rotated(transform.rotation) };

		V2_float thrust;

		if (game.input.KeyDown(Key::W)) {
			thrust = unit_direction * vehicle.forward_thrust;
		} else if (game.input.KeyDown(Key::S)) {
			thrust = unit_direction * vehicle.backward_thrust;
		}

		if (game.input.KeyDown(Key::A)) {
			transform.rotation += vehicle.turn_speed * dt;
		}
		if (game.input.KeyDown(Key::D)) {
			transform.rotation -= vehicle.turn_speed * dt;
		}

		rigid_body.acceleration += thrust;
	}

	void PlayerPhysics(float dt) {
		PTGN_ASSERT(player.Has<RigidBody>());
		PTGN_ASSERT(player.Has<Transform>());

		auto& rigid_body = player.Get<RigidBody>();
		auto& transform	 = player.Get<Transform>();

		rigid_body.velocity += rigid_body.acceleration * dt;

		transform.position += rigid_body.velocity * dt;

		rigid_body.acceleration = {};
	}

	// Draw functions.

	void DrawPlayer() {
		PTGN_ASSERT(player.Has<Transform>());
		PTGN_ASSERT(player.Has<Texture>());
		PTGN_ASSERT(player.Has<Size>());

		auto& player_transform{ player.Get<Transform>() };

		game.renderer.DrawTexture(
			player.Get<Texture>(), player_transform.position, player.Get<Size>().size, {}, {},
			Origin::Center, Flip::None, player_transform.rotation
		);
	}
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
