#include "protegon/protegon.h"

using namespace ptgn;

class SetupScene : public Scene {
public:
	SetupScene() {
		game.window.SetSize({ 960, 540 });
		game.window.SetTitle("Game");
		game.draw.SetClearColor(color::Black);
	}

	void Update() override {}
};

int main() {
	game.Start<SetupScene>();
	return 0;
}