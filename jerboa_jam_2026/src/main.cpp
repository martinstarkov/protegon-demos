
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <deque>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "app/application.h"
#include "core/assert.h"
#include "core/event/dispatcher.h"
#include "core/math/angle.h"
#include "core/math/easing.h"
#include "core/math/geometry/arc.h"
#include "core/math/geometry/origin.h"
#include "core/math/math_utils.h"
#include "core/math/rng.h"
#include "core/math/vector2.h"
#include "core/time/time.h"
#include "core/time/timer.h"
#include "core/util/string.h"
#include "nlohmann/json.hpp"
#include "platform/input/events.h"
#include "platform/input/mouse.h"
#include "platform/window/window.h"
#include "renderer/primitives/color.h"
#include "renderer/primitives/gradient.h"
#include "runtime/animation/animation.h"
#include "runtime/animation/tween.h"
#include "runtime/animation/tween_effect.h"
#include "runtime/asset/asset_manager.h"
#include "runtime/asset/font_system.h"
#include "runtime/audio/audio_system.h"
#include "runtime/ecs/entity.h"
#include "runtime/ecs/entity_hierarchy.h"
#include "runtime/graphics/camera.h"
#include "runtime/graphics/draw.h"
#include "runtime/graphics/render_context.h"
#include "runtime/graphics/shape.h"
#include "runtime/graphics/sprite.h"
#include "runtime/graphics/text.h"
#include "runtime/scene/scene.h"
#include "runtime/scene/scene_input.h"
#include "runtime/scene/scene_transitions.h"
#include "runtime/scripting/script_sequence.h"
#include "runtime/ui/button.h"
#include "runtime/ui/interactive.h"
#include "serialization/json/fwd.h"

using namespace ptgn;

constexpr V2_float game_size{ 320, 180 };

struct Location {
	std::vector<std::string> entities;
};

static V2_float ArcPosition(V2_float start, V2_float end, float t) {
	V2_float pos = Lerp(start, end, t);

	float height_offset	 = std::sin(t * kPi);
	pos.y				+= -height_offset * (end - start).Magnitude() * 0.2f;

	return pos;
}

static std::string FormatDuration(milliseconds ms) {
	auto total_seconds = duration_cast<seconds>(ms);
	auto minute		   = duration_cast<minutes>(total_seconds);
	auto second		   = total_seconds - minute;
	return std::format("{}:{:02}", minute.count(), second.count());
}

static Button CreateMenuButton(
	Scene& scene, V2_float pos, V2_float size, std::string_view content
) {
	return CreateButton(
		scene, pos, size,
		{ .content			= std::string(content),
		  .text_color		= color::Black,
		  .text_color_hover = color::White,
		  .font_size		= 10,
		  .sound_hover		= "hover",
		  .sound_press		= "press" }
	);
}

class MainMenuScene : public Scene {
public:
	void OnEnter() override;
};

class InstructionScene : public Scene {
public:
	void OnEnter() override;
};

class ScoreScene : public Scene {
public:
	std::size_t score{ 0 };
	std::size_t highest_combo{ 0 };

	ScoreScene() = default;

	ScoreScene(std::size_t score, std::size_t highest_combo) :
		score{ score }, highest_combo{ highest_combo } {}

	void OnEnter() override;
};

class GameScene : public Scene {
public:
	Sprite cursor;

	std::vector<std::string> entities;

	std::deque<std::string> next_entity;

	Sprite preview_first;
	Sprite preview_second;

	Text remaining_text;

	Animation cannon;

	Text combo_text;
	Text score_text;
	Entity combo_arc;
	Sprite combo_meter;

	struct ArcTween {};

	Timer combo_decay_timer;

	std::size_t combo{ 0 };
	std::size_t score{ 0 };
	std::size_t highest_combo{ 0 };
	std::size_t standard_score{ 1 };

