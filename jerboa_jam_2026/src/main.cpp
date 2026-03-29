
#include <chrono>
#include <cmath>
#include <deque>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include "app/application.h"
#include "core/assert.h"
#include "core/event/dispatcher.h"
#include "core/log.h"
#include "core/math/geometry/arc.h"
#include "core/math/geometry/origin.h"
#include "core/math/math_utils.h"
#include "core/math/rng.h"
#include "core/math/vector2.h"
#include "core/time/time.h"
#include "core/time/timer.h"
#include "core/util/span.h"
#include "core/util/string.h"
#include "nlohmann/json.hpp"
#include "platform/input/events.h"
#include "platform/input/mouse.h"
#include "platform/window/window.h"
#include "renderer/primitives/color.h"
#include "renderer/primitives/gradient.h"
#include "runtime/animation/tween.h"
#include "runtime/animation/tween_effect.h"
#include "runtime/asset/asset_manager.h"
#include "runtime/asset/font_system.h"
#include "runtime/ecs/component.h"
#include "runtime/ecs/entity.h"
#include "runtime/ecs/entity_hierarchy.h"
#include "runtime/graphics/draw.h"
#include "runtime/graphics/render_context.h"
#include "runtime/graphics/shape.h"
#include "runtime/graphics/sprite.h"
#include "runtime/graphics/text.h"
#include "runtime/physics/lifetime.h"
#include "runtime/physics/rigid_body.h"
#include "runtime/scene/scene.h"
#include "runtime/scene/scene_input.h"
#include "runtime/scene/scene_transitions.h"
#include "runtime/scripting/script_sequence.h"
#include "runtime/scripting/scripts.h"
#include "runtime/ui/button.h"
#include "runtime/ui/interactive.h"
#include "serialization/json/fwd.h"

using namespace ptgn;

constexpr V2_float game_size{ 320, 180 };

struct Location {
	std::vector<std::string> entities;
};

V2_float ArcPosition(V2_float start, V2_float end, float t) {
	V2_float pos = Lerp(start, end, t);

	float height_offset	 = std::sin(t * pi<float>);
	pos.y				+= -height_offset * (end - start).Magnitude() * 0.2f;

	return pos;
}

std::string FormatDuration(milliseconds ms) {
	auto total_seconds = duration_cast<seconds>(ms);
	auto minute		   = duration_cast<minutes>(total_seconds);
	auto second		   = total_seconds - minute;
	return std::format("{}:{:02}", minute.count(), second.count());
}

