#include "math/geometry/circle.h"
#include "protegon/protegon.h"
#include "renderer/api/origin.h"

using namespace ptgn;

constexpr V2_int resolution{ 320, 180 };
constexpr V2_float center{ resolution / 2.0f };
constexpr V2_int world_size{ 320, 180 };
constexpr int button_channel{ 2 };

void SetupWindow() {
	game.window.SetSize(resolution * 4);
	game.renderer.SetScalingMode(ScalingMode::IntegerScale);
	// game.window.SetSetting(WindowSetting::Maximized);
}

class GameScene : public Scene {
public:
	int level_{ 0 };

	GameScene(int level) : level_{ level } {}

	void Enter() override {
		PTGN_LOG("Entering level ", level_);
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

class MainMenuScene : public Scene {
public:
	void Enter() override {
		auto sprite = CreateSprite(*this, "background");
		SetDrawOrigin(sprite, Origin::Center);
		auto button = CreateMyButton(*this)
						  .SetText("Play", color::Black, {}, "mono_font")
						  .SetFontSize(14)
						  .SetTextureKey("main_button")
						  .SetButtonTint(color::White)
						  .SetButtonTint(color::Gray, ButtonState::Hover)
						  .SetButtonTint(color::DarkGray, ButtonState::Pressed)
						  .SetSize(V2_float{ 120, 50 })
						  .OnActivate([]() {
							  game.scene.Transition<LevelSelect>("main_menu", "level_select");
						  });
		SetPosition(button, V2_float{ -70, 0 });
		auto button2 = CreateMyButton(*this)
						   .SetText("Instructions", color::Black, {}, "mono_font")
						   .SetFontSize(14)
						   .SetTextureKey("main_button")
						   .SetButtonTint(color::White)
						   .SetButtonTint(color::Gray, ButtonState::Hover)
						   .SetButtonTint(color::DarkGray, ButtonState::Pressed)
						   .SetSize(V2_float{ 120, 50 })
						   .OnActivate([]() {
							   game.scene.Transition<InstructionScene>("main_menu", "instruction");
						   });

		SetPosition(button2, V2_float{ 70, 0 });
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
	auto t1 = CreateText(*this, "Write\nStuff\nHere", color::White, 10, font_key, properties);
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