	static constexpr float combo_decay_exponential_constant = 0.1f; // Higher -> Faster decay.
	static constexpr milliseconds standard_combo_duration	= 3000ms;
	static constexpr Degrees arc_start_angle{ 248.0f };
	static constexpr Degrees arc_end_angle{ 156.0f };
	static constexpr float arc_radius{ 23.0f };

	static constexpr milliseconds level_duration{ 1min };
	static constexpr float standard_flight_distance		   = 280.0f;
	static constexpr milliseconds standard_flight_duration = 1000ms;

	void CreateLocation(
		const std::vector<std::string>& location_entities, V2_float hitbox_position,
		V2_float hitbox_size
	) {
		auto location = CreateEntity();
		location.Add<Location>(location_entities);
		SetInteractiveRect(
			location, hitbox_position - game_size / 2.0f, hitbox_size, Origin::TopLeft
		);
	}

	std::string RandomChoice() const {
		auto next_choice = RandomNumber(static_cast<std::size_t>(0), entities.size() - 1);
		PTGN_ASSERT(next_choice < entities.size());

		const auto& choice = entities[next_choice];

		if (choice == "robber") {
			return choice;
		}

		if (auto dei = RandomNumber(1, 3); dei == 1) {
			return choice + "2";
		}
		return choice + "1";
	}

	Entity FindLocationAt(V2_float pos) {
		for (auto [e, loc] : EntitiesWith<Location>()) {
			if (SceneInput::Overlap(pos, e)) {
				return e;
			}
		}
		return {};
	}

	Gradient combo_gradient{
		"linear-gradient(90deg,rgba(252, 223, 0, 1) 0%, rgba(255, 170, 0, 1) 22%, rgba(255, 43, "
		"43, 1) 39%, rgba(255, 0, 195, 1) 42%, rgba(191, 0, 255, 1) 65%, rgba(77, 54, 255, 1) 88%, "
		"rgba(255, 255, 255, 1) 100%);"
	};

	void ResetComboTimer() {
		combo_decay_timer.Start();
		if (combo == 0) {
			GetTween<ArcTween>(combo_arc).Clear();
			combo_arc.Get<Arc>().SetStartAngle(arc_start_angle);
			SetTint(combo_meter, color::White);
			SetTint(combo_arc, color::White);
			return;
		}
		auto tint{ combo_gradient.Sample(static_cast<float>(combo) / 25.0f) };
		SetTint(combo_meter, tint);
		SetTint(combo_arc, tint);
		auto combo_decay{ GetComboDecayDuration() };
		auto arc_tween = GetTween<ArcTween>(combo_arc);
		arc_tween.Clear();
		arc_tween.During(combo_decay)
			.OnProgress([](Entity e, float progress) {
				auto& arc_shape{ GetParent(e).Get<Arc>() };

				arc_shape.SetStartAngle(Lerp(arc_start_angle, arc_end_angle, progress));
			})
			.Start();
	}

	void IncrementScore() {
		score += standard_score * std::max(static_cast<std::size_t>(1), combo);
		score_text.SetContent(ToString(score));
	}

	void IncrementCombo() {
		combo++;
		highest_combo = std::max(highest_combo, combo);
		combo_text.SetContent(ToString(combo));
		ResetComboTimer();
	}

	void DecrementCombo() {
		ResetComboTimer();
		if (combo == 0) {
			return;
		}
		combo--;
		combo_text.SetContent(ToString(combo));
		if (combo == 0) {
			ResetComboTimer();
		}
	}

	void ResetCombo() {
		combo = 0;
		combo_text.SetContent(ToString(combo));
		ResetComboTimer();
	}

	bool IsVariant(std::string_view choice, const std::string& type) const {
		return choice == type || choice == type + "1" || choice == type + "2";
	}

	bool IsWoman(std::string_view choice) const {
		return IsVariant(choice, "teacher") || IsVariant(choice, "florist") ||
			   IsVariant(choice, "nurse") || IsVariant(choice, "mech");
	}

