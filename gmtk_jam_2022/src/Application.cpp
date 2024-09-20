#include <array>
#include <numeric>
#include <queue>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "protegon/protegon.h"

using namespace ptgn;

template <typename T>
inline bool Contains(const std::vector<T>& container, const T& value) {
	for (const auto& v : container) {
		if (v == value) {
			return true;
		}
	}
	return false;
}

enum class TileType {
	NONE	 = 0,
	PLAYER	 = 1,
	USED	 = 2,
	WIN		 = 3,
	OBSTACLE = 4
};

struct Tile {
	TileType type{ TileType::NONE };
};

struct CustomGrid {
	CustomGrid(const V2_int& size, const V2_int& tile_size) :
		size{ size }, tile_size{ tile_size } {}

	bool InBound(const V2_int& coordinate) const {
		return coordinate.x < size.x && coordinate.x >= 0 && coordinate.y < size.y &&
			   coordinate.y >= 0;
	}

	void AddTile(const V2_int& coordinate, const Tile& tile) {
		assert(InBound(coordinate));
		tiles.emplace(coordinate, tile);
	}

	void AddTiles(const std::vector<V2_int>& sequence, const Tile& tile) {
		for (auto i = 0; i < sequence.size() - 1; ++i) {
			AddTile(sequence[i], tile);
		}
	}

	bool Permits(const std::vector<V2_int>& sequence, const std::vector<TileType>& ignore) const {
		for (const auto& coordinate : sequence) {
			// If tile is being used or if tile is out of bounds.
			auto it = tiles.find(coordinate);
			if (!InBound(coordinate) || (it != tiles.end() && !Contains(ignore, it->second.type))) {
				return false;
			}
		}
		return true;
	}

	bool WinCondition(const std::vector<V2_int>& sequence) const {
		for (const auto& coordinate : sequence) {
			auto it = tiles.find(coordinate);
			if (it != tiles.end() && it->second.type == TileType::WIN) {
				return true;
			}
		}
		return false;
	}

	bool HasTile(const V2_int& coordinate, const std::vector<TileType>& types = {}) const {
		assert(InBound(coordinate));
		auto it = tiles.find(coordinate);
		return it != tiles.end() && (types.size() == 0 ? true : Contains(types, it->second.type));
	}

	const Tile& GetTile(const V2_int& coordinate) const {
		assert(InBound(coordinate));
		auto it = tiles.find(coordinate);
		assert(it != tiles.end());
		return it->second;
	}

	const V2_int& GetSize() const {
		return size;
	}

	const V2_int& GetTileSize() const {
		return tile_size;
	}

	void Clear() {
		tiles.clear();
	}

private:
	V2_int tile_size;
	V2_int size;
	std::unordered_map<V2_int, Tile> tiles;
};

inline V2_int ClosestAxis(const V2_float& direction) {
	auto dir{ direction.Dot(V2_float{ 1, 0 }) };
	V2_int axis{ 1, 0 };
	auto temp_dir{ direction.Dot(V2_float{ -1, 0 }) };
	if (temp_dir > dir) {
		dir	 = temp_dir;
		axis = { -1, 0 };
	}
	temp_dir = direction.Dot(V2_float{ 0, 1 });
	if (temp_dir > dir) {
		dir	 = temp_dir;
		axis = { 0, 1 };
	}
	temp_dir = direction.Dot(V2_float{ 0, -1 });
	if (temp_dir > dir) {
		dir	 = temp_dir;
		axis = { 0, -1 };
	}
	return axis;
}

using Sequence	 = std::vector<V2_int>;
using Directions = std::vector<V2_int>;

Sequence GetRandomRollSequence(std::size_t count) {
	Sequence sequence;
	sequence.push_back({});
	std::array<V2_int, 4> directions{ V2_int{ 1, 0 }, V2_int{ -1, 0 }, V2_int{ 0, 1 },
									  V2_int{ 0, -1 } };
	V2_int previous_direction{ directions[0] };
	sequence.push_back(previous_direction);
	RNG<int> rng{ 0, 3 };
	for (auto i = 0; i < count - 1; ++i) {
	start:
		auto dir			   = rng();
		auto current_direction = directions[dir] + sequence.back();
		if (directions[dir] != -previous_direction && !Contains(sequence, current_direction)) {
			sequence.emplace_back(current_direction);
			previous_direction = directions[dir];
		} else {
			goto start;
		}
	}
	sequence.erase(sequence.begin());
	return sequence;
}

