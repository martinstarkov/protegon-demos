#include "runtime/ui/button.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

#include "app/application.h"
#include "core/editor.h"
#include "core/event/event.h"
#include "core/graphics/color.h"
#include "core/graphics/fill_style.h"
#include "core/math/angle.h"
#include "core/math/geometry/origin.h"
#include "core/math/transform.h"
#include "core/math/vector2.h"
#include "core/util/timer.h"
#include "runtime/ecs/entity_hierarchy.h"
#include "runtime/graphics/shape.h"
#include "runtime/graphics/text/text.h"
#include "runtime/interaction/interactive.h"
#include "runtime/scene/scene.h"
#include "runtime/scene/scene_registry.h"
#include "runtime/scripting/builtin_scripts.h"
#include "runtime/scripting/script.h"
#include "runtime/scripting/script_registration.h"

using namespace ptgn;

namespace ptgn::event {

/// @brief Emitted after every interactive entity in a scene has been disabled.
struct SceneInteractablesDisabled {};
struct SceneInteractablesEnabled {};

} // namespace ptgn::event

namespace {

constexpr V2_float kButtonSize{
	300.0f,
	80.0f
};

constexpr float kTitleButtonOffset{
	170.0f
};

constexpr V2_float kClockPosition{
	0.0f,
	0.0f
};

constexpr V2_float kFirstHandEnd{
	0.0f,
	-100.0f
};

constexpr V2_float kSecondHandEnd{
	0.0f,
	-140.0f
};

constexpr V2_float kThirdHandEnd{
	0.0f,
	-180.0f
};

constexpr int kRequiredRotations{
	34
};

constexpr float kLoseDelaySeconds{
	25.0f
};

const SignalKey kActionCompletedSignal{
	"action.completed"
};

const SignalKey kWinSignal{
	"win"
};

const SignalKey kLoseSignal{
	"lose"
};

void EmitGlobalSignal(
	Scene& scene,
	const SignalKey& signal
) {
	(void)script_runtime::DispatchGlobal<Signal>(
		scene,
		Signal{
			signal
		}
	);
}

void SetAllSceneInteractablesEnabled(
	Scene& scene,
	bool enabled
) {
	for (auto [entity, interactive] :
		 scene.EntitiesWith<
			 impl::Interactive
		 >()) {
		SetInteractive(
			entity,
			enabled
		);
	}

	if (!enabled) {
		(void)script_runtime::DispatchGlobal<
			event::SceneInteractablesDisabled
		>(scene);
	} else {
		(void)script_runtime::DispatchGlobal<
			event::SceneInteractablesEnabled
		>(scene);
	}
}

struct ClockHand {
	Entity parent;
	Entity line;
};

ClockHand CreateClockHand(
	Scene& scene,
	V2_float clock_position,
	V2_float end,
	float line_width,
	Color color
) {
	Entity parent{
		scene.CreateEntity()
	};

	parent.Add<Transform>(
		Transform{
			clock_position
		}
	);

	Entity line{
		CreateLine(
			scene,
			Transform{},
			V2_float{
				0.0f,
				0.0f
			},
			end,
			color,
			line_width
		)
	};

	SetParent(
		line,
		parent
	);

	return {
		parent,
		line
	};
}

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
	std::string target_scene_key,
	SceneTransitionStyle transition,
	V2_float direction = {
		1.0f,
		0.0f
	}
) {
	SceneChangeScript change;

	change.action =
		SceneChangeAction::Switch;

	change.scene_key =
		std::move(target_scene_key);

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

struct SetSceneInteractablesEnabledScript : public Script {
	bool enabled{ true };

	SetSceneInteractablesEnabledScript() = default;

	explicit SetSceneInteractablesEnabledScript(
		bool enabled
	) :
		enabled{ enabled } {}

	void OnStart() override {
		SetAllSceneInteractablesEnabled(
			GetScene(),
			enabled
		);
	}

	PTGN_REFLECT(
		SetSceneInteractablesEnabledScript,
		enabled
	)
};

PTGN_REGISTER_SCRIPT(
	SetSceneInteractablesEnabledScript,
	{
		.completion = ScriptCompletion::Instant,
		.label =
			"Set Scene Interactables",
		.group =
			"Interaction",
		.description =
			"Enable or disable every interactive entity in the current scene.",
		.type =
			editor::ScriptType::Sequence,
	}
);

struct IncrementClockHandScript : public Script {
	int rotations{
		kRequiredRotations
	};

	SignalKey increment_signal{
		kActionCompletedSignal
	};

	SignalKey win_signal{
		kWinSignal
	};

	IncrementClockHandScript() = default;

	void OnStart() override {
		current_rotation_ = 0;
		won_ = false;

		SetRotation(Owner(), Degrees{ 0.0f });
	}

	void OnEvent(Event event) override {

		event.Dispatch<Signal>(
			[this](
				const Signal& signal
			) {
				if (won_ ||
					signal.key !=
						increment_signal) {
					return;
				}

				const int rotation_count{
					std::max(
						1,
						rotations
					)
				};

				if (current_rotation_ >=
					rotation_count) {
					return;
				}

				++current_rotation_;

				const float angle{
					360.0f *
					static_cast<float>(
						current_rotation_
					) /
					static_cast<float>(
						rotation_count
					)
				};

				SetRotation(Owner(), Degrees{
							angle
						});
				

				if (current_rotation_ !=
					rotation_count) {
					return;
				}

				won_ = true;

				EmitGlobalSignal(
					GetScene(),
					win_signal
				);
			}
		);
	}

	PTGN_REFLECT(
		IncrementClockHandScript,
		rotations,
		increment_signal,
		win_signal
	)

private:
	int current_rotation_{ 0 };
	bool won_{ false };
};

class TitleScene : public Scene {
public:
	void OnLoad() override {
		ctx().asset.LoadDirectory("assets");

		
	}

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

		ClockHand first_hand{
			CreateClockHand(
				*this,
				kClockPosition,
				kFirstHandEnd,
				2.0f,
				color::White
			)
		};

		(void)CreateClockHand(
			*this,
			kClockPosition,
			kSecondHandEnd,
			3.0f,
			color::White
		);

		(void)CreateClockHand(
			*this,
			kClockPosition,
			kThirdHandEnd,
			4.0f,
			color::White
		);


		SetBackgroundColor(
			Color{
				25,
				85,
				55,
				255
			}
		);
	}

	void OnEnter() override {
		lose_timer_.Start(true);
		game_finished_ = false;

		EmitGlobalSignal(
			*this,
			"start"
		);

		auto first_hand{ GetEntity("HandParent") };

		auto& increment_script{ AddScript<IncrementClockHandScript>(first_hand) };

		increment_script.rotations =
			kRequiredRotations;
	}

	void OnUpdate() override {
		
		if (game_finished_ ||
			!lose_timer_.IsRunning() || resetting) {
			lose_timer_.Reset();
			return;
		}

		lose_timer_.Update(ctx().dt());

		if (!lose_timer_.Completed(
				secondsf{
					kLoseDelaySeconds
				}
			)) {
			return;
		}

		game_finished_ = true;
		lose_timer_.Reset();
		lose_timer_.Stop();

		// EmitGlobalSignal(
		// 	*this,
		// 	kLoseSignal
		// );
	}

	bool resetting = false;

	void OnEvent(Event event) override {
		event.Dispatch<
			event::SceneInteractablesDisabled
		>(
			[this] {
				if (game_finished_) {
					return;
				}

				resetting = true;
				lose_timer_.Reset();
			}
		);
		event.Dispatch<
			event::SceneInteractablesEnabled
		>(
			[this] {
				if (game_finished_) {
					return;
				}

				resetting = false;
				lose_timer_.Start(true);
			}
		);

		event.Dispatch<Signal>(
			[this](
				const Signal& signal
			) {
				if (signal.key == kActionCompletedSignal) {
					lose_timer_.Start(true);
				} else if (signal.key == kWinSignal) {
					game_finished_ = true;
					lose_timer_.Reset();
					lose_timer_.Stop();
				}
			}
		);
	}

private:
	ManualTimer lose_timer_;
	bool game_finished_{ false };
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
				"Main",
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
		"Hardly Working"
	};

	PTGN_WITH_EDITOR(
		app,
		true
	);

	app.StartProject<TitleScene>(
		"../project/GMTKJam2026.ptgnproj"
	);
}
