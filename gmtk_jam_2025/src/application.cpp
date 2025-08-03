#include "components/animation.h"
#include "components/draw.h"
#include "components/generic.h"
#include "components/input.h"
#include "components/sprite.h"
#include "core/entity.h"
#include "core/game.h"
#include "input/input_handler.h"
#include "math/geometry/circle.h"
#include "math/vector2.h"
#include "renderer/api/color.h"
#include "renderer/api/origin.h"
#include "scene/scene.h"
#include "scene/scene_manager.h"
#include "serialization/json_manager.h"
#include "tweens/tween.h"
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
	void OnDrag(V2_float mouse) override;

	virtual void OnPickup([[maybe_unused]] Entity dropzone);

	virtual void OnDrop([[maybe_unused]] Entity dropzone);
};

Entity CreateTablet(Scene& scene, const TextureHandle& texture_handle) {
	Entity entity = CreateSprite(scene, texture_handle);
	auto diamonds = CreateSprite(scene, "diamonds");
	diamonds.SetDepth(1);
	diamonds.Hide();
	entity.AddChild(diamonds, "diamonds");
	entity.SetOrigin(Origin::Center);
	// entity.Hide();
	entity.SetPosition(center);
	return entity;
}

Entity CreateScroll(Scene& scene) {
	Entity entity = CreateSprite(scene, "scroll");
	entity.SetOrigin(Origin::Center);
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
	entity.SetDepth(2);
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

	Button submit;

	Entity scroll;

	Timer game_timer;

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

	struct NextLevelScript : public Script<NextLevelScript> {
		NextLevelScript() {}

		void OnTimerStart();

		bool OnTimerStop();
	};

	void DeleteLevel() {
		int max_disk_fly_duration{ 2000 };

		RNG<int> fly_duration{ 500, 2000 };
		auto fly_ease{ AsymmetricalEase::InBounce };
		milliseconds tablet_fly_duration{ 1000 };
		TranslateTo(
			tablet, tablet.GetPosition() + V2_float{ 0.0f, -resolution.y }, tablet_fly_duration,
			fly_ease
		);
		for (auto& disk : disks) {
			TranslateTo(
				disk, disk.GetPosition() + V2_float{ 0.0f, -resolution.y },
				milliseconds{ fly_duration() }, fly_ease
			);
		}
	}

	json GetNextLevel() {
		auto level_name{ "plant" };
		return json{}; // GetLevel(level_name);
	}

	struct ScrollFallScript : public Script<ScrollFallScript> {
		ScrollFallScript() {}

		bool OnTimerStop();
	};

	void ShowScroll() {
		auto fall_ease{ AsymmetricalEase::OutBounce };
		milliseconds scroll_fall_duration{ 1000 };

		scroll.Destroy();
		scroll = CreateScroll(*this);
		auto scroll_position{ scroll.GetPosition() };
		scroll.SetPosition(scroll_position + V2_float{ 0.0f, -resolution.y });
		TranslateTo(scroll, scroll_position, scroll_fall_duration, fall_ease);

		scroll.AddTimerScript<ScrollFallScript>(scroll_fall_duration);
	}

	void CreateLevel(const json& j) {
		PTGN_ASSERT(j.contains("tablet"));
		PTGN_ASSERT(j.at("tablet").is_string());

		RNG<int> fall_duration{ 500, 2000 };
		auto fall_ease{ AsymmetricalEase::OutBounce };
		milliseconds tablet_fall_duration{ 1000 };

		tablet.Destroy();
		tablet = CreateTablet(*this, j.at("tablet"));
		auto tablet_position{ tablet.GetPosition() };
		tablet.SetPosition(tablet_position + V2_float{ 0.0f, -resolution.y });
		TranslateTo(tablet, tablet_position, tablet_fall_duration, fall_ease);

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

		RandomPicker<V2_float> random_picker{ V2_float{ 110, 236 },	 V2_float{ 314, 234 },
											  V2_float{ 80, 400 },	 V2_float{ 238, 375 },
											  V2_float{ 102, 575 },	 V2_float{ 300, 512 },
											  V2_float{ 253, 652 },	 V2_float{ 987, 331 },
											  V2_float{ 1152, 322 }, V2_float{ 1023, 476 },
											  V2_float{ 1207, 454 }, V2_float{ 976, 616 },
											  V2_float{ 1166, 617 } };

		PTGN_ASSERT(json_disks.size() < random_picker.Size());

		for (std::size_t i{ 0 }; i < json_disks.size(); ++i) {
			auto position{ random_picker.Next() };
			PTGN_ASSERT(position);
			PTGN_ASSERT(json_disks[i].is_string());
			TextureHandle handle;
			json_disks[i].get_to(handle);
			auto disk{ CreateDisk(*this, *position, handle, static_cast<int>(i)) };
			disk.SetPosition(*position + V2_float{ 0.0f, -resolution.y });
			TranslateTo(disk, *position, milliseconds{ fall_duration() }, fall_ease);
			disk.SetDepth(1);
			disks.push_back(disk);
		}
		submit.Enable();
	}

	json levels;

	json GetLevel(std::string_view level_name) {
		PTGN_ASSERT(level_name != "level");
		PTGN_ASSERT(levels.contains(level_name), "Level must be set to a valid level entry");
		auto level{ levels.at(level_name) };
		return level;
	}

	Text timer_text;

	void Enter() override {
		game_timer.Start();
		input.SetDrawInteractives(true);
		input.SetTopOnly(false);

		if (levels.empty()) {
			levels = game.json.Get("game_json");
		}

		PTGN_ASSERT(levels.contains("level"));
		auto level_name{ levels.at("level") };
		auto level{ GetLevel(level_name) };

		CreateSprite(*this, "game_bg").SetOrigin(Origin::TopLeft);

		Entity submit_interactable = CreateEntity().SetPosition({ 1114, 152 }
		); /*CreateCircle(*this, { 1114, 152 }, 56.0f, color::Magenta, 1.0f)*/
		submit_interactable.Add<Circle>(56.0f);
		submit = CreateButton(*this)
					 .SetTextureKey("submit")
					 .SetButtonTint(color::White)
					 .SetButtonTint(color::Gray, ButtonState::Hover)
					 .SetButtonTint(color::Gray, ButtonState::Pressed)
					 .OnActivate([this]() {
						 submit.AddTimerScript<NextLevelScript>(milliseconds{ 2000 });
						 submit.Disable();
					 })
					 .SetInteractable(submit_interactable, false)
					 .SetOrigin(Origin::TopLeft)
					 .SetPosition({ 988, 69 });

		CreateLevel(level);

		timer_text = CreateText(*this, "", color::White, 48, {});
		timer_text.SetPosition(V2_float{ 20.0f });
		timer_text.SetOrigin(Origin::TopLeft);
	}

	struct DiamondsScript : public TweenScript<DiamondsScript> {
		DiamondsScript() {}

		void OnUpdate(TweenInfo info) {
			info.parent.SetTint(color::Black.WithAlpha(info.progress * 0.5f));
		}
	};

	void Update();
};

