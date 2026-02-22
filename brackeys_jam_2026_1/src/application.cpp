#include "choice.h"
#include "math/geometry/circle.h"
#include "protegon/protegon.h"
#include "renderer/api/origin.h"

using namespace ptgn;

constexpr V2_int resolution{ 320, 180 };
constexpr V2_float center{ resolution / 2.0f };
constexpr V2_int world_size{ 320, 180 };
constexpr int button_channel{ 2 };
constexpr int planet_channel{ 3 };
std::vector<Entry> levels;
constexpr int planet_count{ 3 };

void SetupWindow() {
	game.window.SetSize(resolution * 4);
	game.renderer.SetScalingMode(ScalingMode::IntegerScale);
	// game.window.SetSetting(WindowSetting::Maximized);
}

struct ButtonAudioScript : public Script<ButtonAudioScript, ButtonScript> {
	ButtonAudioScript() {}

	void OnButtonActivate() override {
		game.sound.Play("high_beep");
	}

	void OnButtonHoverStart() override {
		game.sound.Play("low_beep", button_channel);
	}

	void OnButtonHoverStop() override {
		game.sound.Stop(button_channel);
	}
};

Button CreateMyButton(Scene& scene) {
	auto button = CreateButton(scene);
	AddScript<ButtonAudioScript>(button);
	return button;
}

struct Winner {};

struct Traits {
	std::vector<Trait> traits;
};

struct Selection {
	bool winner{ false };
};

struct PlanetScript : public Script<PlanetScript, ButtonScript> {
	Sprite planet_popup;
	Button exit_button;
	std::vector<Button> planet_buttons;
	Text planet_trait_text;
	Button human;
	Sprite planet_display;
	std::vector<Sprite> glows;
	std::shared_ptr<std::optional<Selection>> selected_level;
	Button confirm;

	PlanetScript() = default;

	PlanetScript(
		Sprite planet_popup, Button exit_button, std::vector<Button> planet_buttons,
		Text planet_trait_text, Button human, Sprite planet_display, std::vector<Sprite> glows,
		std::shared_ptr<std::optional<Selection>> selected_level, Button confirm
	) :
		planet_popup{ planet_popup },
		exit_button{ exit_button },
		planet_buttons{ planet_buttons },
		planet_trait_text{ planet_trait_text },
		human{ human },
		planet_display{ planet_display },
		glows{ glows },
		selected_level{ selected_level },
		confirm{ confirm } {}

	void OnButtonActivate() override {
		for (auto glow : glows) {
			Hide(glow);
		}
		Show(confirm);
		confirm.Enable();
		planet_display.SetTextureKey(Button{ entity }.GetTextureKey());
		Show(planet_display);
		Hide(human);
		human.Disable();
		Show(planet_trait_text);
		game.sound.Play("high_beep");
		for (auto button : planet_buttons) {
			button.Disable();
			Hide(button);
		}
		Show(planet_popup);
		exit_button.Enable();
		// PTGN_LOG("Chose planet with traits:");

		std::string planet_traits_content;
		for (const auto& trait : entity.Get<Traits>().traits) {
			planet_traits_content += "- " + trait.description + std::string("\n\n");
		}

		planet_trait_text.SetContent(planet_traits_content);

		if (entity.Has<Winner>()) {
			*selected_level = Selection{ true };
			// PTGN_LOG("You picked a winner!");
		} else {
			*selected_level = Selection{ false };
			// PTGN_LOG("You picked a loser!");
		}
	}

	void OnButtonHoverStart() override {
		game.sound.Play("low_beep", planet_channel);
	}

	void OnButtonHoverStop() override {
		game.sound.Stop(planet_channel);
	}
};

class GameScene : public Scene {
public:
	int level_{ 0 };
	Entry entry;

	Sprite planet_popup;
	std::vector<Button> planet_buttons;
	std::vector<Sprite> glows;
	Button exit_button;
	Button human;
	Button confirm;
	Text human_trait_text;
	Text planet_trait_text;
	Sprite planet_display;

	std::shared_ptr<std::optional<Selection>> selected_level;

