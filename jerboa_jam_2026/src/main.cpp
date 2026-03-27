
#include <optional>
#include <string>
#include <type_traits>

#include "app/application.h"
#include "core/event/dispatcher.h"
#include "core/log.h"
#include "core/math/geometry/origin.h"
#include "core/math/vector2.h"
#include "nlohmann/json.hpp"
#include "platform/window/window.h"
#include "runtime/asset/asset_manager.h"
#include "runtime/ecs/component.h"
#include "runtime/ecs/entity.h"
#include "runtime/graphics/draw.h"
#include "runtime/graphics/render_context.h"
#include "runtime/graphics/shape.h"
#include "runtime/graphics/sprite.h"
#include "runtime/scene/scene.h"
#include "runtime/scene/scene_input.h"
#include "runtime/scripting/scripts.h"
#include "runtime/ui/interactive.h"

using namespace ptgn;

constexpr V2_float game_size{ 320, 180 };

struct Location {
	std::string name;
};

class LocationScript : public Script {
public:
	void OnEvent(EventDispatcher d) override {
		d.Dispatch<MousePressedOver>([this](auto& e) {
			if (e.button != Mouse::Left) {
				return;
			}
			PTGN_LOG("Player clicked location: ", entity.Get<Location>().name);
		});
	}
};

class GameScene : public Scene {
public:
	Sprite cursor;

	void CreateLocation(
		const std::string& name, V2_float position, V2_float hitbox_position, V2_float hitbox_size
	) {
		auto location = CreateSprite(*this, name, position, Origin::TopLeft);
		location.Add<Location>(name);
		SetInteractiveRect(location, hitbox_position, hitbox_size, Origin::TopLeft, name);
		AddScript<LocationScript>(location);
	}

	void OnEnter() override {
		ctx().input.SetSettings({ .debug_draw_enabled = true });

		// ctx().window.SetOSCursorVisibility(false);
		//  ctx().window.SetCursor("assets/ui/cursor.png", { 0, 0 });
		ctx().asset.LoadDirectory("assets");
		ctx().renderer.SetGameSize(game_size);

		CreateSprite(*this, "bg");

		cursor = CreateSprite(*this, "cursor", ctx().input.GetMousePosition());
		SetDepth(cursor, 1000);

		std::reference_wrapper<json> data_ref = ctx().asset.GetJson("data").value();
		const auto& data					  = data_ref.get();

		for (const auto& location : data.at("locations")) {
			std::string name		  = location.at("name");
			V2_float position		  = location.at("position");
			position				 -= game_size / 2.0f;
			V2_float hitbox_position  = location.at("hitbox_position");
			V2_float hitbox_size	  = location.at("hitbox_size");
			CreateLocation(name, position, hitbox_position, hitbox_size);
		}
	}

	void OnUpdate() override {
		auto mouse_pos{ ctx().input.GetMousePosition() };
		// PTGN_LOG("Mouse pos: ", mouse_pos);
		SetPosition(cursor, mouse_pos);
	}

	void OnExit() override {
		ctx().window.SetOSCursorVisibility(true);
	}

	void OnEvent(EventDispatcher d) override {}
};

int main(int, char**) {
	Application app{ "Go To Town", game_size * 2 };
	app.StartWith<GameScene>();
}