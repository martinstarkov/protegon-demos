#include "runtime/graphics/sprite.h"

#include "app/application.h"
#include "core/editor.h"
#include "runtime/asset/asset_manager.h"
#include "runtime/scene/scene.h"
#include "runtime/scene/scene_registry.h"
#include "runtime/scene/scene_context.h"

using namespace ptgn;

class FirstScene : public Scene {
	void OnNew() override {
		ctx().asset.Load({ { "squirrel", "assets/entity/squirrel.png" } });

		CreateSprite(*this, {}, "squirrel");
	}
};

PTGN_REGISTER_SCENE(FirstScene, "First Scene");

int main(int, char**) {
	Application app{ "Clocking Out" };
	PTGN_WITH_EDITOR(app, true);
	app.StartProject<FirstScene>(
		"../project/FirstScene.ptgnproj"
	);
}