#include "protegon/protegon.h"

using namespace ptgn;

constexpr const V2_int resolution{ 1280, 720 };

class Simulink : public Scene {
public:
	Simulink() {
		game.window.SetTitle("Simulink");
		game.window.SetSize(resolution);
	}

	Button b1;

	ToggleButtonGroup buttons;

    void Init() override {
		b1 = CreateButton({ 300, 500 });

		buttons.Load("a", CreateButton({ 900, 400 }));
		buttons.Load("b", CreateButton({ 900, 500 }));
		buttons.Load("c", CreateButton({ 900, 600 }));
	}

    void Shutdown() override {}

	void Update() override {
		V2_int mouse_pos = game.input.GetMousePosition();
        mouse_pos.Draw(Color{ 0, 0, 255, 30 }, 2.0f);


        auto c = camera.GetPrimary();
        PTGN_LOG("setup scene camera size: ", c.GetSize(), ", camera pos: ", c.GetPosition());

        Rect r{ { 0, 0 }, resolution / 2.0f, Origin::TopLeft };
        r.Draw(color::Cyan, -1.0f);
		game.draw.Text("Hello", color::Red, r);

		b1.Draw();

		buttons.Draw();
	}

	Button CreateButton(const V2_float& center) {
		Button b;
		b.SetRect(Rect{ center, { 50, 50 }, Origin::Center });
		b.Set<ButtonProperty::Toggleable>(true);
		b.Set<ButtonProperty::BackgroundColor>(color::Red);
		b.Set<ButtonProperty::BackgroundColor>(color::Yellow, ButtonState::Hover);
		b.Set<ButtonProperty::BackgroundColor>(color::Silver, ButtonState::Pressed);
		b.Set<ButtonProperty::BackgroundColor>(color::Green, ButtonState::Default, true, false);
		b.Set<ButtonProperty::BackgroundColor>(color::Blue, ButtonState::Hover, true, false);
		b.Set<ButtonProperty::BackgroundColor>(color::Purple, ButtonState::Pressed, true, false);
		b.Set<ButtonProperty::OnActivate>([=]() {
			PTGN_LOG("Pressed button at: ", center);
		});
		return b;
	}
};

int main(int c, char** v) {
	game.Start<Simulink>();
	return 0;
}