Button CreateMenuButton(Scene& scene, V2_float pos, V2_float size, std::string_view content) {
	return CreateButton(
		scene, pos, size,
		{ .content			  = std::string(content),
		  .text_color		  = color::Gray,
		  .text_color_hover	  = color::Gold,
		  .text_outline_width = 1,
		  .font_size		  = 10,
		  .sound_hover		  = "hover",
		  .sound_press		  = "press",
		  .scale			  = ScaleButtonConfig{} }
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

class GameScene : public Scene {
public:
	Sprite cursor;

	std::vector<std::string> entities;

	std::deque<std::string> next_entity;

	Sprite preview_first;
	Sprite preview_second;

	Text remaining_text;

	Text combo_text;
	Text score_text;
	Entity combo_arc;
	Sprite combo_meter;

	struct ArcTween {};

	Timer combo_decay_timer;

	std::size_t combo{ 0 };
	std::size_t score{ 8888 };
	std::size_t standard_score{ 1 };

	static constexpr float combo_decay_exponential_constant = 0.1f; // Higher -> Faster decay.
	static constexpr milliseconds standard_combo_duration	= 3000ms;
	static constexpr float arc_start_angle{ DegToRad(248.0f) };
	static constexpr float arc_end_angle{ DegToRad(156.0f) };
	static constexpr float arc_radius{ 23.0f };

	static constexpr milliseconds level_duration{ 2min };
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
		return entities[next_choice];
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
			combo_arc.Get<Arc>().start_angle = arc_start_angle;
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
			.OnProgress([this](Entity e, float progress) {
				auto& arc_shape{ GetParent(e).Get<Arc>() };

				arc_shape.start_angle = Lerp(arc_start_angle, arc_end_angle, progress);
			})
			.Start();
	}

	void IncrementScore() {
		score += standard_score * std::max(static_cast<std::size_t>(1), combo);
		score_text.SetContent(ToString(score));
	}

	void IncrementCombo() {
		combo++;
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

	void ShootEntity() {
		auto mouse_pos{ ctx().input.GetMousePosition() };

		constexpr V2_float cannon_firing_point{ V2_float{ 283, 151 } - game_size / 2.0f };

		auto dist_to_cannon	 = cannon_firing_point - mouse_pos;
		float distance_ratio = dist_to_cannon.Magnitude() / standard_flight_distance;
		milliseconds flight_duration{
			static_cast<std::size_t>(standard_flight_duration.count() * distance_ratio)
		};
		PTGN_ASSERT(next_entity.size() > 0);
		std::string choice = next_entity.front();
		next_entity.pop_front();
		next_entity.push_back(RandomChoice());

		PTGN_ASSERT(next_entity.size() == 2);

		std::string next_choice		 = next_entity.front();
		std::string next_next_choice = next_entity.back();

		preview_first.SetTexture(next_choice);
		FadeIn(preview_first, 100ms, Ease::Linear, true, true);
		preview_second.SetTexture(next_next_choice);

		ctx().audio.Play("cannon", 0.3f, 0, RandomNumber(0.8f, 1.2f));

		// if (choice == "rat") {
		//	ctx().audio.Play("rat", 0.3f, 0, RandomNumber(0.5f, 1.2f));
		// } else {
		//
		// }
		ctx().audio.Play("fall", 0.01f, 0, RandomNumber(2.0f, 3.0f));

		auto entity = CreateSprite(*this, choice, cannon_firing_point);

		V2_float end{ mouse_pos };

		struct EntityArcPath {};

		auto direction{ RandomNumber(-1, 1) };
		auto angle{ DegToRad(RandomNumber(0.0f, 360.0f)) };

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
						parent,
						static_cast<float>(direction) * t * 3.0f * distance_ratio * DegToRad(360.0f)
					);
				}
				SetPosition(parent, ArcPosition(cannon_firing_point, end, t));
			})
			.OnComplete([this, choice](Entity e) {
				auto parent{ GetParent(e) };

				const auto is_woman = [&choice]() {
					return choice == "teacher" || choice == "florist" || choice == "nurse" ||
						   choice == "mech";
				};

				if (auto location = FindLocationAt(GetWorldPosition(parent)); location) {
					if (VectorContains(location.Get<Location>().entities, choice)) {
						IncrementCombo();
						IncrementScore();
						if (choice == "rat") {
							ctx().audio.Play("rat", 0.7f, 0, RandomNumber(1.2f, 1.7f));
						} else if (choice == "banker") {
							ctx().audio.Play("banker", 0.7f, 0, RandomNumber(0.95f, 1.2f));
						} else if (choice == "robber") {
							if (VectorContains(
									location.Get<Location>().entities, std::string("cop")
								)) {
								ctx().audio.Play("man_ow", 0.3f, 0, RandomNumber(0.8f, 1.2f));
							} else {
								ctx().audio.Play("robber", 0.2f, 0, RandomNumber(0.8f, 1.2f));
							}
						} else if (choice == "firefighter") {
							ctx().audio.Play("firefighter", 1.2f, 0, RandomNumber(0.9f, 1.2f));
						} else if (choice == "cop") {
							ctx().audio.Play("cop", 0.7f, 0, RandomNumber(0.9f, 1.1f));
						} else if (choice == "teacher") {
							ctx().audio.Play("teacher", 0.8f, 0, RandomNumber(0.9f, 1.2f));
						} else if (choice == "nurse") {
							ctx().audio.Play("nurse", 0.8f, 0, RandomNumber(0.98f, 1.15f));
						} else if (is_woman()) {
							ctx().audio.Play("woman_yay", 0.3f, 0, RandomNumber(0.7f, 1.0f));
						} else {
							ctx().audio.Play("man_yay", 0.3f, 0, RandomNumber(0.8f, 1.2f));
						}
					} else {
						if (choice == "rat") {
							ctx().audio.Play("rat", 0.7f, 0, RandomNumber(0.2f, 0.3f));
						} else if (is_woman()) {
							ctx().audio.Play("woman_ugh", 0.7f, 0, RandomNumber(0.9f, 1.1f));
						} else {
							ctx().audio.Play("man_ugh", 0.7f, 0, RandomNumber(0.95f, 1.05f));
						}
						ResetCombo();
					}
				} else {
					DecrementCombo();
				}

				FadeOut(parent, 200ms).OnComplete([](Entity e) { GetParent(e).Destroy(); });
			})
			.Start();
	}

	void OnEnter() override {
		SetBackgroundColor({ 118, 164, 87, 255 });
		ctx().window.SetOSCursorVisibility(false);
		// ctx().input.SetSettings({ .debug_draw_enabled = true });

		std::reference_wrapper<json> data_ref = ctx().asset.GetJson("data").value();
		const auto& data					  = data_ref.get();

		entities = data.at("entities").get<std::vector<std::string>>();

		next_entity.push_back(RandomChoice());
		next_entity.push_back(RandomChoice());

		CreateSprite(*this, "bg");

		auto exit_button = CreateButton(
			*this, V2_float{ -game_size.x, game_size.y } / 2.0f, { 16, 16 },
			{ .texture = "exit_button", .sound_hover = "hover", .sound_press = "press" }
		);
		exit_button.OnPress([exit_button]() mutable {
			exit_button.GetScene().ctx().scene.Switch<MainMenuScene>(
				"main_menu", FadeTransition{ 200ms }
			);
		});
		SetDrawOrigin(exit_button, Origin::BottomLeft);

		PTGN_ASSERT(next_entity.size() == 2);

		preview_first =
			CreateSprite(*this, next_entity.front(), V2_float{ 159, 158 } - game_size / 2.0f);
		preview_second =
			CreateSprite(*this, next_entity.back(), V2_float{ 118, 165 } - game_size / 2.0f);
		SetScale(preview_second, 0.5f);

		cursor = CreateSprite(*this, "cursor", ctx().input.GetMousePosition());
		SetDepth(cursor, 1000);

		//	PTGN_LOG("Entities: ", entities);

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
			.Then([]() { /* PTGN_LOG("You lost");*/ })
			.Start();

		remaining_text = CreateText(*this, FormatDuration(level_duration), color::White, 10);
		SetPosition(remaining_text, V2_float{ 162, 12 } - game_size / 2.0f);

		auto score_label = CreateText(*this, "Score:", color::White, 8, {});
		score_text		 = CreateText(*this, "0", color::White, 8, {});
		SetPosition(score_label, V2_float{ 234, 11 } - game_size / 2.0f);
		SetDrawOrigin(score_label, Origin::CenterLeft);
		SetPosition(score_text, V2_float{ 314, 11 } - game_size / 2.0f);
		SetDrawOrigin(score_text, Origin::CenterRight);

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
			*this, "0", color::Black, 14, {}, TextProperties{ .justify = TextJustify::Right }
		);
		SetParent(combo_text, arc_meter);
		SetPosition(combo_text, combo_meter_size + combo_meter_center_offset + V2_float{ 1, 0 });

		auto cannon = CreateSprite(*this, "cannon", game_size / 2.0f, Origin::BottomRight);
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
		// PTGN_LOG("Mouse pos: ", mouse_pos);
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
			ShootEntity();
		});
	}
};

