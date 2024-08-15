#include "protegon/protegon.h"

using namespace ptgn;

class Paint : public Scene {
public:
	Grid<int> outer_grid{ { 40 * 2, 30 * 2 } };
	Grid<int> inner_grid{ { 40 * 2, 30 * 2 } };
	Grid<int> grid{ { 40 * 2, 30 * 2 } };
	Paint() {
		game.window.SetTitle("paint: left click to draw; right click to erase; B to flip color");
		game.window.SetSize({ 720, 720 });
		outer_grid.Fill(0);
	}
	V2_int tile_size{ 20, 20 };
	bool toggle = true;
	void Update(float dt) final {

		std::vector<int> cells_without;
		cells_without.resize(outer_grid.GetLength(), -1);
		outer_grid.ForEachIndex([&](std::size_t index) {
			int value = outer_grid.Get(index);
			if (value != 1)
				cells_without[index] = value;
		});
		inner_grid = Grid<int>{ outer_grid.GetSize(), cells_without };
		if (game.input.KeyDown(Key::B)) toggle = !toggle;
		if (toggle) {
			grid = outer_grid;
		} else {
			grid = inner_grid;
		}


		V2_int mouse_pos = game.input.GetMousePosition();
		V2_int mouse_tile = mouse_pos / tile_size;
		Rectangle<int> mouse_box{ mouse_tile* tile_size, tile_size };

		if (grid.Has(mouse_tile)) {
			if (game.input.MousePressed(Mouse::LEFT)) {
				outer_grid.Set(mouse_tile, 1);
			}
			if (game.input.MousePressed(Mouse::RIGHT)) {
				outer_grid.Set(mouse_tile, 0);
			}
		}

		grid.ForEachCoordinate([&](const V2_int& p) {
			Color c = color::Red;
			Rectangle<int> r{ V2_int{ p.x * tile_size.x, p.y * tile_size.y }, tile_size };
			if (grid.Has(p)) {
				switch (grid.Get(p)) {
					case 0:
						c = color::Grey;
						break;
					case 1:
						c = color::Green;
						break;
				}
			}
			r.DrawSolid(c);
		});
		if (grid.Has(mouse_tile)) {
			mouse_box.Draw(color::Yellow);
		}
	}
};

int main(int c, char** v) {
	ptgn::game::Start<Paint>();
	return 0;
}
