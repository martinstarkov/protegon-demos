#include "audio/audio.h"
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
#include "utility/string.h"

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

	virtual void OnPickup([[maybe_unused]] Entity dropzone) override;

	virtual void OnDrop([[maybe_unused]] Entity dropzone) override;
};

struct ButtonAudioScript : public Script<ButtonAudioScript> {
	ButtonAudioScript() {}

	void OnButtonActivate() override {
		game.sound.Play("click");
	}
};

Button CreateMyButton(Scene& scene) {
	auto button = CreateButton(scene);
	button.AddScript<ButtonAudioScript>();
	return button;
}

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

Entity CreateScroll(Scene& scene, const TextContent& scroll_content, const FontSize& font_size) {
	TextProperties properties;
	// properties.style = FontStyle::Bold;
	// How wide we want the text to be within the scroll texture (has some margins).
	properties.wrap_after = 440;
	ResourceHandle font_key{ "text_font" };
	auto scroll_text =
		CreateText(scene, scroll_content, color::Black, font_size, font_key, properties);
	Sprite entity = CreateSprite(scene, "scroll");
	entity.SetDepth(2);
	entity.SetOrigin(Origin::Center);
	entity.SetPosition(center);
	scroll_text.SetPosition(V2_float{ 10.0f, 0.0f });
	scroll_text.SetParent(entity);
	scroll_text.SetDepth(3);
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
	entity.SetDepth(1);
	entity.AddScript<DiskDragScript>();
	return entity;
}

