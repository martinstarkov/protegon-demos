#include "components/animation.h"
#include "components/draw.h"
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
constexpr V2_int center{ resolution / 2 };
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
		if (dropzone.Get<Dropzone>().dropped_entities.empty()) {
			dropzone.Add<DiskIndex>(-1);
			entity.GetChild("disk_out").Show();
			// PTGN_LOG("Setting dropzone ", dropzone.GetPosition(), " to -1");
		}
	}

	virtual void OnDrop([[maybe_unused]] Entity dropzone) {
		if (dropzone.Get<Dropzone>().dropped_entities.empty()) {
			dropzone.Add<DiskIndex>(entity.Get<DiskIndex>());
			entity.GetChild("disk_out").Hide();
			/*PTGN_LOG(
				"Setting dropzone ", dropzone.GetPosition(), " to ", dropzone.Get<DiskIndex>()
			);*/
			entity.GetPosition() = dropzone.GetAbsolutePosition();
		}
	}
};

Entity CreateTablet(Scene& scene, const TextureHandle& texture_handle) {
	Entity entity = CreateSprite(scene, texture_handle);
	entity.SetOrigin(Origin::Center);
	// entity.Hide();
	entity.SetPosition(center);
	return entity;
}

Entity CreateDisk(
	Scene& scene, const V2_float& position, const TextureHandle& texture_handle, int index
) {
	auto out		 = CreateSprite(scene, "disk_out");
	Animation entity = CreateSprite(scene, texture_handle);
	entity.AddChild(out, "disk_out");
	entity.SetPosition(position);
	entity.Enable();
	auto circle = entity.CreateChild();
	circle.Add<Circle>(
		entity.GetDisplaySize().x / 2.0f
	); // CreateCircle(scene, {}, entity.GetDisplaySize().x / 2.0f, color::Magenta, 1.0f);
	entity.AddInteractable(circle);
	// entity.Hide();
	entity.Add<DiskIndex>(index);
	entity.Add<Draggable>();
	entity.AddScript<DiskDragScript>();
	return entity;
}

Entity CreateDiskSlot(Scene& scene, const V2_float& position, Sprite tablet) {
	// Size of the interactable.
	float radius{ game.texture.GetSize("disk_out").x / 2.0f };
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
	circle.Add<Circle>(radius); // CreateCircle(scene, {}, radius, color::Magenta, 1.0f);
	entity.AddInteractable(circle);
	entity.Add<Dropzone>().trigger = DropTrigger::MouseOverlaps;
	return entity;
}

class GameScene : public Scene {
public:
	Entity tablet;

	std::vector<Entity> disks;

	std::vector<Entity> disk_slots;

	// @return True if all the slots are not -1.
	bool AllFilled(const std::vector<Entity>& items) {
		auto it = std::find_if(items.begin(), items.end(), [](const Entity& item) {
			return item.Get<DiskIndex>().GetValue() == -1;
		});
		return it == items.end();
	}

	// @return True if all the slots are consecutive disk indices.
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

	void FlyInTablet(const json& j) {
		tablet = CreateTablet(*this, j.at("tablet"));
	}

	void CreateLevel(const json& j) {
		PTGN_ASSERT(j.contains("tablet"));
		PTGN_ASSERT(j.at("tablet").is_string());

		tablet.Destroy();
		FlyInTablet(j);

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
		input.SetTopOnly(false);

		auto levels{ game.json.Get("game_json") };
		PTGN_ASSERT(levels.contains("level"));
		auto level_name{ levels.at("level") };
		PTGN_ASSERT(level_name != "level");
		PTGN_ASSERT(levels.contains(level_name), "Level must be set to a valid level entry");
		auto level{ levels.at(level_name) };

		CreateSprite(*this, "game_bg").SetOrigin(Origin::TopLeft);

		CreateLevel(level);

		Entity submit_interactable = CreateEntity().SetPosition({ 1114, 152 }
		); /*CreateCircle(*this, { 1114, 152 }, 56.0f, color::Magenta, 1.0f)*/
		submit_interactable.Add<Circle>(56.0f);

		submit = CreateButton(*this)
					 .SetTextureKey("submit")
					 .SetButtonTint(color::White)
					 .SetButtonTint(color::Gray, ButtonState::Hover)
					 .SetButtonTint(color::Gray, ButtonState::Pressed)
					 .OnActivate([]() { PTGN_LOG("Submit"); })
					 .SetInteractable(submit_interactable, false)
					 .SetOrigin(Origin::TopLeft)
					 .SetPosition({ 988, 69 });
	}

	void Update() {
		if (CheckPattern(disk_slots)) {
			submit.SetButtonTint(color::Cyan);
		} else if (AllFilled(disk_slots)) {
			submit.SetButtonTint(color::Red);
		} else {
			submit.SetButtonTint(color::White);
		}
	}
};

class InstructionScene : public Scene {
public:
	void Enter() override;
};

class MainMenuScene : public Scene {
public:
	void Enter() override {
		CreateSprite(*this, "main_menu_bg").SetOrigin(Origin::TopLeft);
		CreateButton(*this)
			.SetText("Play", color::White)
			.SetFontSize(48)
			.SetBackgroundColor(color::Gray)
			.SetBackgroundColor(color::DarkGray, ButtonState::Hover)
			.SetBackgroundColor(color::Black, ButtonState::Pressed)
			.SetSize(V2_float{ 500, 150 })
			.OnActivate([]() { game.scene.Transition<GameScene>("main_menu", "game", {}); })
			.SetPosition(center + V2_float{ -300, 100 });

		CreateButton(*this)
			.SetText("Instructions", color::White)
			.SetFontSize(48)
			.SetBackgroundColor(color::Gray)
			.SetBackgroundColor(color::DarkGray, ButtonState::Hover)
			.SetBackgroundColor(color::Black, ButtonState::Pressed)
			.SetSize(V2_float{ 500, 150 })
			.OnActivate([]() {
				game.scene.Transition<InstructionScene>("main_menu", "instruction", {});
			})
			.SetPosition(center + V2_float{ 300, 100 });
	}
};

void InstructionScene::Enter() {
	CreateSprite(*this, "main_menu_bg").SetOrigin(Origin::TopLeft);
	TextProperties properties;
	properties.wrap_after = static_cast<std::uint32_t>(resolution.x * 0.9f);
	properties.justify	  = TextJustify::Center;
	CreateText(
		*this,
		"You are God, forging the foundations of existence.\n\n In your hands are disks "
		"representing a point in a cycle.\n\n Your goal is to place them in the correct order, "
		"forming stable cycles that define the laws and rhythms of the universe.",
		color::Black, 36, {}, properties
	)
		.SetPosition(center + V2_float{ 0, -50 });
	CreateButton(*this)
		.SetText("Back", color::White)
		.SetFontSize(36)
		.SetBackgroundColor(color::Gray)
		.SetBackgroundColor(color::DarkGray, ButtonState::Hover)
		.SetBackgroundColor(color::Black, ButtonState::Pressed)
		.SetSize(V2_float{ 300, 100 })
		.OnActivate([]() { game.scene.Transition<MainMenuScene>("instruction", "main_menu", {}); })
		.SetPosition(center + V2_float{ 0, 250 });
}

class LoadingScene : public Scene {
public:
	void Enter() override {
		LoadResources("resources/data/resources.json");

		game.scene.Transition<MainMenuScene>("loading", "main_menu", {});
	}
};

int main() {
	game.Init(window_title, resolution, window_color);
	game.scene.Enter<LoadingScene>("loading");
	return 0;
}