	GameScene(int level) : level_{ level } {}

	void Enter() override {
		selected_level	= std::make_shared<std::optional<Selection>>();
		*selected_level = std::nullopt;
		planet_buttons	= {};
		glows			= {};
		if (level_ < levels.size()) {
			PTGN_LOG("Attempting to enter level which is out of range");
		}
		PTGN_ASSERT(level_ < levels.size(), "Attempting to enter level which is out of range");

		entry = levels[level_];

		auto dice = RollRoundDice(game.json.Get("traits"), entry);

		PTGN_ASSERT(dice.planets.size() == planet_count);

		std::string human_trait_text_content;
		for (const auto& cat : dice.chosen_categories) {
			PTGN_ASSERT(dice.chosen_human.contains(cat));
			const auto& trait		  = dice.chosen_human.at(cat);
			human_trait_text_content += "- " + trait.get<std::string>() + std::string("\n\n");
		}
		auto font_key{ "mono_font" };

		TextProperties properties1;
		properties1.wrap_after = static_cast<std::uint32_t>(140.0f * game.renderer.GetScale().x);
		properties1.justify	   = TextJustify::Left;
		human_trait_text =
			CreateText(*this, human_trait_text_content, color::White, 8, font_key, properties1);
		SetPosition(human_trait_text, V2_float{ -68, -40 });
		SetDrawOrigin(human_trait_text, Origin::TopLeft);
		SetDepth(human_trait_text, 4);
		Hide(human_trait_text);

		TextProperties properties2;
		properties2.wrap_after = static_cast<std::uint32_t>(73.0f * game.renderer.GetScale().x);
		properties2.justify	   = TextJustify::Left;
		planet_trait_text =
			CreateText(*this, "Planet Traits", color::White, 6, font_key, properties2);
		SetPosition(planet_trait_text, V2_float{ 5, -40 });
		SetDrawOrigin(planet_trait_text, Origin::TopLeft);
		SetDepth(planet_trait_text, 4);
		Hide(planet_trait_text);

		input.SetDrawInteractives(true);

		PTGN_LOG("Entering level ", level_);

		auto sprite = CreateSprite(*this, "background");
		SetDrawOrigin(sprite, Origin::Center);

		float x_offset{ 92.0f };

		planet_popup = CreateSprite(*this, "planet_popup");
		SetDrawOrigin(planet_popup, Origin::Center);
		Hide(planet_popup);
		SetPosition(planet_popup, V2_float{ 0, -1 });
		SetDepth(planet_popup, 3);

		planet_display = CreateSprite(*this, "planet_1");
		SetDrawOrigin(planet_display, Origin::Center);
		Hide(planet_display);
		SetPosition(
			planet_display, V2_float{ 0, -1 } - V2_float{ 183, 137 } / 2.0f + V2_float{ 53, 72 }
		);
		SetDepth(planet_display, 4);

		for (auto i = 0; i < planet_count; i++) {
			auto button = CreateButton(*this);
			V2_float planet_pos{ -x_offset + i * x_offset, 0.0f };
			// TODO: Pick randomly from a list of planet textures.
			auto glow = CreateSprite(*this, "glow");
			SetPosition(glow, planet_pos);
			SetDepth(glow, 1);
			auto planet = button.SetTextureKey("planet_" + std::to_string(i + 1))
							  .SetButtonTint(color::White)
							  .SetButtonTint(color::White.WithAlpha(0.95f), ButtonState::Hover)
							  .SetButtonTint(color::DarkGray, ButtonState::Pressed)
							  .SetSize(V2_float{ 60, 60 })
							  .OnHoverStart([=]() {
								  ScaleTo(glow, V2_float{ 1.1f }, milliseconds{ 100 });
								  TintTo(glow, Color{ 0, 177, 182, 255 }, milliseconds{ 100 });
							  })
							  .OnHoverStop([=]() {
								  ScaleTo(glow, V2_float{ 1.0f }, milliseconds{ 100 });
								  TintTo(glow, color::White, milliseconds{ 100 });
							  });
			glows.push_back(glow);
			SetPosition(planet, planet_pos);
			SetDepth(planet, 2);
			planet_buttons.push_back(planet);
		}

		confirm		= CreateMyButton(*this);
		exit_button = CreateMyButton(*this);

		human = CreateMyButton(*this);

		for (auto button : planet_buttons) {
			AddScript<PlanetScript>(
				button, planet_popup, exit_button, planet_buttons, planet_trait_text, human,
				planet_display, glows, selected_level, confirm
			);
		}

		for (auto i = 0; i < dice.planets.size(); i++) {
			PTGN_ASSERT(i < planet_buttons.size());
			if (i == dice.winner_planet_index) {
				planet_buttons[i].Add<Winner>();
			}
			planet_buttons[i].Add<Traits>(dice.planets[i].planet_traits);
		}

		auto human_popup = CreateSprite(*this, "human_popup");
		SetDrawOrigin(human_popup, Origin::Center);
		Hide(human_popup);
		SetPosition(human_popup, V2_float{ 0, -1 });
		SetDepth(human_popup, 3);

		exit_button.SetSize(V2_float{ 7 } * 2.0f).OnActivate([=]() mutable {
			for (auto button : planet_buttons) {
				button.Enable();
				Show(button);
			}
			for (auto glow : glows) {
				Show(glow);
				SetTint(glow, color::White);
			}
			*selected_level = std::nullopt;
			human.Enable();
			Hide(human_trait_text);
			Hide(planet_trait_text);
			Show(human);
			Hide(human_popup);
			Hide(planet_popup);
			Hide(planet_display);
			Hide(confirm);
			confirm.Disable();
			exit_button.Disable();
		});
		exit_button.Disable();
		SetPosition(exit_button, V2_float{ -58, -62 });

		human.SetText("Species Traits", color::Black, {}, "mono_font")
			.SetFontSize(6)
			.SetTextureKey("human_button")
			.SetButtonTint(color::White)
			.SetButtonTint(color::Gray, ButtonState::Hover)
			.SetButtonTint(color::DarkGray, ButtonState::Pressed)
			.SetSize(V2_float{ 80, 13 })
			.OnActivate([=]() mutable {
				for (auto button : planet_buttons) {
					button.Disable();
				}
				Show(human_trait_text);
				human.Disable();
				Hide(human);
				Show(human_popup);
				exit_button.Enable();
			});
		SetPosition(human, V2_float{ -38, 84 });

		confirm.SetTextureKey("confirm_button")
			.SetButtonTint(color::White)
			.SetButtonTint(color::Gray, ButtonState::Hover)
			.SetButtonTint(color::DarkGray, ButtonState::Pressed)
			.SetSize(V2_float{ 47, 14 })
			.OnActivate([=]() mutable {
				PTGN_ASSERT(selected_level->has_value(), "Cannot confirm without selection");
				PTGN_LOG("Confirmed: ", (*selected_level)->winner ? "Winner" : "Loser");
				/*for (auto button : planet_buttons) {
					button.Disable();
				}
				Show(human_trait_text);
				human.Disable();
				Hide(human);
				Show(human_popup);
				exit_button.Enable();*/
			});
		SetPosition(
			confirm, V2_float{ 0, -1 } - V2_float{ 183, 137 } / 2.0f + V2_float{ 144, 117 }
		);
		SetDepth(confirm, 4);
		Hide(confirm);
		confirm.Disable();
	}
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
		auto sprite = CreateSprite(*this, "title");
		SetDrawOrigin(sprite, Origin::Center);
		auto button = CreateMyButton(*this)
						  .SetTextureKey("main_button")
						  .SetButtonTint(color::White)
						  .SetButtonTint(color::Gray, ButtonState::Hover)
						  .SetButtonTint(color::DarkGray, ButtonState::Pressed)
						  .SetSize(V2_float{ 120, 50 })
						  .OnActivate([]() {
							  game.scene.Transition<LevelSelect>("main_menu", "level_select");
						  });
		SetDrawOrigin(button, Origin::TopLeft);
		SetPosition(button, V2_float{ 41, 113 } - center);
		auto button2 = CreateMyButton(*this)
						   .SetTextureKey("main_button")
						   .SetButtonTint(color::White)
						   .SetButtonTint(color::Gray, ButtonState::Hover)
						   .SetButtonTint(color::DarkGray, ButtonState::Pressed)
						   .SetSize(V2_float{ 120, 50 })
						   .OnActivate([]() {
							   game.scene.Transition<InstructionScene>("main_menu", "instruction");
						   });
		SetDrawOrigin(button2, Origin::TopLeft);
		SetPosition(button2, V2_float{ 136, 130 } - center);
	}
};

