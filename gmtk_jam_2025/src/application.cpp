#include "components/generic.h"
#include "components/input.h"
#include "components/sprite.h"
#include "core/entity.h"
#include "core/game.h"
#include "math/geometry/circle.h"
#include "math/vector2.h"
#include "renderer/api/color.h"
#include "renderer/api/origin.h"
#include "scene/scene.h"
#include "scene/scene_manager.h"
#include "ui/button.h"

using namespace ptgn;

constexpr V2_int resolution{ 1280, 720 };
constexpr Color window_color{ color::Transparent };
constexpr const char* window_title{ "You Are God" };

struct DiskIndex : public ArithmeticComponent<int> {
	using ArithmeticComponent::ArithmeticComponent;
};

struct DiskDragScript : public Script<DiskDragScript> {
	void OnDrag(V2_float mouse) override {
		entity.GetPosition() = mouse + entity.Get<Draggable>().offset;
	}

	virtual void OnPickup([[maybe_unused]] Entity dropzone) {
		dropzone.Add<DiskIndex>(-1);
		PTGN_LOG("Setting dropzone ", dropzone.GetPosition(), " to -1");
	}

	virtual void OnDrop([[maybe_unused]] Entity dropzone) {
		if (dropzone.Get<Dropzone>().entities.empty()) {
			dropzone.Add<DiskIndex>(entity.Get<DiskIndex>());
			PTGN_LOG(
				"Setting dropzone ", dropzone.GetPosition(), " to ", dropzone.Get<DiskIndex>()
			);
			entity.GetPosition() = dropzone.GetAbsolutePosition();
		}
	}
};

Entity CreateTablet(Scene& scene) {
	Entity entity = CreateSprite(scene, "tablet");
	entity.SetOrigin(Origin::Center);
	// entity.Hide();
	entity.SetPosition(resolution / 2.0f);
	return entity;
}

Entity CreateDisk(
	Scene& scene, const V2_float& position, const TextureHandle& texture_handle, int index
) {
	Sprite entity = CreateSprite(scene, texture_handle);
	entity.SetPosition(position);
	entity.Enable();
	entity.SetInteractive();
	// entity.Hide();
	entity.Add<DiskIndex>(index);
	entity.Add<Draggable>();
	entity.Add<Circle>(entity.GetDisplaySize().x / 2.0f);
	entity.AddScript<DiskDragScript>();
	return entity;
}

Entity CreateDiskSlot(Scene& scene, const V2_float& position, Sprite tablet) {
	float radius{ game.texture.GetSize("baby").x / 2.0f };
	Entity entity = scene.CreateEntity(
	); // CreateCircle(scene, position, radius, color::Cyan, -1.0f); // scene.CreateEntity();
	entity.SetParent(tablet);
	entity.SetPosition(
		position - tablet.GetDisplaySize() * 0.5f +
		GetOriginOffset(tablet.GetOrigin(), tablet.GetDisplaySize())
	);
	entity.Add<DiskIndex>(-1);
	entity.Enable();
	// entity.Hide();
	entity.SetInteractive();
	entity.Add<Circle>(radius);
	entity.Add<Dropzone>().trigger = DropTrigger::MouseOverlaps;
	return entity;
}

class GameScene : public Scene {
public:
	Entity tablet;

	std::vector<Entity> disks;

	std::vector<Entity> disk_slots;

	bool CheckPattern(const std::vector<Entity>& items) {
		if (items.empty()) {
			return false;
		}

		auto it = std::find_if(items.begin(), items.end(), [](const Entity& item) {
			return item.Get<DiskIndex>().GetValue() == 0;
		});

		if (it == items.end()) {
			return false;
		}

		std::size_t start = std::distance(items.begin(), it);
		std::size_t n	  = items.size();

		for (std::size_t i = 0; i < n; ++i) {
			const Entity& current = items[(start + i) % n];
			if (current.Get<DiskIndex>().GetValue() != i) {
				return false;
			}
		}

		return true;
	}

	Button submit;

	void Enter() override {
		input.SetDrawInteractives(true);
		input.SetTopOnly(true);

		tablet = CreateTablet(*this);

		disk_slots.push_back(CreateDiskSlot(*this, V2_float{ 121, 298 }, tablet));
		disk_slots.push_back(CreateDiskSlot(*this, V2_float{ 252, 121 }, tablet));
		disk_slots.push_back(CreateDiskSlot(*this, V2_float{ 395, 300 }, tablet));
		disk_slots.push_back(CreateDiskSlot(*this, V2_float{ 255, 479 }, tablet));

		disks.push_back(CreateDisk(*this, V2_float{ 300, 300 }, "baby", 0));
		disks.push_back(CreateDisk(*this, V2_float{ 400, 400 }, "young", 1));
		disks.push_back(CreateDisk(*this, V2_float{ 500, 500 }, "old", 2));
		disks.push_back(CreateDisk(*this, V2_float{ 600, 600 }, "dead", 3));

		submit = CreateButton(*this)
					 .SetTextureKey("submit")
					 .SetButtonTint(color::Gray)
					 .SetButtonTint(color::DarkGray, ButtonState::Pressed)
					 .OnActivate([]() { PTGN_LOG("Submit"); })
					 .SetPosition(resolution / 2.0f + V2_float{ 450, 0 });
	}

	void Update() {
		if (CheckPattern(disk_slots)) {
			submit.SetButtonTint(color::Cyan);
		} else {
			submit.SetButtonTint(color::Gray);
		}
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