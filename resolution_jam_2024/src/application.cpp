#include "protegon/protegon.h"

using namespace ptgn;

class GameScene : public Scene {
public:
	Texture bg_t{ "resources/ui/game_background.png" };
	Texture pin_t{ "resources/ui/pin.png" };
	Texture pin_hover_t{ "resources/ui/pin_hover.png" };
	Texture pin_selected_t{ "resources/ui/pin_selected.png" };
	Texture pin_selected_hover_t{ "resources/ui/pin_selected_hover.png" };

	ToggleButtonGroup pins;

	GameScene() {
		pins.Load("Berlin", CreatePin({ 300, 300 }));
		pins.Load("Prague", CreatePin({ 500, 300 }));
		pins.Load("Moscow", CreatePin({ 700, 300 }));
	}

	void Update() {
		bg_t.Draw();
		pins.Draw();
	}

	Button CreatePin(const V2_float& center) const {
		Button b;
		b.SetRect(Rect{ center, pin_t.GetSize(), Origin::Center });
		// b.Set<ButtonProperty::OnActivate>(activate);
		b.Set<ButtonProperty::Toggleable>(true);
		b.Set<ButtonProperty::Texture>(pin_t);
		b.Set<ButtonProperty::Texture>(pin_hover_t, ButtonState::Hover);
		b.Set<ButtonProperty::Texture>(pin_hover_t, ButtonState::Pressed);
		b.Set<ButtonProperty::Texture>(pin_selected_t, ButtonState::Default, true, false);
		b.Set<ButtonProperty::Texture>(pin_selected_hover_t, ButtonState::Hover, true, false);
		b.Set<ButtonProperty::Texture>(pin_selected_hover_t, ButtonState::Pressed, true, false);
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