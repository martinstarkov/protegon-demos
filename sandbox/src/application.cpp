#include "protegon/protegon.h"

using namespace ptgn;

struct RectangleComponent {
	RectangleComponent(const V2_int& pos, const V2_int& size) : r{ pos, size } {}
	Rectangle<float> r;
};

struct VelocityComponent {
	VelocityComponent(const V2_float& velocity) : v{ velocity } {}
	V2_float v;
};


class SandboxScene : public Scene {
public:
	Texture t;
	RNG<int> rng_x{ 0, 1280 };
	RNG<int> rng_y{ 0, 720 };
	RNG<int> rng_xs{ 1, 32 };
	RNG<int> rng_ys{ 1, 32 };
	ecs::Manager manager;
	RNG<int> move{ -1, 1 };
	std::vector<Rectangle<float>> positions;
	Timer timer;
	V2_int w_size;
	V2_int tile_size{ 8, 8 };
	V2_int grid_size{ 40, 23 };

	ecs::Entity moving;
	ecs::Entity main_tl;
	ecs::Entity main_tr;
	ecs::Entity main_bl;
	ecs::Entity main_br;

	SandboxScene() {
		game.window.SetColor(color::Black);

		t = texture::Load(Hash("test"), "resources/tile/thick_nochoice.png");

		V2_int resolution{ 1280, 720 };
		V2_int minimum_resolution{ 640, 360 };
		V2_float scale{ 4.0f, 4.0f };
		bool fullscreen = false;
		bool borderless = false;
		bool resizeable = true;

		game.window.SetupSize(resolution, minimum_resolution, fullscreen, borderless, resizeable, scale);

		game.window.SetScale({ 4.0f, 4.0f });

		//tileset::Create("h1", 4, 4, 44, 20, 32, 32)

		moving = manager.CreateEntity();
		main_tl = manager.CreateEntity();
		main_tr = manager.CreateEntity();
		main_bl = manager.CreateEntity();
		main_br = manager.CreateEntity();

		moving.Add<RectangleComponent>(tile_size * V2_int{ 0, (grid_size.y - 1) / 2 }, tile_size);
		moving.Add<VelocityComponent>(V2_float{ 5.0f, 0.0f });

		main_tl.Add<RectangleComponent>(scale * tile_size * V2_int{ 0, 0 },                             tile_size);
		main_tr.Add<RectangleComponent>(tile_size * V2_int{ grid_size.x - 1, 0 },               tile_size);
		main_bl.Add<RectangleComponent>(tile_size * V2_int{ 0, grid_size.y - 1 },               tile_size);
		main_br.Add<RectangleComponent>(tile_size * V2_int{ grid_size.x - 1, grid_size.y - 1 }, tile_size);

		manager.Refresh();

	}
	void Update(float dt) final {

		V2_int size = game.window.GetResolution();
		Rectangle<float> r{ {}, size };
		r.DrawSolid(color::Grey);

		auto r1 = main_tl.Get<RectangleComponent>().r;
		auto r2 = main_tr.Get<RectangleComponent>().r;
		auto r3 = main_bl.Get<RectangleComponent>().r;
		auto r4 = main_br.Get<RectangleComponent>().r;

		/*r1.pos *= scale;
		r2.pos *= scale;
		r3.pos *= scale;
		r4.pos *= scale;

		r1.size *= scale;
		r2.size *= scale;
		r3.size *= scale;
		r4.size *= scale;*/

		r1.DrawSolid(color::Red);
		r2.DrawSolid(color::Red);
		r3.DrawSolid(color::Red);
		r4.DrawSolid(color::Red);

		//Rectangle<float> rectangle{ { 0, 0 }, { 32, 32 } };

		//rectangle.DrawSolid(color::Pink);

		moving.Get<RectangleComponent>().r.pos += moving.Get<VelocityComponent>().v * dt;

		auto r5 = moving.Get<RectangleComponent>().r;

		/*r5.pos *= scale;
		r5.size *= scale;*/

		r5.DrawSolid(color::Blue);

		//game.window.Test(r5);
	}
};

int main() {
	ptgn::game::Start<SandboxScene>();
	return 0;
}