	void PlayCorrectSound(std::string_view choice, Entity location) {
		if (IsVariant(choice, "rat")) {
			ctx().audio.Play("rat", 0.9f, 0, RandomNumber(1.2f, 1.7f));
		} else if (IsVariant(choice, "banker")) {
			ctx().audio.Play("banker", 0.7f, 0, RandomNumber(0.95f, 1.2f));
		} else if (IsVariant(choice, "robber")) {
			if (std::ranges::contains(location.Get<Location>().entities, std::string("cop1")) ||
				std::ranges::contains(location.Get<Location>().entities, std::string("cop2"))) {
				ctx().audio.Play("man_ow", 0.3f, 0, RandomNumber(0.8f, 1.2f));
			} else {
				ctx().audio.Play("robber", 0.2f, 0, RandomNumber(0.8f, 1.2f));
			}
		} else if (IsVariant(choice, "firefighter")) {
			ctx().audio.Play("firefighter", 1.2f, 0, RandomNumber(0.9f, 1.2f));
		} else if (IsVariant(choice, "cop")) {
			if (FlipCoin()) {
				ctx().audio.Play("cop2", 0.8f, 0, RandomNumber(0.9f, 1.1f));
			} else {
				ctx().audio.Play("cop", 0.8f, 0, RandomNumber(0.9f, 1.1f));
			}
		} else if (IsVariant(choice, "teacher")) {
			ctx().audio.Play("teacher", 0.8f, 0, RandomNumber(0.9f, 1.2f));
		} else if (IsVariant(choice, "florist")) {
			ctx().audio.Play("florist", 0.4f, 0, RandomNumber(0.9f, 1.2f));
		} else if (IsVariant(choice, "nurse")) {
			ctx().audio.Play("nurse", 0.8f, 0, RandomNumber(0.98f, 1.15f));
		} else if (IsVariant(choice, "mayor")) {
			ctx().audio.Play("mayor", 1.1f, 0, RandomNumber(1.0f, 1.1f));
		} else if (IsVariant(choice, "dog")) {
			ctx().audio.Play("dog", 0.4f, 0, RandomNumber(0.9f, 1.2f));
		} else if (IsVariant(choice, "squirrel")) {
			ctx().audio.Play("squirrel", 0.7f, 0, RandomNumber(0.95f, 1.2f));
		} else if (IsWoman(choice)) {
			ctx().audio.Play("woman_yay", 0.3f, 0, RandomNumber(0.7f, 1.0f));
		} else {
			ctx().audio.Play("man_yay", 0.2f, 0, RandomNumber(0.8f, 1.0f));
		}
	}

	void PlayIncorrectSound(std::string_view choice) {
		if (IsVariant(choice, "rat")) {
			ctx().audio.Play("rat", 0.9f, 0, RandomNumber(0.2f, 0.3f));
		} else if (IsVariant(choice, "dog")) {
			ctx().audio.Play("dog_ow", 0.7f, 0, RandomNumber(1.0f, 1.2f));
		} else if (IsVariant(choice, "squirrel")) {
			ctx().audio.Play("squirrel_ow", 0.6f, 0, RandomNumber(1.0f, 1.2f));
		} else if (IsWoman(choice)) {
			ctx().audio.Play("woman_ugh", 0.7f, 0, RandomNumber(0.9f, 1.1f));
		} else {
			ctx().audio.Play("man_ugh", 0.7f, 0, RandomNumber(0.95f, 1.05f));
		}
	}

