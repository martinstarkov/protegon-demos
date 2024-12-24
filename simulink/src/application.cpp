#include "protegon/protegon.h"

using namespace ptgn;

constexpr const V2_int resolution{ 500, 500 };

class GameScene : public Scene {
public:
    GameScene() {
    }
    
    void Init() override {
    }
    
    void Shutdown() override {}
    
    void Update() override {
        auto c = camera.GetPrimary();
        PTGN_LOG("setup scene camera size: ", c.GetSize(), ", camera pos: ", c.GetPosition());
    }

	// Button CreateButton(const V2_float& center) {
	// 	Button b;
	// 	b.SetRect(Rect{ center, { 50, 50 }, Origin::Center });
	// 	b.Set<ButtonProperty::Toggleable>(true);
	// 	b.Set<ButtonProperty::BackgroundColor>(color::Red);
	// 	b.Set<ButtonProperty::BackgroundColor>(color::Yellow, ButtonState::Hover);
	// 	b.Set<ButtonProperty::BackgroundColor>(color::Silver, ButtonState::Pressed);
	// 	b.Set<ButtonProperty::BackgroundColor>(color::Green, ButtonState::Default, true, false);
	// 	b.Set<ButtonProperty::BackgroundColor>(color::Blue, ButtonState::Hover, true, false);
	// 	b.Set<ButtonProperty::BackgroundColor>(color::Purple, ButtonState::Pressed, true, false);
	// 	b.Set<ButtonProperty::OnActivate>([=]() {
	// 		PTGN_LOG("Pressed button at: ", center);
	// 	});
	// 	return b;
	// }
};

class SetupScene : public Scene {
public:
	SetupScene() {
		game.window.SetTitle("Simulink");
		game.window.SetSize(resolution);
        game.draw.SetClearColor(color::Gray);
        game.scene.Load<GameScene>("game");
        game.scene.AddActive("game");
	}

    void Update() override {
		V2_int mouse_pos = game.input.GetMousePosition();
        mouse_pos.Draw(color::Blue, 30.0f);
        Rect r{ { 0, 0 }, resolution / 2.0f, Origin::TopLeft };
        r.Draw(color::Cyan, -1.0f);
		game.draw.Text("Hello", color::Red, r);
    }
};

int main(int c, char** v) {
	game.Start<SetupScene>();
	return 0;
}