class InstructionScene : public Scene {
public:
	void Enter() override;

	void Update() override;
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

void InstructionScene::Update() {
	if (game.input.KeyDown(Key::Escape)) {
		game.scene.Transition<InstructionScene>("instruction", "main_menu", {});
	}
}

void InstructionScene::Enter() {
	CreateSprite(*this, "instructions_bg").SetOrigin(Origin::TopLeft);
	TextProperties properties;
	properties.wrap_after = static_cast<std::uint32_t>(resolution.x * 0.9f);
	properties.justify	  = TextJustify::Center;
	CreateText(
		*this,
		"You are God, forging the foundations of existence. In your hands are disks "
		"representing a point in a cycle.\n\n Your goal is to place them in the correct order, "
		"forming stable cycles that define the laws and rhythms of the universe.\n\n Ring the "
		"bell "
		"when ready.",
		color::White, 36, {}, properties
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
		.SetPosition(center + V2_float{ 0, 280 });
}

class LoadingScene : public Scene {
public:
	void Enter() override {
		LoadResources("resources/data/resources.json");

		game.scene.Transition<MainMenuScene>("loading", "main_menu", {});
	}
};

void GameScene::Update() {
	bool correct{ CheckPattern(disk_slots) };
	bool filled{ AllFilled(disk_slots) };

	auto diamonds = tablet.GetChild("diamonds");

	if ((filled || correct) && !diamonds.IsVisible()) {
		diamonds.Show();
		if (diamonds.HasChild("tween")) {
			diamonds.GetChild("tween").Destroy();
		}
		auto tween = CreateTween(*this);
		diamonds.AddChild(tween, "tween");
		tween.During(milliseconds{ 3000 }).Repeat(-1).Yoyo().AddTweenScript<DiamondsScript>();
		tween.Start();
	}

	if (correct) {
		submit.SetButtonTint(color::Cyan);
		submit.Enable();
	} else if (filled) {
		submit.SetButtonTint(color::Red);
		submit.Enable();
	} else {
		submit.Disable();
		if (diamonds.HasChild("tween")) {
			diamonds.Hide();
			diamonds.GetChild("tween").Destroy();
		}
		submit.SetButtonTint(color::White);
	}
	if (game.input.KeyDown(Key::Escape)) {
		game.scene.Transition<MainMenuScene>("game", "main_menu", {});
	}
	std::string elapsed_text{ "Time: " +
							  ToString(game_timer.Elapsed<duration<float>>().count(), 1) };
	timer_text.SetContent(elapsed_text);
}

void DiskDragScript::OnDrag(V2_float mouse) {
	auto& pos = entity.GetPosition();
	pos		  = mouse + entity.Get<Draggable>().offset;
	pos.x	  = std::clamp(pos.x, 0.0f, (float)resolution.x);
	pos.y	  = std::clamp(pos.y, 0.0f, (float)resolution.y);
}

void DiskDragScript::OnPickup([[maybe_unused]] Entity dropzone) {
	if (dropzone.Get<Dropzone>().dropped_entities.empty()) {
		dropzone.Add<DiskIndex>(-1);
		entity.GetChild("disk_out").Show();
		// PTGN_LOG("Setting dropzone ", dropzone.GetPosition(), " to -1");
	}
}

void DiskDragScript::OnDrop([[maybe_unused]] Entity dropzone) {
	if (dropzone.Get<Dropzone>().dropped_entities.empty()) {
		dropzone.Add<DiskIndex>(entity.Get<DiskIndex>());
		// Shake(game.scene.Get<GameScene>("game").tablet, 0.5f);
		Shake(entity, 0.2f);
		entity.GetChild("disk_out").Hide();
		/*PTGN_LOG(
			"Setting dropzone ", dropzone.GetPosition(), " to ", dropzone.Get<DiskIndex>()
		);*/
		entity.GetPosition() = dropzone.GetAbsolutePosition();
	}
}

void GameScene::NextLevelScript::OnTimerStart() {
	auto& scene{ game.scene.Get<GameScene>("game") };
	scene.DeleteLevel();
}

bool GameScene::NextLevelScript::OnTimerStop() {
	auto& scene{ game.scene.Get<GameScene>("game") };
	bool correct{ scene.CheckPattern(scene.disk_slots) };
	if (correct) {
		PTGN_LOG("Correct!");
	} else {
		PTGN_LOG("Incorrect!");
	}
	auto level = scene.GetNextLevel();
	if (level.empty()) {
		scene.game_timer.Stop();
		scene.ShowScroll();
	} else {
		scene.CreateLevel(level);
	}
	return true;
}

bool GameScene::ScrollFallScript::OnTimerStop() {
	auto& scene{ game.scene.Get<GameScene>("game") };
	CreateButton(scene)
		.SetText("Replay", color::White)
		.SetFontSize(24)
		.SetBackgroundColor(color::Gray)
		.SetBackgroundColor(color::DarkGray, ButtonState::Hover)
		.SetBackgroundColor(color::Black, ButtonState::Pressed)
		.SetSize(V2_float{ 200, 60 })
		.OnActivate([&scene]() { scene.ReEnter(); })
		.SetPosition(center + V2_float{ 450, 200 });

	CreateButton(scene)
		.SetText("Level Select", color::White)
		.SetFontSize(24)
		.SetBackgroundColor(color::Gray)
		.SetBackgroundColor(color::DarkGray, ButtonState::Hover)
		.SetBackgroundColor(color::Black, ButtonState::Pressed)
		.SetSize(V2_float{ 200, 60 })
		.OnActivate([this]() { game.scene.Transition<MainMenuScene>("game", "main_menu", {}); })
		.SetPosition(center + V2_float{ 450, 300 });
	return true;
}

int main() {
	game.Init(window_title, resolution, window_color);
	game.scene.Enter<LoadingScene>("loading");
	return 0;
}