void LevelSelect::Enter() {
	auto sprite = CreateSprite(*this, "background");
	SetDrawOrigin(sprite, Origin::Center);

	for (int i{ 0 }; i < 5; ++i) {
		auto button =
			CreateMyButton(*this)
				.SetText(std::to_string(i + 1), color::Black, {}, "mono_font")
				.SetFontSize(10)
				.SetTextureKey("square_button")
				.SetButtonTint(color::White)
				.SetButtonTint(color::Gray, ButtonState::Hover)
				.SetButtonTint(color::DarkGray, ButtonState::Pressed)
				.SetSize(V2_float{ 40 })
				.OnActivate([i]() { game.scene.Transition<GameScene>("level_select", "game", i); });
		SetPosition(button, V2_float{ -120 + i * 60, -20 });
	}

	auto back = CreateMyButton(*this)
					.SetText("Back", color::Black, {}, "mono_font")
					.SetFontSize(14)
					.SetTextureKey("back_button")
					.SetButtonTint(color::White)
					.SetButtonTint(color::Gray, ButtonState::Hover)
					.SetButtonTint(color::DarkGray, ButtonState::Pressed)
					.SetSize(V2_float{ 120, 50 })
					.OnActivate([]() {
						game.scene.Transition<MainMenuScene>("level_select", "main_menu");
					});

	SetPosition(back, V2_float{ 0, 45 });
}

