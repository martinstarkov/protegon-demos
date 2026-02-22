#include "math/geometry/circle.h"
#include "protegon/protegon.h"
#include "renderer/api/origin.h"

using namespace ptgn;

constexpr V2_int resolution{ 320, 180 };
constexpr V2_float center{ resolution / 2.0f };
constexpr V2_int world_size{ 320, 180 };

void SetupWindow() {
	game.window.SetSize(resolution * 4);
	game.renderer.SetScalingMode(ScalingMode::IntegerScale);
	// game.window.SetSetting(WindowSetting::Maximized);
}

class GameScene : public Scene {
public:
	void Enter() override {}
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

	void OnButtonHover() override {
		game.sound.Play("low_beep");
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
						  .SetText("Play", color::Black)
						  .SetFontSize(24)
						  .SetTextureKey("main_button")
						  .SetButtonTint(color::White)
						  .SetButtonTint(color::Gray, ButtonState::Hover)
						  .SetButtonTint(color::DarkGray, ButtonState::Pressed)
						  .SetSize(V2_float{ 100, 50 })
						  .OnActivate([]() {
							  game.scene.Transition<LevelSelect>("main_menu", "level_select");
						  });
		SetPosition(button, V2_float{ -100, 0 });
		auto button2 = CreateMyButton(*this)
						   .SetText("Instructions", color::Black)
						   .SetFontSize(24)
						   .SetTextureKey("main_button")
						   .SetButtonTint(color::White)
						   .SetButtonTint(color::Gray, ButtonState::Hover)
						   .SetButtonTint(color::DarkGray, ButtonState::Pressed)
						   .SetSize(V2_float{ 100, 50 })
						   .OnActivate([]() {
							   game.scene.Transition<InstructionScene>("main_menu", "instruction");
						   });

		SetPosition(button2, V2_float{ 100, 0 });
	}
};

void LevelSelect::Enter() {
	auto sprite = CreateSprite(*this, "level_select_bg");
	SetDrawOrigin(sprite, Origin::Center);
	auto b1 =
		CreateMyButton(*this)
			.SetText("1", color::Black)
			.SetFontSize(48)
			.SetTextureKey("square_button")
			.SetButtonTint(color::White)
			.SetButtonTint(color::Gray, ButtonState::Hover)
			.SetButtonTint(color::DarkGray, ButtonState::Pressed)
			.SetSize(V2_float{ 125 })
			.OnActivate([]() { game.scene.Transition<GameScene>("level_select", "game", {}, 0); });
	SetPosition(b1, V2_float{ -300, 0 });
	auto b2 =
		CreateMyButton(*this)
			.SetText("2", color::Black)
			.SetFontSize(48)
			.SetTextureKey("square_button")
			.SetButtonTint(color::White)
			.SetButtonTint(color::Gray, ButtonState::Hover)
			.SetButtonTint(color::DarkGray, ButtonState::Pressed)
			.SetSize(V2_float{ 125 })
			.OnActivate([]() { game.scene.Transition<GameScene>("level_select", "game", {}, 1); });
	SetPosition(b2, V2_float{ 0, 0 });
	auto b3 =
		CreateMyButton(*this)
			.SetText("3", color::Black)
			.SetFontSize(48)
			.SetTextureKey("square_button")
			.SetButtonTint(color::White)
			.SetButtonTint(color::Gray, ButtonState::Hover)
			.SetButtonTint(color::DarkGray, ButtonState::Pressed)
			.SetSize(V2_float{ 125 })
			.OnActivate([]() { game.scene.Transition<GameScene>("level_select", "game", {}, 2); });
	SetPosition(b3, V2_float{ 300, 0 });
	auto b4 = CreateMyButton(*this)
				  .SetText("Back", color::Black)
				  .SetFontSize(36)
				  .SetTextureKey("button")
				  .SetButtonTint(color::White)
				  .SetButtonTint(color::Gray, ButtonState::Hover)
				  .SetButtonTint(color::DarkGray, ButtonState::Pressed)
				  .SetSize(V2_float{ 300, 100 })
				  .OnActivate([]() {
					  game.scene.Transition<MainMenuScene>("level_select", "main_menu");
				  });
	SetPosition(b4, V2_float{ 0, 280 });
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
	ResourceHandle font_key{ "text_font" };
	auto t1 = CreateText(
		*this,
		"You are God, forging the foundations of existence.\n\n In your hands are disks "
		"representing a point in a cycle.\n\n Your goal is to place them in the correct order, "
		"forming stable cycles that define the laws and rhythms of the universe.\n\n Ring the "
		"bell "
		"when ready.",
		color::White, 36, font_key, properties
	);
	SetPosition(t1, V2_float{ 0, -50 });
	auto b1 =
		CreateMyButton(*this)
			.SetText("Back", color::Black)
			.SetFontSize(36)
			.SetTextureKey("button")
			.SetButtonTint(color::White)
			.SetButtonTint(color::Gray, ButtonState::Hover)
			.SetButtonTint(color::DarkGray, ButtonState::Pressed)
			.SetSize(V2_float{ 300, 100 })
			.OnActivate([]() { game.scene.Transition<MainMenuScene>("instruction", "main_menu"); });
	SetPosition(b1, V2_float{ 0, 280 });
}

class LoadingScene : public Scene {
public:
	void Enter() override {
		game.renderer.SetGameSize(resolution);
		SetupWindow();
		LoadResources("resources/resources.json");
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