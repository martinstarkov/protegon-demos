#include "choice.h"
#include "core/script_sequence.h"
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
std::vector<int> unlocked_levels{};

struct Selection {
	bool winner{ false };
	std::string name;
	TextureHandle picture_key;
};

std::vector<Selection> wins{};
constexpr int planet_count{ 3 };

const std::vector<std::string> planet_names = {
	"KA-7", "Q-19", "ZP-3", "MX-8", "NV-4", "TR-6", "LB-2", "CY-9", "HF-1", "WD-5", "JU-0",
	"PS-7", "VK-6", "RA-8", "OX-2", "EG-4", "BN-9", "SI-3", "CL-1", "FT-8", "DR-7", "UG-5",
	"PH-2", "YH-6", "K-42", "A-37", "V-09", "N-73", "R-15", "C-88", "J-60", "M-24", "X-05",
	"B-91", "D-12", "S-66", "T-31", "P-80", "L-27", "G-54", "IX-7", "UR-3", "AE-9", "VO-1",
	"QX-6", "ZK-8", "MY-2", "HN-5", "E-03", "O-17", "U-99", "I-26", "Y-40", "W-58", "F-74",
	"KQ-4", "SR-8", "PL-2", "CZ-7", "GX-1", "RV-6", "TN-9", "BD-3", "LM-5"
};

class MainMenuScene : public Scene {
public:
	void Enter() override;
};

class LevelSelect : public Scene {
public:
	void Enter() override;
};

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

struct PlanetName {
	std::string name;
};

class WinScene : public Scene {
public:
	void Enter() override {
		auto sprite = CreateSprite(*this, "background2");
		SetDrawOrigin(sprite, Origin::Center);

		TextProperties properties;
		properties.justify = TextJustify::Center;
		auto font_key{ "mono_font" };
		auto t1 = CreateText(
			*this, "You won!\nThanks for playing!", color::White, 16, font_key, properties
		);
		SetPosition(t1, V2_float{ 0, -38 });

		std::array<V2_float, 5> positions{ V2_float{ -120, 10 }, V2_float{ -60, 10 },
										   V2_float{ 0, 10 }, V2_float{ 60, 10 },
										   V2_float{ 120, 10 } };

		for (auto i = 0; i < wins.size(); i++) {
			auto planet{ CreateSprite(*this, wins[i].picture_key) };
			SetDrawOrigin(planet, Origin::Center);
			V2_float scale{ 0.5f };
			SetScale(planet, scale);
			PTGN_ASSERT(i < positions.size(), "More wins than positions to display them in");
			SetPosition(planet, positions[i]);
			SetDepth(planet, 2);

			auto glow = CreateSprite(*this, "glow");
			SetScale(glow, scale);
			SetPosition(glow, positions[i]);
			SetDepth(glow, 1);

			auto text{ CreateText(*this, wins[i].name, color::White, 8, font_key) };
			auto size = V2_float{ 60, 60 } * scale;
			SetPosition(text, positions[i] + V2_float{ 0, size.y - 3 });
			SetDepth(text, 2);
			SetDrawOrigin(text, Origin::Center);
		}

		auto b1 = CreateMyButton(*this);
		b1.SetTextureKey("back_button")
			.SetButtonTint(color::White)
			.SetButtonTint(color::Gray, ButtonState::Hover)
			.SetButtonTint(color::DarkGray, ButtonState::Pressed)
			.SetSize(V2_float{ 48, 25 })
			.OnActivate([b1]() mutable {
				b1.Disable();
				unlocked_levels.clear();
				unlocked_levels.push_back(0);
				wins.clear();
				game.scene.Transition<MainMenuScene>("win_scene", "main_menu");
			});
		SetPosition(b1, V2_float{ 0, 67 });
	}
};

