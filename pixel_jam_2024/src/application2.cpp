#include "protegon/protegon.h"

using namespace ptgn;

const V2_int grid_size{ 40, 23 };
const V2_int tile_size{ 16, 16 };
const V2_float scale{ 2.0f };

class LevelSelectScene : public Scene {
public:
  LevelSelectScene() {}

  void Init() override {}
  void Shutdown() override {}
  void Update() override {}
};

class MainMenuScene : public Scene {
public:
  Button play;

  MainMenuScene() {
    game.music.Load("theme", Music{ "resources/music/aqualife_theme.mp3" });
    game.music.Load("ocean", Music{ "resources/music/ocean_loop.mp3" });
    game.font.SetDefault(Font{ "resources/font/retro_gaming.ttf" });

    play.Set<ButtonProperty::Texture>(Texture{ "resources/ui/play.png" });
    play.Set<ButtonProperty::Text>(Text{ "Play", color::White });
    play.Set<ButtonProperty::TextColor>(color::White);
    play.Set<ButtonProperty::TextColor>(color::Gold, ButtonState::Hover);
    play.Set<ButtonProperty::TextSize>(play.Get<ButtonProperty::Texture>().GetSize() / 2.0f);
    play.SetRect(Rect{ game.window.GetCenter() + V2_int{ 0, 50 }, play.Get<ButtonProperty::Texture>().GetSize() / 1.5f, Origin::Center });
    play.Set<ButtonProperty::OnActivate>([](){
      game.scene.Load<LevelSelectScene>("level_select");
      game.scene.TransitionActive("menu", "level_select");
    });
  }

  void Init() override {
    play.Enable();
    game.music.Stop();
    game.music.Get("ocean").Play(-1);
    game.music.Get("theme").Play(-1);
  }

  void Shutdown() override {
    play.Enable();
  }

  void Update() override {
    Texture{ "resources/ui/start_background.png" }.Draw();
    play.Draw();
  }
};

class SetupScene : public Scene {
public:
  SetupScene() {}

  void Init() override {
    game.window.SetTitle("Aqualife");
    game.window.SetSize(grid_size * tile_size * scale);
    game.window.SetSetting(WindowSetting::Resizable);
    game.window.SetSetting(WindowSetting::Maximized);
    game.scene.Load<MainMenuScene>("main_menu");
    game.scene.AddActive("main_menu");
  }
};

int main() {
  game.Start<SetupScene>();
  return 0;
}
