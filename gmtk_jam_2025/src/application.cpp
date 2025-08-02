#include "components/animation.h"
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
#include "serialization/json_manager.h"
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

Entity CreateTablet(Scene& scene, const TextureHandle& texture_handle) {
	Entity entity = CreateSprite(scene, texture_handle);
	entity.SetOrigin(Origin::Center);
	// entity.Hide();
	entity.SetPosition(resolution / 2.0f);
	return entity;
}

Entity CreateDisk(
	Scene& scene, const V2_float& position, const TextureHandle& texture_handle, int index
) {
	Animation entity = CreateAnimation(scene, texture_handle, 2);
	entity.SetPosition(position);
	entity.Enable();
	auto circle = entity.CreateChild();
	circle.Add<Circle>(entity.GetDisplaySize().x / 2.0f);
	entity.AddInteractable(circle);
	// entity.Hide();
	entity.Add<DiskIndex>(index);
	entity.Add<Draggable>();
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
	auto circle = entity.CreateChild();
	circle.Add<Circle>(radius);
	entity.AddInteractable(circle);
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

	void CreateLevel(const json& j) {
		PTGN_ASSERT(j.contains("tablet"));
		PTGN_ASSERT(j.at("tablet").is_string());

		tablet.Destroy();
		tablet = CreateTablet(*this, j.at("tablet"));

		for (Entity slot : disk_slots) {
			slot.Destroy();
		}
		disk_slots.clear();

		PTGN_ASSERT(j.contains("positions"));
		PTGN_ASSERT(j.at("positions").is_array());

		std::vector<V2_int> slots{ j.at("positions").get<std::vector<V2_int>>() };

		for (const auto& slot : slots) {
			disk_slots.push_back(CreateDiskSlot(*this, slot, tablet));
		}

		for (Entity disk : disks) {
			disk.Destroy();
		}
		disks.clear();

		PTGN_ASSERT(j.contains("disks"));

		const auto& json_disks{ j.at("disks") };

		PTGN_ASSERT(json_disks.is_array());

		std::array<V2_float, 8> positions{
			V2_float{ 300, 300 },
		};

		RandomPicker<V2_float> random_picker{ V2_float{ 200, 200 }, V2_float{ 300, 300 },
											  V2_float{ 400, 400 }, V2_float{ 500, 500 },
											  V2_float{ 600, 600 }, V2_float{ 800, 600 },
											  V2_float{ 900, 500 }, V2_float{ 1000, 400 } };

		PTGN_ASSERT(json_disks.size() < random_picker.Size());

		for (std::size_t i{ 0 }; i < json_disks.size(); ++i) {
			auto position{ random_picker.Next() };
			PTGN_ASSERT(position);
			PTGN_ASSERT(json_disks[i].is_string());
			TextureHandle handle;
			json_disks[i].get_to(handle);
			disks.push_back(CreateDisk(*this, *position, handle, static_cast<int>(i)));
		}
	}

	void Enter() override {
		input.SetDrawInteractives(true);
		input.SetTopOnly(true);

		auto levels{ game.json.Get("game_json") };
		PTGN_ASSERT(levels.contains("level"));
		auto level_name{ levels.at("level") };
		PTGN_ASSERT(level_name != "level");
		PTGN_ASSERT(levels.contains(level_name), "Level must be set to a valid level entry");
		auto level{ levels.at(level_name) };

		CreateLevel(level);

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