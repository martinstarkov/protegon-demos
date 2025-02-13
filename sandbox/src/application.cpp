#include "protegon/protegon.h"

using namespace ptgn;

class GameScene : public Scene {
public:
	GameScene() {}

	void Init() override {}

	void Update() override {
		game.draw.Line({ 960, 0 }, { 0, 540 }, color::Red);
		game.input.GetMousePosition().Draw(Color{ 0, 0, 255, 140 }, 10.0f);
	}
};

class SetupScene : public Scene {
public:
	SetupScene() {
		game.window.SetSize({ 960, 540 });
		game.window.SetTitle("Game");
		// TODO: The reason this does not work is because the draw target is changed by the initial scene.
		//game.draw.SetClearColor(color::Red);
	}

	void Init() override {
		game.draw.SetClearColor(color::Orange);
		game.scene.Load<GameScene>("game");
		game.scene.AddActive("game");
	}

	void Update() override {
		game.draw.Line({ 0, 0 }, { 960, 540 }, color::Cyan);
		game.draw.Text("Hello", color::Gray, Rect{ { 960 / 2, 540 / 2 }, {}, Origin::Center });
		//PTGN_LOG("SetupScene (0, 0): ", (GetRenderTarget().GetFrameBuffer().GetPixel({ 0, 0 })));
		//PTGN_LOG("SetupScene (540, 0): ", (GetRenderTarget().GetFrameBuffer().GetPixel({ 540, 0 })));
		//GetRenderTarget().GetFrameBuffer().ForEachPixel([](V2_int pos, Color c) { PTGN_LOG("SetupScene ", pos, ": ", c); });
	}
};

int main() {
	game.Start<SetupScene>();
	return 0;
}