Entity CreateDiskSlot(Scene& scene, const V2_float& position, Sprite tablet) {
	// Size of the interactable.
	// PTGN_LOG("Disk slot inside1");
	float radius{ game.texture.GetSize("disk_out").x / 2.0f };
	// PTGN_LOG("Disk slot inside2");
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
	bool CheckSequenceWithFlip(
		const std::vector<Entity>& items, std::vector<Entity>::const_iterator it,
		const std::vector<std::pair<std::size_t, std::size_t>>& flip_pairs
	) {
		std::size_t start = std::distance(items.begin(), it);
		std::size_t n	  = items.size();

		auto is_flippable = [&](std::size_t i, std::size_t val) -> bool {
			if (i == val) {
				return true; // normal case
			}

			// Check if i and val form a flip pair (order does not matter)
			for (const auto& p : flip_pairs) {
				if ((i == p.first && val == p.second) || (i == p.second && val == p.first)) {
					return true;
				}
			}
			return false;
		};

		for (std::size_t i = 0; i < n; ++i) {
			const Entity& current = items[(start + i) % n];
			std::size_t val		  = current.Get<DiskIndex>().GetValue();

			if (!is_flippable(i, val)) {
				return false;
			}
		}

		return true;
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

		auto current_cycle_name{ GetCurrentCycleName() };

		if (current_cycle_name == "racecar") {
			std::vector<std::pair<std::size_t, std::size_t>> flip_pairs = { { 1, 5 }, { 2, 4 } };
			return CheckSequenceWithFlip(items, it, flip_pairs);
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

	bool reactive{ false };

	Entity tablet;

	Button submit;

	Entity scroll;

	Timer game_timer;

	std::vector<Entity> disks;

	std::vector<Entity> disk_slots;

	int set_of_cycles_index{ 0 };

	json game_json;
	json level_object;
	json cycles;
	// pair: name of cycle, correctness score
	std::vector<std::pair<std::string, int>> correctness;
	int current_cycle{ 0 };

	GameScene(int set_of_cycles_index) : set_of_cycles_index{ set_of_cycles_index } {
		if (game_json.empty()) {
			game_json = game.json.Get("game_json");
			// PTGN_LOG("Levels json: ", levels.dump(4));
		}
		PTGN_ASSERT(game_json.contains("levels"));
		auto levels = game_json.at("levels");
		PTGN_ASSERT(levels.is_array());
		if (set_of_cycles_index > levels.size()) {
			PTGN_ERROR("Level index outside of range of json levels array");
		}
		level_object = levels.at(set_of_cycles_index);
		PTGN_ASSERT(level_object.is_object());
		PTGN_ASSERT(level_object.contains("cycles"));
		cycles = level_object.at("cycles");
		correctness.clear();
		PTGN_ASSERT(cycles.is_array());
		PTGN_ASSERT(cycles.size() > 0, "Each level must have at least one cycle");
	}

	// @return True if all the slots are not -1.
	bool AllFilled(const std::vector<Entity>& items) {
		if (items.empty()) {
			return false;
		}
		auto it = std::find_if(items.begin(), items.end(), [](const Entity& item) {
			return item.Get<DiskIndex>().GetValue() == -1;
		});
		return it == items.end();
	}

	struct NextCycleScript : public Script<NextCycleScript> {
		NextCycleScript() {}

		void OnTimerStart() override;
		void OnTimerUpdate(float f) override;
		bool OnTimerStop() override;
	};

	void DeleteLevel() {
		int max_disk_fly_duration{ 2000 };

		RNG<int> fly_duration{ 500, 2000 };
		auto fly_ease{ AsymmetricalEase::InBack };
		milliseconds tablet_fly_duration{ 1000 };

		game.sound.Play("rockfly");
		TranslateTo(
			tablet, tablet.GetPosition() + V2_float{ 0.0f, -resolution.y }, tablet_fly_duration,
			fly_ease
		);
		for (auto i = 0; i < disks.size(); i++) {
			auto disk = disks[i];
			TranslateTo(
				disk, disk.GetPosition() + V2_float{ 0.0f, -resolution.y }, tablet_fly_duration,
				fly_ease
			);
		}
	}

	std::string GetCurrentCycleName() {
		PTGN_ASSERT(!cycles.empty());
		if (current_cycle < cycles.size()) {
			json cycle = cycles.at(current_cycle);
			PTGN_ASSERT(cycle.is_string());
			std::string cycle_name{ cycle.get<std::string>() };
			return cycle_name;
		}
		return "";
	}

	json GetNextCycle() {
		auto name = GetCurrentCycleName();
		bool correct{ CheckPattern(disk_slots) };
		int score = 0;
		if (correct) {
			PTGN_LOG("Cycle '", name, "' correct");
			score = 1;
		} else {
			PTGN_LOG("Cycle '", name, "' incorrect");
		}
		correctness.emplace_back(name, score);

		current_cycle++;

		auto remaining{ cycles.size() - current_cycle };

		auto next_name = GetCurrentCycleName();
		if (next_name.empty()) {
			cycles_remaining_text.SetContent("Level completed");
			return json{}; // No more cycles remaining.
		} else {
			std::string remaining_text{ "Cycles remaining: " + ToString(remaining) };
			cycles_remaining_text.SetContent(remaining_text);
		}
		return GetCycle(next_name);
	}

	struct ScrollFallScript : public Script<ScrollFallScript> {
		ScrollFallScript() {}

		void OnTimerUpdate(float f) override;
		bool OnTimerStop() override;
	};

	void ShowScroll() {
		auto fall_ease{ AsymmetricalEase::OutBounce };
		milliseconds scroll_fall_duration{ 1000 };

		TextContent scroll_content{ "" /*"Here is how you did: \n\n"*/ };
		FontSize font_size{ 18 };
		if (level_object.contains("font_size")) {
			int font_size_json{ level_object.at("font_size").get<int>() };
			font_size = font_size_json;
		}

		for (auto i = 0; i < correctness.size(); i++) {
			std::string cycle_name = correctness[i].first;
			int cycle_correctness  = correctness[i].second;
			PTGN_ASSERT(game_json.contains(cycle_name), "Cycle ", cycle_name, " not found in json");
			json cycle_json{ game_json.at(cycle_name) };
			PTGN_ASSERT(cycle_json.is_object());
			if (cycle_json.is_array()) {
				cycle_json = cycle_json.at(0);
			}
			std::string message;
			if (cycle_correctness == 0 && cycle_json.contains("failure_message")) {
				std::string failure{ cycle_json.at("failure_message").get<std::string>() };
				message = failure + " "; // + "\n";
			} else if (cycle_correctness == 1 && cycle_json.contains("success_message")) {
				std::string success{ cycle_json.at("success_message").get<std::string>() };
				message = success + " "; // + "\n";
			} else {
				message = "CYCLE MESSAGE NOT FOUND, SORRY.";
			}
			scroll_content.GetValue() += message;
		}

		// PTGN_LOG("Scroll size: ", font_size.GetValue(), ", content: ",
		// scroll_content.GetValue());

		scroll.Destroy();
		scroll = CreateScroll(*this, scroll_content, font_size);
		auto scroll_position{ scroll.GetPosition() };
		scroll.SetPosition(scroll_position + V2_float{ 0.0f, -resolution.y });
		TranslateTo(scroll, scroll_position, scroll_fall_duration, fall_ease);

		scroll.AddTimerScript<ScrollFallScript>(scroll_fall_duration);
	}

	void DestroyCycle() {
		tablet.Destroy();

		for (auto i = 0; i < disk_slots.size(); ++i) {
			Entity slot = disk_slots[i];
			slot.Destroy();
		}
		disk_slots.clear();

		for (auto i = 0; i < disks.size(); ++i) {
			Entity disk = disks[i];
			disk.Destroy();
		}
		disks.clear();
	}

	void CreateCycle(json j) {
		if (j.is_array()) {
			j = j.at(0);
		}
		PTGN_ASSERT(j.contains("tablet"));
		PTGN_ASSERT(j.at("tablet").is_string());
		// PTGN_LOG("Level json: ", j);

		RNG<int> fall_duration{ 500, 2000 };
		auto fall_ease{ AsymmetricalEase::OutBounce };
		milliseconds tablet_fall_duration{ 1000 };

		TextureHandle tablet_handle{ j.at("tablet").get<std::string>() };

		tablet = CreateTablet(*this, tablet_handle);
		auto tablet_position{ tablet.GetPosition() };
		tablet.SetPosition(tablet_position + V2_float{ 0.0f, -resolution.y });
		TranslateTo(tablet, tablet_position, tablet_fall_duration, fall_ease);

		PTGN_ASSERT(j.contains("positions"));
		PTGN_ASSERT(j.at("positions").is_array());

		std::vector<V2_int> slots{ j.at("positions").get<std::vector<V2_int>>() };

		// PTGN_LOG("11: ", j.at("positions").dump(4));

		/*for (auto i = 0; i < slots.size(); i++) {
			PTGN_LOG(slots[i]);
		}*/

		for (auto i = 0; i < slots.size(); i++) {
			V2_int slot = slots[i];
			disk_slots.emplace_back(CreateDiskSlot(*this, slot, tablet));
		}

		PTGN_ASSERT(j.contains("disks"));
		// PTGN_LOG("15: ", j.dump(4));

		const auto& json_disks{ j.at("disks") };
		// PTGN_LOG("16: ", json_disks.dump(4));

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
			TextureHandle handle{ json_disks[i].get<std::string>() };
			auto disk{ CreateDisk(*this, *position, handle, static_cast<int>(i)) };
			disk.SetPosition(*position + V2_float{ 0.0f, -resolution.y });
			TranslateTo(disk, *position, milliseconds{ fall_duration() }, fall_ease);
			disks.push_back(disk);
		}
		submit.Enable();
	}

	json GetCycle(std::string_view cycle_name) {
		PTGN_ASSERT(cycle_name != "level");
		PTGN_ASSERT(game_json.contains(cycle_name), "Cycle ", cycle_name, " not found in json");
		auto cycle{ game_json.at(cycle_name) };
		if (cycle.is_array()) {
			cycle = cycle.at(0);
		}
		return cycle;
	}

	Text timer_text;

	Sprite cycles_remaining;
	Text cycles_remaining_text;
	Sprite timer_bg;

	void Enter() override {
		current_cycle = 0;
		correctness.clear();

		game_timer.Start();
		input.SetDrawInteractives(true);
		input.SetTopOnly(false);

		auto name = GetCurrentCycleName();

		PTGN_ASSERT(!name.empty(), "Could not find a current cycle name that is valid");

		auto cycle{ GetCycle(name) };
		if (cycle.is_array()) {
			cycle = cycle.at(0);
		}

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
						 if (!submit.HasScript<NextCycleScript>()) {
							 game.sound.Play("bell");
							 submit.AddTimerScript<NextCycleScript>(milliseconds{ 2000 });
						 }
						 submit.Disable();
					 })
					 .SetInteractable(submit_interactable, false)
					 .SetOrigin(Origin::TopLeft)
					 .SetPosition({ 988, 69 });

		cycles_remaining_text.Destroy();
		cycles_remaining.Destroy();

		auto remaining{ cycles.size() };
		std::string remaining_content{ "Cycles remaining: " + ToString(remaining) };
		TextProperties proper;
		proper.justify		  = TextJustify::Center;
		cycles_remaining_text = CreateText(*this, remaining_content, color::Black, 24, {}, proper);
		cycles_remaining	  = CreateSprite(*this, "cycles_remaining");
		cycles_remaining.SetPosition({ 0.0f, resolution.y });
		cycles_remaining.SetOrigin(Origin::BottomLeft);
		cycles_remaining_text.SetPosition(V2_float{ 181, 677 });
		cycles_remaining_text.SetOrigin(Origin::Center);
		cycles_remaining_text.SetDepth(0);

		timer_bg.Destroy();
		timer_bg = CreateSprite(*this, "replay_button");
		timer_bg.SetPosition({ 0.0f, 0.0f });
		timer_bg.SetOrigin(Origin::TopLeft);

		game.sound.Play("rockfly2");
		DestroyCycle();
		CreateCycle(cycle);

		timer_text = CreateText(*this, "", color::Black, 48, {});
		timer_text.SetPosition(V2_float{ 20.0f, 20.0f });
		timer_text.SetOrigin(Origin::TopLeft);
	}

	struct DiamondsScript : public TweenScript<DiamondsScript> {
		DiamondsScript() {}

		void OnUpdate(TweenInfo info) override {
			info.parent.SetTint(color::Black.WithAlpha(info.progress * 0.5f));
		}
	};

	void Update() override;
};