	void ShootEntity() {
		auto mouse_pos{ ctx().input.GetMousePosition() };

		cannon.Start();

		constexpr V2_float cannon_firing_point{ V2_float{ 283, 151 } - game_size / 2.0f };

		auto dist_to_cannon	 = cannon_firing_point - mouse_pos;
		float distance_ratio = dist_to_cannon.Magnitude() / standard_flight_distance;
		milliseconds flight_duration{
			static_cast<std::size_t>(standard_flight_duration.count() * distance_ratio)
		};
		PTGN_ASSERT(!next_entity.empty());
		std::string choice = next_entity.front();
		next_entity.pop_front();
		next_entity.push_back(RandomChoice());

		PTGN_ASSERT(next_entity.size() == 2);

		std::string next_choice		 = next_entity.front();
		std::string next_next_choice = next_entity.back();

		preview_first.SetTexture(next_choice);
		FadeIn(preview_first, 100ms, Ease::Linear, true, true);
		preview_second.SetTexture(next_next_choice);

		ctx().audio.Play("cannon", 0.2f, 0, RandomNumber(0.8f, 1.2f));

		ctx().audio.Play("fall", 0.01f, 0, RandomNumber(2.0f, 3.0f));

		auto entity = CreateSprite(*this, choice, cannon_firing_point);

		V2_float end{ mouse_pos };

		struct EntityArcPath {};

		auto direction{ RandomNumber(-1, 1) };
		auto angle{ Degrees::Random() };

		ScaleTo(entity, V2_float{ 1.1f }, flight_duration / 2)
			.OnComplete([flight_duration](auto e) {
				ScaleTo(GetParent(e), V2_float{ 0.3f }, flight_duration / 2);
			});

		GetTween<EntityArcPath>(entity)
			.During(flight_duration)
			.OnProgress([angle, direction, cannon_firing_point, end,
						 distance_ratio](Entity e, float t) {
				auto parent{ GetParent(e) };
				if (direction == 0) {
					SetRotation(parent, angle);
				} else {
					SetRotation(
						parent, static_cast<float>(direction) * t * 3.0f * distance_ratio *
									Degrees{ 360.0f }
					);
				}
				SetPosition(parent, ArcPosition(cannon_firing_point, end, t));
			})
			.OnComplete([this, choice](Entity e) {
				auto parent{ GetParent(e) };

				if (auto location = FindLocationAt(GetWorldPosition(parent)); location) {
					if (std::ranges::contains(location.Get<Location>().entities, choice)) {
						IncrementCombo();
						IncrementScore();
						PlayCorrectSound(choice, location);
					} else {
						PlayIncorrectSound(choice);
						ResetCombo();
					}
				} else {
					DecrementCombo();
				}

				FadeOut(parent, 200ms).OnComplete([](Entity e) { GetParent(e).Destroy(); });
			})
			.Start();
	}

	bool shooting{ true };

