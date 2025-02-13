#include "protegon/protegon.h"

using namespace ptgn;

constexpr V2_int window_size{ 1280, 720 };
constexpr Color window_color{ color::Transparent };
constexpr const char* window_title{ "Sample Title" };

class SampleScene : public Scene {
public:
	Texture texture{ "resources/sample.png" };

	void Enter() override {
		PTGN_LOG("Entered sample scene");
	}

	void Update() override {
		texture.Draw({ game.window.GetCenter(), {}, Origin::Center });
	}

	void Exit() override {
		PTGN_LOG("Exited sample scene");
	}
};

int main() {
	game.Init(window_title, window_size, window_color);
	game.scene.Enter<SampleScene>("sample");
	return 0;
}