void MainMenuScene::OnEnter() {
	//	ctx().input.SetSettings({ .debug_draw_enabled = true });
	ctx().asset.LoadDirectory("assets");
	ctx().font.SetDefault("Early GameBoy");
	ctx().renderer.SetGameSize(game_size);

	CreateSprite(*this, "menu_bg");

	auto play = CreateMenuButton(*this, { -70, 55 }, V2_float{ 50, 25 }, "Play");
	play.OnPress([play]() mutable {
		play.Disable();
		play.GetScene().ctx().scene.Switch<GameScene>("game", FadeTransition{ 500ms });
	});

	auto instructions = CreateMenuButton(*this, { 70, 55 }, V2_float{ 100, 25 }, "Instructions");
	instructions.OnPress([instructions]() mutable {
		instructions.Disable();
		instructions.GetScene().ctx().scene.Switch<InstructionScene>(
			"instructions", FadeTransition{ 200ms }
		);
	});
}

void InstructionScene::OnEnter() {
	// ctx().input.SetSettings({ .debug_draw_enabled = true });
	CreateSprite(*this, "instructions_bg");
	TextProperties properties;
	properties.wrap_after = static_cast<std::uint32_t>(game_size.x * 0.9f);
	properties.justify	  = TextJustify::Left;
	auto t1				  = CreateText(
		  *this,
		  "Citizens await their daily commute... though not quite in the usual way.\n\n"
					  "They're late for work, and you're in charge of getting them there!\n\n"
					  "Launch them to their rightful place: rats to the "
					  "sewers, nurses to hospitals, mechanics to the autoshop, you know the drill.\n\n"
					  "Chain together perfect placements to build combos and rack up a skyhigh score.\n\n"
					  "Workplace satisfaction has never been so explosive!",
		  color::White, 6, "retro_gaming", properties
	  );
	SetPosition(t1, V2_float{ 0, -12 });
	auto back = CreateMenuButton(*this, { 0, 55 }, V2_float{ 50, 25 }, "Back");
	back.OnPress([back]() mutable {
		back.Disable();
		back.GetScene().ctx().scene.Switch<MainMenuScene>("main_menu", FadeTransition{ 200ms });
	});
}

int main(int, char**) {
	Application app{ "Go To Town", game_size * 3 };
	app.StartWith<MainMenuScene>();
}