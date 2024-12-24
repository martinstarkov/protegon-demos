#include "protegon/protegon.h"

using namespace ptgn;

constexpr const V2_int resolution{ 960, 540 };

class GameScene : public Scene {
public:
    GameScene() {
    }
    
    void Init() override {
        OrthographicCamera c;
        c.SetPosition({ 0, 0 });
        c.SetSize(resolution);
        game.camera.SetPrimary(c);
    }
    
    void Shutdown() override {}
    
    void Update() override {
        if (game.input.KeyDown(Key::A)) {
            PTGN_LOG("Hello");
        }

        Draw();
    }
    
    void Draw() {
        game.draw.Rect(Rect{ { 50, 50 }, { 70, 70 }, Origin::Center }, color::Red);

        game.draw.Present();

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
		game.window.SetSize(resolution);
		game.window.SetTitle("Simulink");
		game.draw.SetClearColor(color::White);
        game.scene.Load<GameScene>("game");
        game.scene.AddActive("game");
	}

	void Update() override {}
};

int main() {
	game.Start<SetupScene>();
	return 0;
}
