#include "components/input.h"
#include "components/sprite.h"
#include "core/entity.h"
#include "core/game.h"
#include "math/vector2.h"
#include "rendering/api/color.h"
#include "rendering/api/origin.h"
#include "rendering/graphics/circle.h"
#include "scene/scene.h"
#include "scene/scene_manager.h"
#include "ui/button.h"

using namespace ptgn;

constexpr V2_int resolution{ 1280, 720 };
constexpr Color window_color{ color::Transparent };
constexpr const char* window_title{ "You Are God" };

struct DiskDragScript : public Script<DiskDragScript> {
	void OnDrag(V2_float mouse) override {
		entity.GetPosition() = mouse + entity.Get<Draggable>().offset;
	}

	virtual void OnDrop([[maybe_unused]] Entity dropzone) {
		entity.GetPosition() = dropzone.GetAbsolutePosition();
	}
};

Entity CreateTablet(Scene& scene) {
	Entity entity = CreateSprite(scene, "tablet");
	entity.SetOrigin(Origin::Center);
	// entity.Hide();
	entity.SetPosition(resolution / 2.0f);
	return entity;
}

Entity CreateDisk(Scene& scene, const V2_float& position, const TextureHandle& texture_handle) {
	Sprite entity = CreateSprite(scene, texture_handle);
	entity.SetPosition(position);
	entity.Enable();
	entity.SetInteractive();
	// entity.Hide();
	entity.Add<Draggable>();
	entity.Add<InteractiveCircles>(entity.GetDisplaySize().x / 2.0f);
	entity.AddScript<DiskDragScript>();
	return entity;
}

Entity CreateDiskEntry(Scene& scene, const V2_float& position, Sprite tablet) {
	float radius{ game.texture.GetSize("baby").x / 2.0f };
	Entity entity = scene.CreateEntity(
	); // CreateCircle(scene, position, radius, color::Cyan, -1.0f); // scene.CreateEntity();
	entity.SetParent(tablet);
	entity.SetPosition(
		position - tablet.GetDisplaySize() * 0.5f +
		GetOriginOffset(tablet.GetOrigin(), tablet.GetDisplaySize())
	);
	entity.Enable();
	// entity.Hide();
	entity.SetInteractive();
	entity.Add<InteractiveCircles>(radius);
	entity.Add<Dropzone>().trigger = DropTrigger::MouseOverlaps;
	return entity;
}

class GameScene : public Scene {
public:
	Entity tablet;

	std::vector<Entity> disks;

	void Enter() override {
		input.SetDrawInteractives(true);
		input.SetTopOnly(true);

		tablet = CreateTablet(*this);

		CreateDiskEntry(*this, V2_float{ 121, 298 }, tablet);
		CreateDiskEntry(*this, V2_float{ 252, 121 }, tablet);
		CreateDiskEntry(*this, V2_float{ 395, 300 }, tablet);
		CreateDiskEntry(*this, V2_float{ 255, 479 }, tablet);

		disks.push_back(CreateDisk(*this, V2_float{ 300, 300 }, "baby"));
		disks.push_back(CreateDisk(*this, V2_float{ 400, 400 }, "young"));
		disks.push_back(CreateDisk(*this, V2_float{ 500, 500 }, "old"));
		disks.push_back(CreateDisk(*this, V2_float{ 600, 600 }, "dead"));
	}
};

class MainMenuScene : public Scene {
public:
	void Enter() override {
		LoadResources("resources/data/resources.json");

		CreateSprite(*this, "main_menu_bg").SetOrigin(Origin::TopLeft);
		CreateButton(*this)
			.SetText("Play", color::White)
			.SetFontSize(50)
			.SetBackgroundColor(color::Gray)
			.SetBackgroundColor(color::DarkGray, ButtonState::Hover)
			.SetBackgroundColor(color::Black, ButtonState::Pressed)
			.SetSize(V2_float{ 400, 200 })
			.OnActivate([]() { game.scene.Transition<GameScene>("main_menu", "game", {}); })
			.SetPosition(resolution / 2.0f + V2_float{ 0, 100 });
	}
};

int main() {
	game.Init(window_title, resolution, window_color);
	game.scene.Enter<MainMenuScene>("main_menu");
	return 0;
}