class InstructionScene : public Scene {
public:
	void Enter() override;

	void Update() override;
};

class LevelSelect : public Scene {
public:
	void Enter() override;
};

class MainMenuScene : public Scene {
public:
	void Enter() override {
		CreateSprite(*this, "main_menu_bg").SetOrigin(Origin::TopLeft);
		CreateMyButton(*this)
			.SetText("Play", color::Black)
			.SetFontSize(48)
			.SetTextureKey("main_button")
			.SetButtonTint(color::White)
			.SetButtonTint(color::Gray, ButtonState::Hover)
			.SetButtonTint(color::DarkGray, ButtonState::Pressed)
			.SetSize(V2_float{ 450, 150 })
			.OnActivate([]() {
				game.scene.Transition<LevelSelect>("main_menu", "level_select", {});
			})
			.SetPosition(center + V2_float{ -300, 100 });
		CreateMyButton(*this)
			.SetText("Instructions", color::Black)
			.SetFontSize(48)
			.SetTextureKey("main_button")
			.SetButtonTint(color::White)
			.SetButtonTint(color::Gray, ButtonState::Hover)
			.SetButtonTint(color::DarkGray, ButtonState::Pressed)
			.SetSize(V2_float{ 450, 150 })
			.OnActivate([]() {
				game.scene.Transition<InstructionScene>("main_menu", "instruction", {});
			})
			.SetPosition(center + V2_float{ 300, 100 });
	}
};

