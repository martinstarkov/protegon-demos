#include "protegon/protegon.h"

using namespace ptgn;

struct Target {
	V2_float pos;

	Target() {}
};

class GameScene : public Scene {
public:
	ecs::Manager manager;
	ecs::Entity player;

	Texture bg_t{ "resources/ui/game_background.png" };
	Texture pin_t{ "resources/ui/pin.png" };
	Texture pin_hover_t{ "resources/ui/pin_hover.png" };
	Texture pin_selected_t{ "resources/ui/pin_selected.png" };
	Texture pin_selected_hover_t{ "resources/ui/pin_selected_hover.png" };

	ToggleButtonGroup pins;
	V2_float pin_offset{ 0, -16 };
	constexpr static float player_thrust{ 300.0f };

	GameScene() {}

	void Init() {
		manager.Clear();
		player = CreatePlayer({ 400, 300 });

		pins.Clear();

		pins.Load("New York", CreatePin({ 333, 239 }));
		pins.Load("Paris", CreatePin({ 604, 213 }));
		pins.Load("Helsinki", CreatePin({ 689, 143 }));
		pins.Load("London", CreatePin({ 591, 177 }));
	}

	void Update() override {
		UpdatePlayer();
		game.physics.Update(manager);

		Draw();
	}

	void Draw() {
		bg_t.Draw();
		pins.Draw();
		DrawPinLabels();
		player.Get<Animation>().Draw(player);
	}

	void DrawPinLabels() {
		auto f = std::function([&](std::string name, Button pin) {
			auto c = pin.GetRect().Center();
			Text label{ name, color::Gold };
			label.Draw({ c + pin_offset, label.GetSize(), Origin::Center });
		});
		pins.ForEachKeyValue(f);
	}

	void UpdatePlayer() {
		auto& target	= player.Get<Target>();
		auto& rb		= player.Get<RigidBody>();
		auto& transform = player.Get<Transform>();
		V2_float dir{ target.pos - transform.position };
		float threshold_dist{ 3.0f };

		if (target.pos.IsZero()) {
			return;
		}

		if (FastAbs(dir.x) <= threshold_dist && FastAbs(dir.y) <= threshold_dist) {
			// Reached target.
			target.pos	= {};
			rb.velocity = {};
		}

		transform.rotation = dir.Angle();

		transform.scale.x = FastAbs(transform.scale.x);
		if (dir.x < 0.0f) {
			transform.scale.x  *= -1.0f;
			transform.rotation += DegToRad(180.0f);
		}
		rb.AddAcceleration(dir.Normalized() * player_thrust);
	}

	ecs::Entity CreatePlayer(const V2_float& pos) {
		auto e = manager.CreateEntity();

		auto& transform = e.Add<Transform>(pos);
		transform.scale = V2_float{ 1.0f / 3.0f };
		Texture t{ "resources/entity/player.png" };
		e.Add<Animation>(t, 1, t.GetSize(), milliseconds{ 1000 });
		e.Add<Target>();
		auto& rb		= e.Add<RigidBody>();
		rb.max_velocity = 800.0f;
		rb.drag			= 3.0f;
		manager.Refresh();
		return e;
	}

	Button CreatePin(const V2_float& center) {
		Button b;
		b.SetRect(Rect{ center, pin_t.GetSize() / 2.0f, Origin::Center });
		b.Set<ButtonProperty::Toggleable>(true);
		b.Set<ButtonProperty::Texture>(pin_t);
		b.Set<ButtonProperty::Texture>(pin_hover_t, ButtonState::Hover);
		b.Set<ButtonProperty::Texture>(pin_hover_t, ButtonState::Pressed);
		b.Set<ButtonProperty::Texture>(pin_selected_t, ButtonState::Default, true, false);
		b.Set<ButtonProperty::Texture>(pin_selected_hover_t, ButtonState::Hover, true, false);
		b.Set<ButtonProperty::Texture>(pin_selected_hover_t, ButtonState::Pressed, true, false);
		b.Set<ButtonProperty::OnActivate>([=]() {
			PTGN_ASSERT(player.Has<Target>());
			auto& t{ player.Get<Target>() };
			t.pos = center;
		});
		return b;
	}
};

class LoadingScene : public Scene {
public:
	LoadingScene() {
		game.scene.Load<GameScene>("game");
	}

	void Init() {
		game.scene.TransitionActive(
			"loading", "game", SceneTransition{ TransitionType::Fade, milliseconds{ 1000 } }
		);
	}
};

class SetupScene : public Scene {
public:
	SetupScene() {
		game.window.SetSize({ 1280, 720 });
		game.window.SetTitle("Wanted: Santa");
		game.draw.SetClearColor(color::Black);
		game.font.SetDefault(Font{ "resources/font/hey_comic.ttf", 15 });
	}

	void Init() {
		game.scene.Load<LoadingScene>("loading");
		game.scene.AddActive("loading");
	}

	void Update() override {}
};

int main() {
	game.Start<SetupScene>();
	return 0;
}