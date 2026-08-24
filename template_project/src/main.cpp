#include <string>
#include <string_view>
#include <utility>

#include "app/application.h"
#include "app/editor.h"
#include "core/graphics/color.h"
#include "core/math/geometry/origin.h"
#include "core/math/transform.h"
#include "runtime/animation/animation.h"
#include "runtime/animation/animation_event.h"
#include "runtime/asset/asset_manager.h"
#include "runtime/graphics/shape.h"
#include "runtime/graphics/custom_shader.h"
#include "runtime/graphics/visible.h"
#include "runtime/scene/scene.h"
#include "runtime/scene/scene_context.h"
#include "runtime/scene/scene_registry.h"
#include "runtime/scripting/builtin_scripts.h"
#include "runtime/scripting/script.h"
#include "runtime/ui/button.h"

using namespace ptgn;

namespace {

void LoadAssets(Scene& scene) {
	//scene.ctx().asset.Load("animation", "assets/textures/animation_frames4.png");
}

} // namespace

class TestScene : public Scene {
public:
	void OnNew() override {
		LoadAssets(*this);

		Animation animation{ CreateAnimation(
			*this, {}, "animation", { 1, 500ms, V2_int{ 16, 32 }, 1, { 0, 32 } },
			Origin::Center
		) };

		SetScale(animation, 4.0f);
	}

	void OnLoad() override {
		LoadAssets(*this);
	}
};

PTGN_REGISTER_SCENE(TestScene);

int main(int, char**) {
	Application app{ "Template Project" };

	PTGN_WITH_EDITOR(app, true);

	app.StartProject<TestScene>("template_project.ptgnproj");
}