struct PlanetScript : public Script<PlanetScript, ButtonScript> {
	Sprite planet_popup;
	Text planet_name;
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
		std::shared_ptr<std::optional<Selection>> selected_level, Button confirm, Text planet_name
	) :
		planet_popup{ planet_popup },
		exit_button{ exit_button },
		planet_buttons{ planet_buttons },
		planet_trait_text{ planet_trait_text },
		human{ human },
		planet_display{ planet_display },
		glows{ glows },
		selected_level{ selected_level },
		confirm{ confirm },
		planet_name{ planet_name } {}

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
		Show(planet_name);
		Show(exit_button);
		exit_button.SetTextureKey("exit_popup_button2");
		exit_button.Enable();
		// PTGN_LOG("Chose planet with traits:");

		auto name{ "Planet " + entity.Get<PlanetName>().name };

		planet_name.SetContent(name);

		std::string planet_traits_content;
		for (const auto& trait : entity.Get<Traits>().traits) {
			planet_traits_content += "- " + trait.description + std::string("\n");
		}

		planet_trait_text.SetContent(planet_traits_content);

		if (entity.Has<Winner>()) {
			*selected_level = Selection{ true, name, Button{ entity }.GetTextureKey() };
			// PTGN_LOG("You picked a winner!");
		} else {
			*selected_level = Selection{ false, name, Button{ entity }.GetTextureKey() };
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
	Text planet_name;
	std::vector<Button> planet_buttons;
	std::vector<Sprite> glows;
	Button exit_button; // exit popup
	Button human;
	Button exit;		// exit scene
	Button confirm;
	Text human_trait_text;
	Text planet_trait_text;
	Sprite planet_display;
	Sprite win_screen;
	Sprite lose_screen;
	Text game_over;

	std::shared_ptr<std::optional<Selection>> selected_level;

	GameScene(int level) : level_{ level } {}

	void Enter() override {
		game_over = CreateText(*this, "GAME OVER", color::Red, 36, "mono_font");
		SetDrawOrigin(game_over, Origin::Center);
		Hide(game_over);
		SetDepth(game_over, 6);

		win_screen = CreateSprite(*this, "win_screen");
		SetDrawOrigin(win_screen, Origin::Center);
		Hide(win_screen);
		SetPosition(win_screen, V2_float{ 0, 0 });
		SetDepth(win_screen, 3);

		lose_screen = CreateSprite(*this, "dead_rocket");
		SetDrawOrigin(lose_screen, Origin::Center);
		Hide(lose_screen);
		SetPosition(lose_screen, V2_float{ 0, -33 });
		SetDepth(lose_screen, 5);

		selected_level	= std::make_shared<std::optional<Selection>>();
		*selected_level = std::nullopt;
		planet_buttons	= {};
		glows			= {};
		PTGN_ASSERT(level_ < levels.size(), "Attempting to enter level which is out of range");

		entry = levels[level_];

		auto dice = RollRoundDice(game.json.Get("traits"), entry);

		PTGN_ASSERT(dice.planets.size() == planet_count);

		std::string human_trait_text_content;
		for (const auto& cat : dice.chosen_categories) {
			PTGN_ASSERT(dice.chosen_human.contains(cat));
			const auto& trait		  = dice.chosen_human.at(cat);
			human_trait_text_content += "- " + trait.get<std::string>() + std::string("\n");
		}
		auto font_key{ "mono_font" };

		TextProperties properties1;
		properties1.wrap_after = static_cast<std::uint32_t>(125.0f * game.renderer.GetScale().x);
		properties1.justify	   = TextJustify::Left;
		human_trait_text =
			CreateText(*this, human_trait_text_content, color::White, 7, font_key, properties1);
		SetPosition(human_trait_text, V2_float{ -80, -42 });
		SetDrawOrigin(human_trait_text, Origin::TopLeft);
		SetDepth(human_trait_text, 4);
		Hide(human_trait_text);

		TextProperties properties2;
		properties2.wrap_after = static_cast<std::uint32_t>(80.0f * game.renderer.GetScale().x);
		properties2.justify	   = TextJustify::Left;
		planet_trait_text =
			CreateText(*this, "Planet Traits", color::White, 5, font_key, properties2);
		SetPosition(planet_trait_text, V2_float{ 5, -40 });
		SetDrawOrigin(planet_trait_text, Origin::TopLeft);
		SetDepth(planet_trait_text, 4);
		Hide(planet_trait_text);

		// input.SetDrawInteractives(true);

		// PTGN_LOG("Entering level ", level_);

		auto sprite = CreateSprite(*this, "background");
		SetDrawOrigin(sprite, Origin::Center);

		float x_offset{ 92.0f };

		planet_popup = CreateSprite(*this, "planet_popup");
		SetDrawOrigin(planet_popup, Origin::Center);
		Hide(planet_popup);
		SetPosition(planet_popup, V2_float{ 0, -1 });
		SetDepth(planet_popup, 3);

		planet_name = CreateText(*this, "Temporary Name", color::White, 9, font_key);
		SetDrawOrigin(planet_name, Origin::Center);
		Hide(planet_name);
		SetPosition(
			planet_name, V2_float{ 0, -1 } - V2_float{ 183, 137 } / 2.0f + V2_float{ 55, 23 }
		);
		SetDepth(planet_name, 4);

		planet_display = CreateSprite(*this, "planet_1");
		SetDrawOrigin(planet_display, Origin::Center);
		Hide(planet_display);
		SetPosition(
			planet_display, V2_float{ 0, -1 } - V2_float{ 183, 137 } / 2.0f + V2_float{ 53, 72 }
		);
		SetDepth(planet_display, 4);

		confirm		= CreateMyButton(*this);
		exit_button = CreateMyButton(*this);
		exit		= CreateMyButton(*this);

		const int planet_texture_count = 11;

		std::vector<int> v(planet_texture_count);

		std::iota(v.begin(), v.end(), 1);

		auto sample = random_sample(v, planet_count);

		PTGN_ASSERT(planet_count == sample.size());

		auto names = random_sample(planet_names, planet_count);
		PTGN_ASSERT(names.size() == planet_count);

		for (auto i = 0; i < planet_count; i++) {
			auto name	= names[i];
			auto button = CreateButton(*this);
			button.Add<PlanetName>(name);
			V2_float planet_pos{ -x_offset + i * x_offset, 0.0f };
			// TODO: Pick randomly from a list of planet textures.
			auto glow = CreateSprite(*this, "glow");
			SetPosition(glow, planet_pos);
			SetDepth(glow, 1);
			auto texture_key{ "planet_" + std::to_string(sample[i]) };
			PTGN_ASSERT(game.texture.Has(texture_key));
			auto planet = button.SetTextureKey(texture_key)
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

		human = CreateMyButton(*this);

		for (auto button : planet_buttons) {
			AddScript<PlanetScript>(
				button, planet_popup, exit_button, planet_buttons, planet_trait_text, human,
				planet_display, glows, selected_level, confirm, planet_name
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

		SetDepth(exit_button, 4);
		exit_button.SetTextureKey("exit_popup_button")
			.SetButtonTint(color::White)
			.SetButtonTint(color::Gray, ButtonState::Hover)
			.SetButtonTint(color::DarkGray, ButtonState::Pressed)
			.SetSize(V2_float{ 23, 9 })
			.OnActivate([=]() mutable {
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
				Hide(planet_name);
				Hide(planet_display);
				Hide(confirm);
				confirm.Disable();
				Hide(exit_button);
				exit_button.Disable();
			});
		Hide(exit_button);
		exit_button.Disable();
		SetPosition(
			exit_button, V2_float{ 0, -1 } - V2_float{ 183, 137 } / 2.0f + V2_float{ 56, 7 }
		);

		TextProperties human_text_properties;
		human_text_properties.style = FontStyle::Bold;
		human.SetText("Species Traits", color::White, {}, "mono_font", human_text_properties)
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
				exit_button.SetTextureKey("exit_popup_button");
				Show(human_trait_text);
				human.Disable();
				Hide(human);
				Show(human_popup);
				Show(exit_button);
				exit_button.Enable();
			});
		SetPosition(human, V2_float{ -38, 84 });

		confirm.SetTextureKey("confirm_button")
			.SetButtonTint(color::White)
			.SetButtonTint(color::LightGray, ButtonState::Hover)
			.SetButtonTint(color::DarkGray, ButtonState::Pressed)
			.SetSize(V2_float{ 47, 14 })
			.OnActivate([&]() mutable {
				PTGN_ASSERT(selected_level->has_value(), "Cannot confirm without selection");
				// PTGN_LOG("Confirmed: ", (*selected_level)->winner ? "Winner" : "Loser");
				human.Disable();
				exit.Disable();
				confirm.Disable();
				exit_button.Disable();
				Hide(exit_button);
				Hide(exit);
				if ((*selected_level)->winner) {
					milliseconds fade_duration{ 1000 };
					milliseconds break_duration{ 100 };
					milliseconds translate_duration{ 1000 };
					milliseconds break2_duration{ 2000 };
					milliseconds fade2_duration{ 2000 };
					milliseconds break3_duration{ 200 };
					milliseconds predid_break{ 100 };
					milliseconds fade_in_duration{ 500 };
					FadeOut(confirm, fade_duration);
					FadeOut(planet_trait_text, fade_duration);
					FadeOut(planet_popup, fade_duration);
					FadeOut(planet_name, fade_duration);
					After(*this, fade_duration + break_duration, [=](auto t) {
						TranslateTo(
							planet_display,
							V2_float{ -center } + V2_float{ 138, 64 } + V2_float{ 30 },
							translate_duration
						);
					});
					After(
						*this, fade_duration + break_duration + translate_duration + predid_break,
						[=](auto t) {
							Show(win_screen);
							SetTint(win_screen, color::Transparent);
							FadeIn(win_screen, fade_in_duration);
						}
					);
					After(
						*this,
						fade_duration + break_duration + translate_duration + predid_break +
							fade_in_duration + break2_duration,
						[=](auto t) {
							FadeOut(planet_display, fade2_duration);
							FadeOut(win_screen, fade2_duration);
						}
					);
					After(
						*this,
						fade_duration + break_duration + translate_duration + predid_break +
							fade_in_duration + break2_duration + fade2_duration + break3_duration,
						[=](auto t) {
							if (level_ == levels.size() - 1) {
								game.scene.Transition<WinScene>("game", "win_scene");
							} else {
								game.scene.Transition<LevelSelect>("game", "level_select");
							}
						}
					);
					wins.push_back(*(*selected_level));
					unlocked_levels.push_back(level_ + 1);
				} else {
					milliseconds fade_duration{ 1000 };
					milliseconds break_duration{ 100 };
					milliseconds translate_duration{ 1000 };
					milliseconds break2_duration{ 2000 };
					milliseconds fade2_duration{ 2000 };
					milliseconds fade_game_over_duration{ 2000 };
					milliseconds break3_duration{ 200 };
					milliseconds predid_break{ 100 };
					milliseconds fade_in_duration{ 500 };
					FadeOut(confirm, fade_duration);
					FadeOut(planet_trait_text, fade_duration);
					FadeOut(planet_popup, fade_duration);
					FadeOut(planet_name, fade_duration);
					After(*this, fade_duration + break_duration, [=](auto t) {
						TranslateTo(planet_display, V2_float{}, translate_duration);
					});
					After(
						*this, fade_duration + break_duration + translate_duration + predid_break,
						[=](auto t) {
							Show(lose_screen);
							SetTint(lose_screen, color::Transparent);
							FadeIn(lose_screen, fade_in_duration);
						}
					);
					After(
						*this,
						fade_duration + break_duration + translate_duration + predid_break +
							fade_in_duration,
						[=](auto t) {
							Show(game_over);
							SetTint(game_over, color::Transparent);
							SetScale(game_over, V2_float{ 0.01f });
							FadeIn(game_over, fade_game_over_duration);
							ScaleTo(
								game_over, V2_float{ 1.2f }, fade_game_over_duration,
								AsymmetricalEase::InQuad
							);
							/*game.sound.Play("lose_sound");*/
						}
					);
					After(
						*this,
						fade_duration + break_duration + translate_duration + predid_break +
							fade_in_duration + fade_game_over_duration + break2_duration,
						[=](auto t) {
							FadeOut(planet_display, fade2_duration);
							FadeOut(lose_screen, fade2_duration);
							FadeOut(game_over, fade2_duration);
						}
					);
					After(
						*this,
						fade_duration + break_duration + translate_duration + predid_break +
							fade_in_duration + fade_game_over_duration + break2_duration +
							fade2_duration + milliseconds{ 100 },
						[=](auto t) { game.scene.Transition<LevelSelect>("game", "level_select"); }
					);
				}
			});
		SetPosition(confirm, V2_float{ 0, -1 } - V2_float{ 183, 137 } / 2.0f + V2_float{ 52, 121 });
		SetDepth(confirm, 4);
		Hide(confirm);
		confirm.Disable();

		exit.SetTextureKey("exit_button")
			.SetButtonTint(color::White)
			.SetButtonTint(color::Gray, ButtonState::Hover)
			.SetButtonTint(color::DarkGray, ButtonState::Pressed)
			.SetSize(V2_float{ 23, 13 })
			.OnActivate([exit_game_scene = exit]() mutable {
				exit_game_scene.Disable();
				game.scene.Transition<LevelSelect>("game", "level_select");
			});
		SetDrawOrigin(exit, Origin::BottomRight);
		SetPosition(exit, center - V2_float{ 3 });
	}
};

class InstructionScene : public Scene {
public:
	void Enter() override;

	void Update() override;
};

void MainMenuScene::Enter() {
	auto sprite = CreateSprite(*this, "title");
	SetDrawOrigin(sprite, Origin::Center);
	auto button = CreateMyButton(*this);
	button.SetTextureKey("play_button")
		.SetButtonTint(color::White)
		.SetButtonTint(color::Gray, ButtonState::Hover)
		.SetButtonTint(color::DarkGray, ButtonState::Pressed)
		.SetSize(V2_float{ 48, 25 })
		.OnActivate([button]() mutable {
			button.Disable();
			game.scene.Transition<LevelSelect>("main_menu", "level_select");
		});
	SetDrawOrigin(button, Origin::TopLeft);
	SetPosition(button, V2_float{ 41, 113 } - center);
	auto button2 = CreateMyButton(*this);
	button2.SetTextureKey("instructions_button")
		.SetButtonTint(color::White)
		.SetButtonTint(color::Gray, ButtonState::Hover)
		.SetButtonTint(color::DarkGray, ButtonState::Pressed)
		.SetSize(V2_float{ 78, 14 })
		.OnActivate([button2]() mutable {
			button2.Disable();
			game.scene.Transition<InstructionScene>("main_menu", "instruction");
		});
	SetDrawOrigin(button2, Origin::TopLeft);
	SetPosition(button2, V2_float{ 136, 130 } - center);
}

void LevelSelect::Enter() {
	auto sprite = CreateSprite(*this, "background");
	SetDrawOrigin(sprite, Origin::Center);

	for (int i{ 0 }; i < 5; ++i) {
		auto button = CreateMyButton(*this);
		button.SetTextureKey("square_button")
			.SetDisabledTextureKey("square_button_disabled")
			.SetButtonTint(color::White)
			.SetButtonTint(color::Gray, ButtonState::Hover)
			.SetButtonTint(color::DarkGray, ButtonState::Pressed)
			.SetSize(V2_float{ 32 })
			.OnActivate([i, button]() mutable {
				game.scene.Transition<GameScene>("level_select", "game", i);
			});
		SetPosition(button, V2_float{ -120 + i * 60, -20 });

		if (!VectorContains(unlocked_levels, i)) {
			button.Disable();
		} else {
			button.SetText(std::to_string(i + 1), color::White, {}, "mono_font").SetFontSize(10);
		}
	}

	auto back = CreateMyButton(*this);
	back.SetTextureKey("back_button")
		.SetButtonTint(color::White)
		.SetButtonTint(color::Gray, ButtonState::Hover)
		.SetButtonTint(color::DarkGray, ButtonState::Pressed)
		.SetSize(V2_float{ 48, 25 })
		.OnActivate([back]() mutable {
			back.Disable();
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
	properties.wrap_after =
		static_cast<std::uint32_t>((float)resolution.x * game.renderer.GetScale().x * 0.9f);
	properties.justify = TextJustify::Left;
	auto font_key{ "mono_font" };
	auto t1 = CreateText(
		*this,
		"Strange worlds await our sort-of human explorers...\n\n"
		"They are searching for a new home! \n\n"
		"These species may be a bit different than you or me, but they need a planet that "
		"suits "
		"them just as well as Earth suits us.\n\nHelp each race of intergalactic travelers "
		"select "
		"from three possible destination planets by carefully weighing the worlds' traits "
		"against "
		"their own. \n\nUse your logic and choose wisely!",
		color::White, 6, font_key, properties
	);
	SetPosition(t1, V2_float{ 0, -12 });
	auto b1 = CreateMyButton(*this);
	b1.SetTextureKey("back_button")
		.SetButtonTint(color::White)
		.SetButtonTint(color::Gray, ButtonState::Hover)
		.SetButtonTint(color::DarkGray, ButtonState::Pressed)
		.SetSize(V2_float{ 48, 25 })
		.OnActivate([b1]() mutable {
			b1.Disable();
			game.scene.Transition<MainMenuScene>("instruction", "main_menu");
		});
	SetPosition(b1, V2_float{ 0, 67 });
}

class LoadingScene : public Scene {
public:
	void Enter() override {
		game.window.SetSize(resolution * 4);
		game.renderer.SetScalingMode(ScalingMode::IntegerScale);
		game.renderer.SetGameSize(resolution);

		LoadResources("resources/resources.json");
		levels = ParseEntries(game.json.Get("levels"));
		unlocked_levels.push_back(0);
		game.font.SetDefault("mono_font");
		game.music.SetVolume(2);
		game.sound.SetVolume("low_beep", 15);
		game.sound.SetVolume("high_beep", 15);
		game.music.Play("music1_music", -1);
		// game.music.Play("elevator_music", -1);
		game.scene.Transition<MainMenuScene>("loading", "main_menu");
	}
};

int main() {
	game.Init("The Last Habitat", resolution);
	game.scene.Enter<LoadingScene>("loading");
	return 0;
}