void LevelSelect::Enter() {
	CreateSprite(*this, "level_select_bg").SetOrigin(Origin::TopLeft);
	CreateMyButton(*this)
		.SetText("1", color::Black)
		.SetFontSize(48)
		.SetTextureKey("square_button")
		.SetButtonTint(color::White)
		.SetButtonTint(color::Gray, ButtonState::Hover)
		.SetButtonTint(color::DarkGray, ButtonState::Pressed)
		.SetSize(V2_float{ 125 })
		.OnActivate([]() { game.scene.Transition<GameScene>("level_select", "game", {}, 0); })
		.SetPosition(center + V2_float{ -300, 0 });
	CreateMyButton(*this)
		.SetText("2", color::Black)
		.SetFontSize(48)
		.SetTextureKey("square_button")
		.SetButtonTint(color::White)
		.SetButtonTint(color::Gray, ButtonState::Hover)
		.SetButtonTint(color::DarkGray, ButtonState::Pressed)
		.SetSize(V2_float{ 125 })
		.OnActivate([]() { game.scene.Transition<GameScene>("level_select", "game", {}, 1); })
		.SetPosition(center + V2_float{ 0, 0 });
	CreateMyButton(*this)
		.SetText("3", color::Black)
		.SetFontSize(48)
		.SetTextureKey("square_button")
		.SetButtonTint(color::White)
		.SetButtonTint(color::Gray, ButtonState::Hover)
		.SetButtonTint(color::DarkGray, ButtonState::Pressed)
		.SetSize(V2_float{ 125 })
		.OnActivate([]() { game.scene.Transition<GameScene>("level_select", "game", {}, 2); })
		.SetPosition(center + V2_float{ 300, 0 });
	CreateMyButton(*this)
		.SetText("Back", color::Black)
		.SetFontSize(36)
		.SetTextureKey("back_button1")
		.SetButtonTint(color::White)
		.SetButtonTint(color::Gray, ButtonState::Hover)
		.SetButtonTint(color::DarkGray, ButtonState::Pressed)
		.SetSize(V2_float{ 300, 100 })
		.OnActivate([]() { game.scene.Transition<MainMenuScene>("level_select", "main_menu", {}); })
		.SetPosition(center + V2_float{ 0, 280 });
}

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
	ResourceHandle font_key{ "text_font" };
	CreateText(
		*this,
		"You are God, forging the foundations of existence. In your hands are disks "
		"representing a point in a cycle.\n\n Your goal is to place them in the correct order, "
		"forming stable cycles that define the laws and rhythms of the universe.\n\n Ring the "
		"bell "
		"when ready.",
		color::White, 36, font_key, properties
	)
		.SetPosition(center + V2_float{ 0, -50 });
	CreateMyButton(*this)
		.SetText("Back", color::Black)
		.SetFontSize(36)
		.SetTextureKey("back_button1")
		.SetButtonTint(color::White)
		.SetButtonTint(color::Gray, ButtonState::Hover)
		.SetButtonTint(color::DarkGray, ButtonState::Pressed)
		.SetSize(V2_float{ 300, 100 })
		.OnActivate([]() { game.scene.Transition<MainMenuScene>("instruction", "main_menu", {}); })
		.SetPosition(center + V2_float{ 0, 280 });
}

