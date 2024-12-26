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

	Texture t1{ "resources/ui/pin.png" };
	Texture t2{ "resources/ui/pin_hover.png" };
	Texture t3{ "resources/ui/pin_selected.png" };
	Texture t4{ "resources/ui/pin_selected_hover.png" };

    void Init() override {
		b1 = CreateButton({ 300, 500 });

		buttons.Load("a", CreateButton({ 900, 400 }));
		buttons.Load("b", CreateButton({ 900, 500 }));
		buttons.Load("c", CreateButton({ 900, 600 }));
		buttons.Load("d", CreateButton({ 1000, 600 }));
	}

    void Shutdown() override {}

	Text text{ "Goodbye", color::DarkGray };

	Button dragging;

	void Update() override {
		V2_int mouse_pos = game.input.GetMousePosition();
        mouse_pos.Draw(Color{ 0, 0, 255, 30 }, 2.0f);

		if (game.input.MouseDown(Mouse::Left)) {
			buttons.ForEachValue([=](Button& b) {
				auto rect = b.GetRect();
				if (rect.Overlaps(mouse_pos)) {
					dragging = b;
				}
			});
		} else if (game.input.MouseUp(Mouse::Left)) {
			dragging = {};
		}

		if (dragging != Button{}) {
			auto rect = dragging.GetRect();
			rect.position = mouse_pos;
			dragging.SetRect(rect);

			game.draw.Text("Dragging button", color::Red, Rect{ dragging.GetRect().position - V2_float{ 0, 30 }, {}, Origin::Center });

		}
		
		Line line1{ dragging.GetRect().position, mouse_pos };
		line1.Draw(color::Purple, 3.0f);


        auto c = camera.GetPrimary();
        PTGN_LOG("setup scene camera size: ", c.GetSize(), ", camera pos: ", c.GetPosition());

        Rect r{ { 0, 0 }, resolution / 2.0f, Origin::TopLeft };
        r.Draw(color::Cyan, -1.0f);
		game.draw.Text("Hello", color::Red, r);

		b1.Draw();

		buttons.Draw();

		text.Draw(Rect{ { 500, 500 }, {}, Origin::Center, DegToRad(30.0f) });
	}

	Button CreateButton(const V2_float& center) {
		Button b;
		b.SetRect(Rect{ center, { 50, 50 }, Origin::Center });
		b.Set<ButtonProperty::Toggleable>(true);
		b.Set<ButtonProperty::Texture>(t1);
		b.Set<ButtonProperty::Texture>(t2, ButtonState::Hover);
		b.Set<ButtonProperty::Texture>(t2, ButtonState::Pressed);
		b.Set<ButtonProperty::Texture>(t3, ButtonState::Default, true);
		b.Set<ButtonProperty::Texture>(t4, ButtonState::Hover, true);
		b.Set<ButtonProperty::Texture>(t4, ButtonState::Pressed, true);
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
