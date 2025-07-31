#include "protegon/protegon.h"

using namespace ptgn;

constexpr V2_int resolution{ 1280, 720 };
constexpr Color window_color{ color::Transparent };
constexpr const char* window_title{ "You Are God" };

class GameScene : public Scene {
public:
	void Enter() override {}

	void Update() override {}

	void Exit() override {}
};

int main() {
	game.Init(window_title, resolution, window_color);
	game.scene.Enter<GameScene>("game");
	return 0;
}