class LoadingScene : public Scene {
public:
	void Enter() override {
		LoadResources("resources/data/resources.json");
		game.music.SetVolume(15);
		game.sound.SetVolume("rockfly", 15);
		game.sound.SetVolume("rockfly2", 128);
		game.sound.SetVolume("bell", 40);
		game.sound.SetVolume("click", 40);
		game.music.Play("elevator_music", -1);
		game.scene.Transition<MainMenuScene>("loading", "main_menu", {});
	}
};

void GameScene::Update() {
	bool correct{ CheckPattern(disk_slots) };
	bool filled{ AllFilled(disk_slots) };

	auto diamonds = tablet.GetChild("diamonds");

	if (diamonds) {
		if ((filled || correct) && !diamonds.IsVisible()) {
			diamonds.Show();
			if (diamonds.HasChild("tween")) {
				diamonds.GetChild("tween").Destroy();
			}
			auto tween = CreateTween(*this);
			diamonds.AddChild(tween, "tween");
			tween.During(milliseconds{ 3000 })
				.Repeat(-1)
				.Yoyo()
				.AddTweenScript<GameScene::DiamondsScript>();
			tween.Start();
		}
	}

	if (submit) {
		if (correct) {
			// submit.SetButtonTint(color::Cyan);
			submit.Enable();
		} else if (filled) {
			// submit.SetButtonTint(color::Red);
			submit.Enable();
		} else {
			submit.Disable();
			if (diamonds && diamonds.HasChild("tween")) {
				diamonds.Hide();
				diamonds.GetChild("tween").Destroy();
			}
			// submit.SetButtonTint(color::White);
		}
	}
	if (game.input.KeyDown(Key::Escape)) {
		game.scene.Transition<MainMenuScene>("game", "main_menu", {});
	}
	std::string elapsed_text{ "Time: " +
							  ToString(game_timer.Elapsed<duration<float>>().count(), 1) };
	timer_text.SetContent(elapsed_text);

	if (reactive) {
		reactive = false;
		ReEnter();
	}
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

void GameScene::NextCycleScript::OnTimerStart() {
	auto& scene{ game.scene.Get<GameScene>("game") };
	scene.DeleteLevel();
}

void GameScene::NextCycleScript::OnTimerUpdate(float f) {
	auto& scene{ game.scene.Get<GameScene>("game") };
	scene.submit.Disable();
}

bool GameScene::NextCycleScript::OnTimerStop() {
	auto& scene{ game.scene.Get<GameScene>("game") };
	auto cycle = scene.GetNextCycle();
	if (cycle.empty()) {
		scene.game_timer.Stop();
		scene.submit.Disable();
		scene.ShowScroll();
		scene.DestroyCycle();
	} else {
		scene.DestroyCycle();
		scene.CreateCycle(cycle);
	}
	return true;
}

void GameScene::ScrollFallScript::OnTimerUpdate(float f) {
	auto& scene{ game.scene.Get<GameScene>("game") };
	scene.submit.Disable();
}

bool GameScene::ScrollFallScript::OnTimerStop() {
	auto& scene{ game.scene.Get<GameScene>("game") };
	scene.submit.Disable();
	CreateMyButton(scene)
		.SetText("Replay", color::Black)
		.SetFontSize(36)
		.SetTextureKey("replay_button")
		.SetButtonTint(color::White)
		.SetButtonTint(color::Gray, ButtonState::Hover)
		.SetButtonTint(color::DarkGray, ButtonState::Pressed)
		.SetSize(V2_float{ 300, 80 })
		.OnActivate([&scene]() { scene.reactive = true; })
		.SetPosition(center + V2_float{ 470, 200 });

	CreateMyButton(scene)
		.SetText("Level Select", color::Black)
		.SetFontSize(36)
		.SetTextureKey("replay_button")
		.SetButtonTint(color::White)
		.SetButtonTint(color::Gray, ButtonState::Hover)
		.SetButtonTint(color::DarkGray, ButtonState::Pressed)
		.SetSize(V2_float{ 300, 80 })
		.OnActivate([this]() { game.scene.Transition<MainMenuScene>("game", "main_menu", {}); })
		.SetPosition(center + V2_float{ 470, 300 });
	return true;
}

int main() {
	game.Init(window_title, resolution, window_color);
	game.scene.Enter<LoadingScene>("loading");
	return 0;
}