Sequence GetRotatedSequence(Sequence sequence, const float angle) {
	for (auto& vector : sequence) {
		vector = V2_int{ Round(vector.Rotated(angle)) };
	}
	return sequence;
}

Sequence GetAbsoluteSequence(Sequence sequence, const V2_int& tile) {
	for (auto& vector : sequence) {
		vector += tile;
	}
	return sequence;
}

void Combinations(
	std::vector<Sequence>& sequences, const Directions& directions, std::vector<int>& pos, int n
) {
	if (n == pos.size()) {
		V2_int previous{ directions[0] };
		Sequence sequence{ V2_int{}, previous };
		bool invalidate = false;
		for (int i = 0; i != n; i++) {
			auto current = directions[pos[i]] + sequence.back();
			if (directions[pos[i]] == -previous || Contains(sequence, current)) {
				invalidate = true;
				break;
			} else {
				sequence.emplace_back(current);
				previous = directions[pos[i]];
			}
		}
		if (!invalidate) {
			sequence.erase(sequence.begin());
			sequences.emplace_back(sequence);
		}
		return;
	}
	for (int i = 0; i != directions.size(); i++) {
		pos[n] = i;
		Combinations(sequences, directions, pos, n + 1);
	}
}

std::pair<Sequence, Directions> GetSequenceAndAllowedDirections(
	std::vector<Sequence>& sequences, const CustomGrid& grid, const V2_int& tile
) {
	auto rd	 = std::random_device{};
	auto rng = std::default_random_engine{ rd() };
	std::shuffle(std::begin(sequences), std::end(sequences), rng);
	std::array<V2_int, 4> directions{ V2_int{ 1, 0 }, V2_int{ -1, 0 }, V2_int{ 0, 1 },
									  V2_int{ 0, -1 } };
	for (auto& sequence : sequences) {
		Directions permitted_directions;
		for (const auto& direction : directions) {
			auto rotated  = GetRotatedSequence(sequence, direction.Angle<float>());
			auto absolute = GetAbsoluteSequence(rotated, tile);
			if (grid.Permits(absolute, { TileType::WIN })) {
				permitted_directions.emplace_back(direction);
			}
		}
		if (permitted_directions.size() > 0) {
			return { sequence, permitted_directions };
		}
	}
	return {};
}

bool CanWin(const CustomGrid& grid, const V2_int& player_tile, const V2_int& win_tile) {
	std::array<V2_int, 4> directions{ V2_int{ 0, 1 }, V2_int{ 0, -1 }, V2_int{ 1, 0 },
									  V2_int{ -1, 0 } };
	std::queue<V2_int> q;
	q.push(player_tile);
	std::vector<V2_int> visited;
	bool reached = false;
	while (q.size() > 0) {
		V2_int p = q.front();
		q.pop();

		// mark as visited
		visited.emplace_back(p);

		// destination is reached.
		if (p == win_tile) {
			return true;
		}

		// check all four directions
		for (int i = 0; i < 4; i++) {
			// using the direction array
			V2_int t = p + directions[i];
			if (grid.InBound(t) && !Contains(visited, t) &&
				!grid.HasTile(t, { TileType::OBSTACLE, TileType::USED })) {
				q.push(t);
			}
		}
	}
	return false;
}

V2_int GetNewWinTile(const CustomGrid& grid, const V2_int& player_tile) {
	RNG<int> rng_x{ 0, grid.GetSize().x - 1 };
	RNG<int> rng_y{ 0, grid.GetSize().y - 1 };
	V2_int win_tile{ rng_x(), rng_y() };
	if (!grid.HasTile(win_tile) && win_tile != player_tile) {
		return win_tile;
	}
	return GetNewWinTile(grid, player_tile);
}

