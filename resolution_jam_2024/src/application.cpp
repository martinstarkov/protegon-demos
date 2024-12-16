#include "protegon/protegon.h"

using namespace ptgn;

class GameScene : public Scene {
public:
	GameScene() {}
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