	void OnEnter() override {
		SetBackgroundColor({ 118, 164, 87, 255 });
		ctx().window.SetOSCursorVisibility(false);

		std::reference_wrapper<json> data_ref = ctx().asset.GetJson("data").value();
		const auto& data					  = data_ref.get();

		entities = data.at("entities").get<std::vector<std::string>>();

		next_entity.push_back(RandomChoice());
		next_entity.push_back(RandomChoice());

		CreateSprite(*this, "bg");

		CreateText(
			*this, V2_float{ 241, 163 } - game_size / 2.0f, "Welcome\nto\nTown",
			{ 225, 215, 5, 255 }, 4, {}, Origin::Center,
			TextProperties{ .justify = TextJustify::Center }
		);

		auto exit_button = CreateButton(
			*this, V2_float{ -game_size.x, game_size.y } / 2.0f, { 16, 16 },
			{ .texture		 = "exit_button",
			  .texture_hover = "exit_button_hover",
			  .texture_press = "exit_button_hover",
			  .sound_hover	 = "hover",
			  .sound_press	 = "press" },
			Origin::BottomLeft
		);
		exit_button.OnPress([](auto button) mutable {
			button.GetScene().ctx().scene.Switch<MainMenuScene>(
				"main_menu", FadeTransition{ 200ms }
			);
		});

		PTGN_ASSERT(next_entity.size() == 2);

		preview_first =
			CreateSprite(*this, next_entity.front(), V2_float{ 159, 158 } - game_size / 2.0f);
		preview_second =
			CreateSprite(*this, next_entity.back(), V2_float{ 118, 165 } - game_size / 2.0f);
		SetScale(preview_second, 0.5f);

		cursor = CreateSprite(*this, "cursor", ctx().input.GetMousePosition());
		SetUI(cursor);
		SetDepth(cursor, 1000);

		for (const auto& location : data.at("locations")) {
			PTGN_ASSERT(location.at("entities").is_array(), "Entities must be array");
			std::vector<std::string> location_entities =
				location.at("entities").get<std::vector<std::string>>();
			V2_float hitbox_position = location.at("hitbox_position");
			V2_float hitbox_size	 = location.at("hitbox_size");
			CreateLocation(location_entities, hitbox_position, hitbox_size);
		}

		CreateScriptSequence(*this)
			.Wait(1s)
			.During(
				level_duration,
				[this](Entity e) {
					auto elapsed{ Tween{ GetChild(e, "tween") }.GetProgress() };
					auto remaining_time_text = FormatDuration(
						level_duration -
						milliseconds{
							static_cast<std::size_t>(FastFloor(elapsed * level_duration.count())) }
					);
					remaining_text.SetContent(remaining_time_text);
				}
			)
			.Then([this]() {
				GetTween<ArcTween>(combo_arc).Stop();
				shooting = false;
				combo_decay_timer.Stop();
				ctx().scene.Switch<ScoreScene>(
					"score", FadeTransition{ 1000ms }, score, highest_combo
				);
			})
			.Start();

		remaining_text = CreateText(
			*this, V2_float{ 162, 12 } - game_size / 2.0f, FormatDuration(level_duration),
			color::White, 10, {}, Origin::Center
		);

		CreateText(
			*this, V2_float{ 234, 11 } - game_size / 2.0f, "Score:", color::White, 8, {},
			Origin::CenterLeft
		);
		score_text = CreateText(
			*this, V2_float{ 314, 11 } - game_size / 2.0f, "0", color::White, 8, {},
			Origin::CenterRight
		);

		V2_float combo_meter_pos{ V2_float{ 2, 2 } - game_size / 2.0f };
		auto arc_meter = CreateSprite(*this, "combo_meter_bg", combo_meter_pos, Origin::TopLeft);

		auto combo_meter_size{ *GetTextureSize(arc_meter) / 2.0f };
		V2_float combo_meter_center_offset{ 4, 4 };

		combo_arc = CreateArc(
			*this, combo_meter_size + combo_meter_center_offset - V2_float{ 1, 1 }, arc_radius,
			arc_start_angle, arc_end_angle, false, color::Red
		);
		SetParent(combo_arc, arc_meter);
		combo_meter = CreateSprite(*this, "combo_meter", combo_meter_pos, Origin::TopLeft);

		combo_text = CreateText(
			*this, combo_meter_size + combo_meter_center_offset + V2_float{ 1, 0 }, "0",
			color::Black, 14, {}, Origin::Center, TextProperties{ .justify = TextJustify::Right }
		);
		SetParent(combo_text, arc_meter);

		cannon = CreateAnimation(
			*this, "cannon", game_size / 2.0f, AnimationConfig{ 3, 100ms, V2_int{ 49, 39 }, 1 },
			Origin::BottomRight
		);
		SetDepth(cannon, 1);

		ResetComboTimer();
	}

	milliseconds GetComboDecayDuration() const {
		milliseconds combo_decay_duration{ static_cast<std::size_t>(
			standard_combo_duration.count() * 1.0f /
			std::exp(static_cast<float>(combo) * combo_decay_exponential_constant)
		) };
		return combo_decay_duration;
	}

	void OnUpdate() override {
		auto mouse_pos{ ctx().input.GetMousePosition() };
		SetPosition(cursor, mouse_pos);

		if (auto decay{ GetComboDecayDuration() }; combo_decay_timer.Completed(decay)) {
			DecrementCombo();
		}
	}

