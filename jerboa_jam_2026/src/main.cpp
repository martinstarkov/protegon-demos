
#include "app/application.h"
#include "core/event/dispatcher.h"
#include "core/math/vector2.h"
#include "runtime/asset/asset_manager.h"
#include "runtime/graphics/render_context.h"
#include "runtime/graphics/sprite.h"
#include "runtime/scene/scene.h"

using namespace ptgn;

constexpr V2_float game_size{ 320, 180 };

class GameScene : public Scene {
public:
	void OnEnter() override {
		ctx().asset.LoadDirectory("assets");
		ctx().renderer.SetGameSize(game_size);

		CreateSprite(*this, "bg");
	}

	void OnUpdate() override {}

	void OnExit() override {}

	void OnEvent(EventDispatcher d) override {}
};

int main(int, char**) {
	Application app{ "Go To Town", game_size * 2 };
	app.StartWith<GameScene>();
}