void InstructionScene::Update() {
	if (game.input.KeyDown(Key::Escape)) {
		game.scene.Transition<InstructionScene>("instruction", "main_menu");
	}
}

void InstructionScene::Enter() {
	auto sprite = CreateSprite(*this, "background");
	SetDrawOrigin(sprite, Origin::Center);
	TextProperties properties;
	properties.wrap_after = static_cast<std::uint32_t>(resolution.x * 0.9f);
	properties.justify	  = TextJustify::Center;
	auto font_key{ "mono_font" };
	auto t1 = CreateText(
		*this,
		"Pick the best habitat for your species\n"
		"Pick wrong and they will die\n"
		"Pick right and they will thrive",
		color::White, 8, font_key, properties
	);
	SetPosition(t1, V2_float{ 0, -50 });
	auto b1 =
		CreateMyButton(*this)
			.SetText("Back", color::Black, {}, font_key)
			.SetFontSize(14)
			.SetTextureKey("back_button")
			.SetButtonTint(color::White)
			.SetButtonTint(color::Gray, ButtonState::Hover)
			.SetButtonTint(color::DarkGray, ButtonState::Pressed)
			.SetSize(V2_float{ 120, 50 })
			.OnActivate([]() { game.scene.Transition<MainMenuScene>("instruction", "main_menu"); });
	SetPosition(b1, V2_float{ 0, 45 });
}

class LoadingScene : public Scene {
public:
	void Enter() override {
		game.renderer.SetGameSize(resolution);
		SetupWindow();
		LoadResources("resources/resources.json");
		levels = ParseEntries(game.json.Get("levels"));
		game.font.SetDefault("mono_font");
		game.music.SetVolume(15);
		// game.sound.SetVolume("rockfly", 15);
		// game.music.Play("elevator_music", -1);
		game.scene.Transition<MainMenuScene>("loading", "main_menu");
	}
};

int main() {
	game.Init("Strange Worlds", resolution);
	game.scene.Enter<LoadingScene>("loading");
	return 0;
}