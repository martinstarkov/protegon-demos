#include "runtime/ui/button.h"

#include <string>
#include <string_view>
#include <utility>

#include "app/application.h"
#include "core/editor.h"
#include "core/graphics/color.h"
#include "core/math/geometry/origin.h"
#include "core/math/transform.h"
#include "core/math/vector2.h"
#include "runtime/graphics/text/text.h"
#include "runtime/scene/scene.h"
#include "runtime/scene/scene_registry.h"
#include "runtime/scripting/builtin_scripts.h"
#include "runtime/scripting/script.h"

using namespace ptgn;

namespace {

constexpr V2_float kButtonSize{
	300.0f,
	80.0f
};

constexpr float kTitleButtonOffset{
	170.0f
};

Button CreateMenuButton(
	Scene& scene,
	V2_float position,
	std::string_view label
) {
	Transform transform;
	transform.position = position;

	Button button{
		CreateButton(
			scene,
			transform,
			kButtonSize,
			Origin::Center
		)
	};

	button.Background();

	button
		.Text()
		.Content(label)
		.Color(color::Black)
		.Size(28.0f)
		.Align(
			HorizontalAlign::Center,
			VerticalAlign::Center
		);

	return button;
}

ScriptSequence MakeSceneChangeSequence(
	std::string name,
	std::string target_scene_type,
	SceneTransitionStyle transition,
	V2_float direction = {
		1.0f,
		0.0f
	}
) {
	SceneChangeScript change;

	change.action =
		SceneChangeAction::Switch;

	// Reuse the current scene tag so the active scene is replaced.
	change.scene_tag.clear();

	change.scene_type =
		std::move(target_scene_type);

	change.scene_parameters =
		json::object();

	change.transition =
		transition;

	change.duration_ms =
		700.0f;

	change.delay_ms =
		0.0f;

	change.ease =
		Ease::InOutQuad;

	change.direction =
		direction;

	ScriptSequence sequence{
		std::move(name)
	};

	sequence
		.StartOn<event::ButtonPress>()
		.Then(std::move(change));

	return sequence;
}

void AttachSequence(
	Entity owner,
	ScriptSequence sequence
) {
	auto& script{
		AddScript<Script>(owner)
	};

	script.sequence =
		std::move(sequence);
}

} // namespace

class TitleScene : public Scene {
public:
	void OnNew() override {
		SetBackgroundColor(
			Color{
				20,
				30,
				55,
				255
			}
		);

		auto title{
			CreateText(
				*this,
				{
					0.0f,
					-180.0f
				}
			)
		};

		title
			.Content("Game Demo")
			.Color(color::White)
			.Size(64.0f)
			.Bold();

		Button play_button{
			CreateMenuButton(
				*this,
				{
					-kTitleButtonOffset,
					60.0f
				},
				"Play"
			)
		};

		AttachSequence(
			play_button,
			MakeSceneChangeSequence(
				"Play Game",
				"GameScene",
				SceneTransitionStyle::Fade
			)
		);

		Button instructions_button{
			CreateMenuButton(
				*this,
				{
					kTitleButtonOffset,
					60.0f
				},
				"Instructions"
			)
		};

		AttachSequence(
			instructions_button,
			MakeSceneChangeSequence(
				"Open Instructions",
				"InstructionsScene",
				SceneTransitionStyle::Slide,
				{
					1.0f,
					0.0f
				}
			)
		);
	}
};

class GameScene : public Scene {
public:
	void OnNew() override {
		SetBackgroundColor(
			Color{
				25,
				85,
				55,
				255
			}
		);
	}
};

class InstructionsScene : public Scene {
public:
	void OnNew() override {
		SetBackgroundColor(
			Color{
				45,
				35,
				65,
				255
			}
		);

		auto title{
			CreateText(
				*this,
				{
					0.0f,
					-260.0f
				}
			)
		};

		title
			.Content("Instructions")
			.Color(color::White)
			.Size(52.0f)
			.Bold();

		auto instructions{
			CreateText(
				*this,
				{
					0.0f,
					-150.0f
				}
			)
		};

		instructions
			.Content(
				"Welcome to our game!\n\n"
				"Additional gameplay instructions will be added as the game develops.\n"
				"Bye!"
			)
			.Color(color::White)
			.Size(24.0f)
			.Box(
				{
					{
						-380.0f,
						0.0f
					},
					{
						380.0f,
						280.0f
					}
				}
			)
			.Align(
				HorizontalAlign::Center,
				VerticalAlign::Top
			)
			.Wrap(WrapMode::Word)
			.LineSpacing(0.2f);

		Button back_button{
			CreateMenuButton(
				*this,
				{
					0.0f,
					250.0f
				},
				"Back"
			)
		};

		AttachSequence(
			back_button,
			MakeSceneChangeSequence(
				"Back To Title",
				"TitleScene",
				SceneTransitionStyle::Slide,
				{
					-1.0f,
					0.0f
				}
			)
		);
	}
};

PTGN_REGISTER_SCENE(
	TitleScene,
	"Title Scene"
);

PTGN_REGISTER_SCENE(
	GameScene,
	"Game Scene"
);

PTGN_REGISTER_SCENE(
	InstructionsScene,
	"Instructions Scene"
);

int main(int, char**) {
	Application app{
		"4;59 PM"
	};

	PTGN_WITH_EDITOR(
		app,
		true
	);

	app.StartProject<TitleScene>(
		"GMTKJam2026/"
		"GMTKJam2026.ptgnproj"
	);
}