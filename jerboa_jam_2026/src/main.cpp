
#include <format>
#include <optional>
#include <string>
#include <type_traits>

#include "app/application.h"
#include "core/event/dispatcher.h"
#include "core/log.h"
#include "core/math/geometry/origin.h"
#include "core/math/rng.h"
#include "core/math/vector2.h"
#include "core/time/time.h"
#include "core/util/span.h"
#include "nlohmann/json.hpp"
#include "platform/input/events.h"
#include "platform/window/window.h"
#include "runtime/animation/tween_effect.h"
#include "runtime/asset/asset_manager.h"
#include "runtime/ecs/component.h"
#include "runtime/ecs/entity.h"
#include "runtime/graphics/draw.h"
#include "runtime/graphics/render_context.h"
#include "runtime/graphics/shape.h"
#include "runtime/graphics/sprite.h"
#include "runtime/physics/lifetime.h"
#include "runtime/physics/rigid_body.h"
#include "runtime/scene/scene.h"
#include "runtime/scene/scene_input.h"
#include "runtime/scripting/script_sequence.h"
#include "runtime/scripting/scripts.h"
#include "runtime/ui/interactive.h"

using namespace ptgn;

constexpr V2_float game_size{ 320, 180 };

struct Location {
	std::string name;
	std::vector<std::string> entities;
};

class LocationScript : public Script {
public:
	void OnEvent(EventDispatcher d) override {
		d.Dispatch<MousePressedOver>([this](auto& e) {
			if (e.button != Mouse::Left) {
				return;
			}
			auto& location{ entity.Get<Location>() };
			PTGN_LOG(
				"Player clicked location: ", location.name,
				", correct entities: ", location.entities
			);
		});
	}
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

class GameScene : public Scene {
public:
	Sprite cursor;

	std::vector<std::string> entities;

	std::deque<std::string> next_entity;

	Sprite preview_first;
	Sprite preview_second;

	Text remaining_text;

	static constexpr milliseconds level_duration{ 10s };
	static constexpr V2_float cannon_firing_point{ V2_float{ 283, 151 } - game_size / 2.0f };
	static constexpr V2_float preview_first_pos{ V2_float{ 157, 157 } - game_size / 2.0f };
	static constexpr V2_float preview_second_pos{ V2_float{ 117, 157 } - game_size / 2.0f };
	static constexpr float standard_distance		= 280.0f;
	static constexpr milliseconds standard_duration = 1000ms;

	void CreateLocation(
		const std::string& name, const std::vector<std::string>& location_entities,
		V2_float position, V2_float hitbox_position, V2_float hitbox_size
	) {
		auto location = CreateSprite(*this, name, position, Origin::TopLeft);
		location.Add<Location>(name, location_entities);
		SetInteractiveRect(location, hitbox_position, hitbox_size, Origin::TopLeft, name);
		AddScript<LocationScript>(location);
	}

	std::string RandomChoice() const {
		auto next_choice = RandomNumber(0ULL, entities.size() - 1);
		PTGN_ASSERT(next_choice < entities.size());
		return entities[next_choice];
	}

	void ShootEntity() {
		auto mouse_pos{ ctx().input.GetMousePosition() };
		auto dist_to_cannon = cannon_firing_point - mouse_pos;
		// PTGN_LOG("Mouse pos: ", mouse_pos, ", cannon: ", cannon_firing_point);
		// PTGN_LOG("dist_to_cannon: ", dist_to_cannon);
		// PTGN_LOG("dist_to_cannon.Magnitude(): ", dist_to_cannon.Magnitude());
		// PTGN_LOG("distance_ratio: ", distance_ratio);
		float distance_ratio = dist_to_cannon.Magnitude() / standard_distance;
		milliseconds flight_duration{
			static_cast<std::size_t>(standard_duration.count() * distance_ratio)
		};
		std::string choice = next_entity.front();
		next_entity.pop_front();
		next_entity.push_back(RandomChoice());

		PTGN_ASSERT(next_entity.size() == 2);

		std::string next_choice		 = next_entity.front();
		std::string next_next_choice = next_entity.back();

		preview_first.SetTexture(next_choice);
		preview_second.SetTexture(next_next_choice);

		V2_float start{ cannon_firing_point };
		auto entity = CreateSprite(*this, choice, start);

		V2_float end{ mouse_pos };

		struct EntityArcPath {};

		auto direction{ RandomNumber(-1, 1) };
		auto angle{ DegToRad(RandomNumber(0.0f, 360.0f)) };

		GetTween<EntityArcPath>(entity)
			.During(flight_duration)
			.OnProgress([angle, direction, start, end, distance_ratio](Entity e, float t) {
				auto parent{ GetParent(e) };
				if (direction == 0) {
					SetRotation(parent, angle);
				} else {
					SetRotation(parent, direction * t * 3.0f * distance_ratio * DegToRad(360.0f));
				}
				SetPosition(parent, ArcPosition(start, end, t));
			})
			.OnComplete([this](Entity e) {
				auto parent{ GetParent(e) };
				FadeOut(parent, 200ms).OnComplete([](Entity e) { GetParent(e).Destroy(); });
			})
			.Start();
	}

	void OnEnter() override {
		// ctx().window.SetOSCursorVisibility(false);
		ctx().asset.LoadDirectory("assets");
		ctx().renderer.SetGameSize(game_size);
		ctx().input.SetSettings({ .debug_draw_enabled = true });

		std::reference_wrapper<json> data_ref = ctx().asset.GetJson("data").value();
		const auto& data					  = data_ref.get();

		entities = data.at("entities").get<std::vector<std::string>>();

		next_entity.push_back(RandomChoice());
		next_entity.push_back(RandomChoice());

		CreateSprite(*this, "bg");

		PTGN_ASSERT(next_entity.size() == 2);

		preview_first  = CreateSprite(*this, next_entity.front(), preview_first_pos);
		preview_second = CreateSprite(*this, next_entity.back(), preview_second_pos);
		SetScale(preview_second, 0.5f);

		cursor = CreateSprite(*this, "cursor", ctx().input.GetMousePosition());
		SetDepth(cursor, 1000);

		PTGN_LOG("Entities: ", entities);

		for (const auto& location : data.at("locations")) {
			std::string name = location.at("name");
			PTGN_ASSERT(location.at("entities").is_array(), "Entities must be array");
			std::vector<std::string> location_entities =
				location.at("entities").get<std::vector<std::string>>();
			V2_float position		  = location.at("position");
			position				 -= game_size / 2.0f;
			V2_float hitbox_position  = location.at("hitbox_position");
			V2_float hitbox_size	  = location.at("hitbox_size");
			CreateLocation(name, location_entities, position, hitbox_position, hitbox_size);
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
			.Then([]() { PTGN_LOG("You lost"); })
			.Start();

		remaining_text = CreateText(*this, FormatDuration(level_duration), color::Black, 24);
		SetPosition(remaining_text, V2_float{ 156, 12 } - game_size / 2.0f);
	}

	void OnUpdate() override {
		auto mouse_pos{ ctx().input.GetMousePosition() };
		// PTGN_LOG("Mouse pos: ", mouse_pos);
		SetPosition(cursor, mouse_pos);
	}

	void OnExit() override {
		ctx().window.SetOSCursorVisibility(true);
	}

	void OnEvent(EventDispatcher d) override {
		d.Dispatch<MousePressed>([this](MousePressed& e) {
			if (e.button != Mouse::Left) {
				return;
			}
			ShootEntity();
		});
	}
};

int main(int, char**) {
	Application app{ "Go To Town", game_size * 2 };
	app.StartWith<GameScene>();
}