class DiceScene : public Scene {
public:
	V2_int grid_top_left_offset{ 32, 32 + 64 };
	V2_int dice_size{ 24, 24 };
	V2_int player_tile{ 1, 9 };
	V2_int win_tile{ 8, 8 };
	V2_int player_start_tile{ player_tile };
	RNG<int> dice_roll{ 1, 6 };
	Sequence sequence;
	Sequence absolute_sequence;
	Directions directions{ V2_int{ 1, 0 }, V2_int{ -1, 0 }, V2_int{ 0, 1 }, V2_int{ 0, -1 } };
	int dice{ 1 };
	bool turn_allowed = false;
	bool game_over	  = false;
	bool generate_new = false;
	V2_int previous_direction;
	std::unordered_map<std::size_t, std::vector<Sequence>> sequence_map;
	std::size_t turn		  = 0;
	std::size_t win_count	  = 0;
	std::size_t current_moves = 0;
	std::size_t best_moves	  = 1000000;
	CustomGrid& grid;
	Text text7{ game.font.Get(Hash("1")), "Press 'i' to see instructions", color::Gold };
	Sound s_select{ "resources/sound/select_click.wav" };
	Sound s_move{ "resources/sound/move_click.wav" };
	Sound s_win{ "resources/sound/win.wav" };
	Sound s_loss{ "resources/sound/loss.wav" };

	Texture t_grid{ "resources/tile/thick_grid.png" };
	Texture t_choice{ "resources/tile/thick_choice.png" };
	Texture t_nochoice{ "resources/tile/thick_nochoice.png" };
	Texture t_win{ "resources/tile/thick_win.png" };
	Texture t_used{ "resources/tile/used.png" };
	Texture t_dice{ "resources/tile/dice.png" };

	DiceScene(CustomGrid& grid) : grid{ grid } {
		sequence_map.emplace(1, std::vector<Sequence>{ Sequence{ V2_int{ 1, 0 } } });
		for (std::size_t i = 1; i < 6; ++i) {
			std::vector<Sequence> sequences;
			std::vector<int> pos(i, 0);
			Combinations(sequences, directions, pos, 0);
			sequence_map.emplace(i + 1, sequences);
		}
		auto pair =
			GetSequenceAndAllowedDirections(sequence_map.find(dice)->second, grid, player_tile);
		sequence   = pair.first;
		directions = pair.second;
		grid.AddTile(win_tile, Tile{ TileType::WIN });
		assert(
			pair.second.size() != 0 && "Could not find a valid starting positions, restart program"
		);
	}