	void OnExit() override {
		ctx().window.SetOSCursorVisibility(true);
	}

	void OnEvent(EventDispatcher d) override {
		d.Dispatch<MousePressed>([this](const MousePressed& e) {
			if (e.button != Mouse::Left) {
				return;
			}
			if (shooting) {
				ShootEntity();
			}
		});
	}
};

void ScoreScene::OnEnter() {
	SetBackgroundColor({ 66, 66, 66, 255 });

	CreateText(*this, { 0, -60 }, "Thanks for playing!", color::Black, 14);
	CreateText(*this, { 0, -30 + 10 }, "Score: " + ToString(score), color::White, 14);
	CreateText(
		*this, { 0, -5 + 10 }, "Highest Combo: " + ToString(highest_combo), color::White, 14
	);

	auto back = CreateMenuButton(*this, { -40, 55 }, V2_float{ 50, 25 }, "Exit");
	back.OnPress([](auto button) mutable {
		if (button.GetScene().ctx().scene.Switch<MainMenuScene>(
				"main_menu", FadeTransition{ 200ms }
			)) {
			button.Disable();
		}
	});

	auto exit_button = CreateButton(
		*this, { 40, 55 }, { 45, 45 },
		{
			.texture	   = "replay",
			.texture_hover = "replay_hover",
			.texture_press = "replay_hover",
			.sound_hover   = "hover",
			.sound_press   = "press",
		}
	);
	SetScale(exit_button, 0.75f);
	exit_button.OnPress([](auto button) mutable {
		button.GetScene().ctx().scene.Switch<GameScene>("game", FadeTransition{ 500ms });
	});
}

void MainMenuScene::OnEnter() {
	ctx().asset.LoadDirectory("assets");
	ctx().font.SetDefault("Early GameBoy");
	ctx().audio.Play("music", 0.10f, -1, 1.0f, true, false);
	ctx().renderer.SetGameSize(game_size);

	CreateSprite(*this, "menu_bg");

	auto play = CreateMenuButton(*this, { -70, 55 }, V2_float{ 50, 25 }, "Play");
	play.OnPress([](auto button) mutable {
		if (button.GetScene().ctx().scene.Switch<GameScene>("game", FadeTransition{ 500ms })) {
			button.Disable();
		}
	});

	auto instructions = CreateMenuButton(*this, { 80, 55 }, V2_float{ 50, 25 }, "Help");
	instructions.OnPress([](auto button) mutable {
		if (button.GetScene().ctx().scene.Switch<InstructionScene>(
				"instructions", FadeTransition{ 200ms }
			)) {
			button.Disable();
		}
	});
}

void InstructionScene::OnEnter() {
	SetBackgroundColor({ 118, 164, 87, 255 });
	TextProperties properties;
	properties.wrap_after = static_cast<std::uint32_t>(game_size.x * 0.9f);
	properties.justify	  = TextJustify::Left;
	CreateText(
		*this, V2_float{ 0, -12 },
		"Citizens await their daily commute... though not quite in the usual way.\n\n"
		"Launch them to their rightful place: nurses to hospitals, rats to "
		"sewers, mechanics to the autoshop, you know the drill.\n\n"
		"Chain together perfect placements to build combos and rack up a skyhigh "
		"score.\n\n"
		"Workplace satisfaction has never been so explosive!",
		color::White, 6, "retro_gaming", Origin::Center, properties
	);
	auto back = CreateMenuButton(*this, { 0, 55 }, V2_float{ 50, 25 }, "Back");
	back.OnPress([](auto button) mutable {
		if (button.GetScene().ctx().scene.Switch<MainMenuScene>(
				"main_menu", FadeTransition{ 200ms }
			)) {
			button.Disable();
		}
	});
}

int main(int, char**) {
	Application app{ "Go To Town", game_size * 3 };
	app.StartWith<MainMenuScene>();
}