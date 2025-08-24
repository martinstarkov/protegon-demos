#include "core/game.h"
#include "protegon/protegon.h"

using namespace ptgn;

constexpr V2_int resolution{ 320, 180 };

class GameScene : public Scene {
public:
	void Enter() override {
		SetBackgroundColor(color::LightBlue);
		game.window.SetSetting(WindowSetting::Maximized);

		PTGN_LOG("Entered sample scene");

		CreateRect(*this, { 0, 0 }, { 30, 30 }, color::Red, -1.0f, Origin::TopLeft);
		CreateRect(*this, { resolution.x, 0 }, { 30, 30 }, color::Red, -1.0f, Origin::TopRight);
		CreateRect(*this, resolution, { 30, 30 }, color::Red, -1.0f, Origin::BottomRight);
		CreateRect(*this, { 0, resolution.y }, { 30, 30 }, color::Red, -1.0f, Origin::BottomLeft);
	}

	void Update() override {
		PTGN_LOG("Updating sample scene");
	}

	void Exit() override {
		PTGN_LOG("Exited sample scene");
	}
};

int main() {
	game.Init("Zombie Game", resolution);
	game.scene.Enter<GameScene>("game");
	return 0;
}