	void Update() final {
		auto mouse = game.input.GetMousePosition();
		if (game.input.KeyDown(Key::I)) {
			game.scene.AddActive(Hash("menu"));
		}
		if (game.input.KeyDown(Key::R) || game_over) {
			if (turn > 0) {
				s_loss.Play(-1, 0);
				current_moves	   = 0;
				std::string title  = "";
				title			  += "Moves: ";
				title			  += std::to_string(0);
				if (win_count > 0) {
					title += " | Wins: ";
					title += std::to_string(win_count);
					title += " | Lowest: ";
					title += std::to_string(best_moves);
				}
				game.window.SetTitle(title.c_str());
			}
			++turn;
			grid.Clear();
			player_tile = GetNewWinTile(grid, win_tile);
			win_tile	= GetNewWinTile(grid, player_tile);
			grid.AddTile(win_tile, Tile{ TileType::WIN });
			game_over	 = false;
			generate_new = true;
		}

		if (!game_over && generate_new) {
			generate_new = false;
			dice		 = dice_roll();
			auto pair =
				GetSequenceAndAllowedDirections(sequence_map.find(dice)->second, grid, player_tile);
			sequence   = pair.first;
			directions = pair.second;
		}

		game_over = directions.size() == 0;

		if (!game_over) {
			auto player_position =
				grid_top_left_offset + player_tile * grid.GetTileSize() + grid.GetTileSize() / 2;
			auto direction		= mouse - player_position;
			auto axis_direction = ClosestAxis(direction);

			if (previous_direction != axis_direction && previous_direction != V2_int{}) {
				s_move.Play(-1, 0);
			}

			if (previous_direction != axis_direction) {
				previous_direction = axis_direction;
			}

			turn_allowed = Contains(directions, axis_direction);

			if (turn_allowed || previous_direction != axis_direction) {
				auto rotated	  = GetRotatedSequence(sequence, axis_direction.Angle<float>());
				absolute_sequence = GetAbsoluteSequence(rotated, player_tile);
			}

			if (turn_allowed && game.input.KeyDown(Key::SPACE) && sequence.size() > 0) {
				grid.AddTile(player_tile, Tile{ TileType::USED });
				player_tile = absolute_sequence.back();
				grid.AddTiles(absolute_sequence, Tile{ TileType::USED });
				generate_new = true;
				current_moves++;

				if (!grid.WinCondition(absolute_sequence)) {
					s_select.Play(-1, 0);
				} else {
					s_win.Play(-1, 0);
					game_over = true;
					turn	  = 0;
					++win_count;
					best_moves = std::min(best_moves, current_moves);
				}
				std::string title = "";
				if (game_over) {
					current_moves = 0;
				}
				title += "Moves: ";
				title += std::to_string(current_moves);
				if (win_count > 0) {
					title += " | Wins: ";
					title += std::to_string(win_count);
					title += " | Lowest: ";
					title += std::to_string(best_moves);
				}
				game.window.SetTitle(title.c_str());
				// game_over = !CanWin(grid, player_tile, win_tile);
			}

			for (auto i = 0; i < grid.GetSize().x; ++i) {
				for (auto j = 0; j < grid.GetSize().x; j++) {
					V2_int tile_position{ i, j };

					game.renderer.DrawTexture(
						t_grid,
						Rectangle<float>{ grid_top_left_offset + tile_position * grid.GetTileSize(),
										  grid.GetTileSize() }
					);

					if (grid.HasTile(tile_position)) {
						auto& tile = grid.GetTile(tile_position);
						if (tile.type == TileType::USED) {
							game.renderer.DrawTexture(
								t_used, Rectangle<float>{ grid_top_left_offset +
															  tile_position * grid.GetTileSize(),
														  grid.GetTileSize() }
							);
						} else if (tile.type == TileType::WIN) {
							game.renderer.DrawTexture(
								t_win, Rectangle<float>{ grid_top_left_offset +
															 tile_position * grid.GetTileSize(),
														 grid.GetTileSize() }
							);
						}
					}
				}
			}
			for (auto i = 0; i < absolute_sequence.size(); ++i) {
				auto pos = grid_top_left_offset +
						   absolute_sequence[i] *
							   grid.GetTileSize(); // + (grid.GetTileSize() - dice_size) / 2
				if (turn_allowed) {
					game.renderer.DrawTexture(
						t_choice, Rectangle<float>{ pos, grid.GetTileSize() }
					);
					Text t{ Hash("0"), std::to_string(i + 1).c_str(), color::Yellow };
					t.Draw({ pos + (grid.GetTileSize() - dice_size) / 2, dice_size });
				} else {
					auto rotated	  = GetRotatedSequence(sequence, axis_direction.Angle<float>());
					absolute_sequence = GetAbsoluteSequence(rotated, player_tile);
					if (grid.InBound(absolute_sequence[i])) {
						auto pos = grid_top_left_offset +
								   absolute_sequence[i] *
									   grid.GetTileSize(); // + (grid.GetTileSize() - dice_size) / 2
						game.renderer.DrawTexture(
							t_nochoice, Rectangle<float>{ pos, grid.GetTileSize() }
						);
					}
				}
			}

			// auto player_dice = 1;
			game.renderer.DrawTexture(
				t_dice,
				Rectangle<float>{ grid_top_left_offset + player_tile * grid.GetTileSize(),
								  grid.GetTileSize() },
				Rectangle<float>{ V2_float{ 64.0f * (dice - 1), 0 }, V2_float{ 64, 64 } }
			);
			// draw::SolidRectangle({ grid_top_left_offset + player_tile * grid.GetTileSize() +
			// (grid.GetTileSize() - dice_size) / 2, dice_size }, color::Grey);
			V2_float s = grid.GetSize() * grid.GetTileSize();
			text7.Draw(Rectangle<float>{ V2_float{ 32, 32 }, V2_float{ s.x, 64 } });
		}
	}
};

