#include "protegon/protegon.h"

using namespace ptgn;

class StartScreen;
class GameScene;

class GameScene : public Scene {
public:
	Surface test_map{ "resources/maps/test_map.png" };
	V2_int grid_size{ 30, 15 };
	V2_int tile_size{ 32, 32 };
	ecs::Manager manager;
	json j;

	GameScene() {

		/*music::Unmute();
		music::Load(Hash("in_game"), "resources/music/in_game.wav");
		music::Get(Hash("in_game"))->Play(-1);*/

		// Load json data.
		/*std::ifstream f{ "resources/data/level_data.json" };
		if (f.fail())
			f = std::ifstream{ GetAbsolutePath("resources/data/level_data.json") };
		assert(!f.fail() && "Failed to load json file");
		j = json::parse(f);

		levels = j["levels"].size();*/

		// Load textures.
		texture::Load(500, "resources/tile/wall.png");
	}
	void Update(float dt) final {

	}
};


//class StartScreen : public Scene {
//public:
//	//Text text0{ Hash("0"), "Stroll of the Dice", color::CYAN };
//	
//	TexturedButton play{ {}, Hash("play"), Hash("play_hover"), Hash("play_hover") };
//	Color play_text_color{ color::WHITE };
//
//	StartScreen() {
//		texture::Load(Hash("play"), "resources/ui/play.png");
//		texture::Load(Hash("play_hover"), "resources/ui/play_hover.png");
//		music::Mute();
//	}
//	void Update(float dt) final {
//		music::Mute();
//		V2_int window_size{ window::GetLogicalSize() };
//		Rectangle<float> bg{ {}, window_size };
//		texture::Get(2)->Draw(bg);
//
//		V2_int play_texture_size = play.GetCurrentTexture().GetSize();
//
//		play.SetRectangle({ window_size / 2 - play_texture_size / 2, play_texture_size });
//
//		V2_int play_text_size{ 220, 80 };
//		V2_int play_text_pos = window_size / 2 - play_text_size / 2;
//		play_text_pos.y += 20;
//		
//		Color text_color = color::WHITE;
//
//		auto play_press = [&]() {
//			sound::Get(Hash("click"))->Play(3, 0);
//			scene::Load<GameScene>(Hash("game"));
//			scene::SetActive(Hash("game"));
//		};
//
//		play.SetOnActivate(play_press);
//		play.SetOnHover([&]() {
//			play_text_color = color::GOLD;
//		}, [&]() {
//			play_text_color = color::WHITE;
//		});
//
//        if (input::KeyDown(Key::SPACE)) {
//			play_press();
//		}
//
//		play.Draw();
//
//		Text t3{ Hash("2"), "Tower Offense", color::DARK_GREEN };
//		t3.Draw({ play_text_pos - V2_int{ 250, 160 }, { play_text_size.x + 500, play_text_size.y } });
//
//		Text t{ Hash("2"), "Play", play_text_color };
//		t.Draw({ play_text_pos, play_text_size });
//	}
//};

class PixelJam2024 : public Scene {
public:
	PixelJam2024() {
		// Setup window configuration.
		window::SetTitle("Aqualife");
		window::SetSize({ 1080, 720 }, true);
		window::SetColor(color::CYAN);
		window::SetResizeable(true);
		window::Maximize();
		window::SetLogicalSize({ 960, 480 });

		font::Load(Hash("04B_30"), "resources/font/04B_30.ttf", 32);
		font::Load(Hash("retro_gaming"), "resources/font/retro_gaming.ttf", 32);
		//scene::Load<StartScreen>(Hash("menu"));
		scene::Load<GameScene>(Hash("game"));
		scene::SetActive(Hash("game"));
	}
};

int main(int c, char** v) {
	ptgn::game::Start<PixelJam2024>();
	return 0;
}