class MenuScreen : public Scene {
public:
	CustomGrid grid{ { 20, 20 }, { 32, 32 } };
	Text text0{ Hash("0"), "Stroll of the Dice", color::Cyan };
	Text text1{ Hash("1"), "'R' to restart if stuck", color::Red };
	Text text2{ Hash("1"), "'Mouse' to choose direction", color::Orange };
	Text text3{ Hash("1"), "'Spacebar' to confirm move", color::Gold };
	Text text4{ Hash("1"), "Green tile = Go over it to win", color::Green };
	Text text5{ Hash("1"), "Grey tile = Cannot move in that direction", color::Grey };
	Text text6{ Hash("1"), "Red tile = No longer usable tile", color::Red };
	Texture button{ "resources/ui/button.png" };

	MenuScreen() {
		game.music.Load(Hash("music"), "resources/music/background.wav");
		game.music.Get(Hash("music")).Play(-1);
	}

	void Update() final {
		auto mouse = game.input.GetMousePosition();
		V2_float s = grid.GetSize() * grid.GetTileSize();
		text0.Draw(Rectangle<float>{ V2_float{ 32, 32 }, V2_float{ s.x, 64 } });
		text1.Draw(Rectangle<float>{ V2_float{ 32, s.y }, V2_float{ s.x, 64 } });
		text2.Draw(Rectangle<float>{ V2_float{ 32, s.y + 64 }, V2_float{ s.x, 64 } });
		text3.Draw(Rectangle<float>{ V2_float{ 32, s.y + 128 }, V2_float{ s.x, 64 } });
		text4.Draw(Rectangle<float>{ V2_float{ 32, 32 + 128 + 128 }, V2_float{ s.x, 64 } });
		text5.Draw(Rectangle<float>{ V2_float{ 32, 32 + 128 }, V2_float{ s.x, 64 } });
		text6.Draw(Rectangle<float>{ V2_float{ 32, 32 + 64 + 128 }, V2_float{ s.x, 64 } });

		V2_float play_size{ s.x, 128 + 64 };
		V2_float play_pos{ 32, 32 + 128 + 128 + 32 + 64 };

		V2_float play_text_size{ s.x - 16 - 16, 128 + 64 - 16 - 16 - 16 - 16 };
		V2_float play_text_pos{ 32 + 16 + 16, 32 + 128 + 128 + 32 + 16 + 16 + 64 };

		Color text_color = color::White;

		bool hover =
			game.collision.overlap.PointRectangle(mouse, Rectangle<int>{ play_pos, play_size });
		if (hover) {
			text_color = color::Gold;
		}
		if ((hover && game.input.MouseDown(Mouse::Left)) || game.input.KeyDown(Key::SPACE)) {
			game.scene.Load<DiceScene>(Hash("game"), grid);
			game.scene.AddActive(Hash("game"));
		}
		game.renderer.DrawTexture(button, Rectangle<float>{ play_pos, play_size });
		Text t{ Hash("0"), "Play", text_color };
		t.Draw(Rectangle<float>{ play_text_pos, play_text_size });
	}
};

class DiceGame : public Scene {
public:
	DiceGame() {
		game.window.SetSize({ 704, 860 });
		game.font.Load(Hash("0"), "resources/font/04B_30.ttf", 32);
		game.font.Load(Hash("1"), "resources/font/retro_gaming.ttf", 32);
		game.scene.Load<MenuScreen>(Hash("menu"));
		game.scene.AddActive(Hash("menu"));
	}
};

int main(int c, char** v) {
	game.Start<DiceGame>();
	return 0;
}
