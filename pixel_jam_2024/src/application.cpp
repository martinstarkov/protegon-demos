#include "protegon/protegon.h"

using namespace ptgn;

class StartScreen;
class GameScene;

struct TextureComponent {
	TextureComponent(std::size_t key) : key{ key } {}
	TextureComponent(std::size_t key, int index) : key{ key }, index{ index } {}
	std::size_t key{ 0 };
	int index{ 0 };
};

struct TileComponent {
	TileComponent(const V2_int& coordinate) : coordinate{ coordinate } {}
	V2_int coordinate;
};

struct PrevTileComponent : public TileComponent {
	using TileComponent::TileComponent;
};

struct StaticComponent {};
struct DrawComponent {};
struct SpawnComponent {};
struct FishComponent {};
struct PathComponent {};

struct OffsetComponent {
	OffsetComponent(const V2_int& offset) : offset{ offset } {}
	V2_int offset;
};

struct PathingComponent {
	PathingComponent(const std::vector<ecs::Entity>& paths) {
		for (auto& e : paths) {
			assert(e.Has<TileComponent>());
			path_visits.emplace(e.Get<TileComponent>().coordinate, 0);
		}
	}
	void IncreaseVisitCount(const V2_int& tile) {
		auto it = path_visits.find(tile);
		assert(it != path_visits.end() && "Cannot increase visit count for tile which does not exist in pathing component");
		++it->second;
	}
	V2_int GetNextTile(const V2_int& prev_tile, const std::vector<V2_int>& neighbors) {
		assert(neighbors.size() > 0 && "Cannot get next tile when there exist no neighbors");
		if (neighbors.size() == 1) {
			return neighbors.at(0);
		} else {
			std::vector<V2_int> candidates;
			for (const V2_int& candidate : neighbors) {
				if (candidate == prev_tile) continue;
				candidates.emplace_back(candidate);
			}
			if (candidates.size() == 1) {
				return candidates.at(0);
			} else if (candidates.size() == 2) {
				V2_int optionA = candidates.at(0);
				V2_int optionB = candidates.at(1);
				int visitsA = GetVisitCount(optionA);
				int visitsB = GetVisitCount(optionB);
				if (visitsA == visitsB) {
					// Prioritize new tiles to avoid rubber banding at dead ends.
					RNG<int> rng{ 0, 1 };
					return candidates.at(rng());
				}
				else {
					return visitsA < visitsB ? optionA : optionB;
				}
			}
		}
		assert(!"Pathing algorithm does not currently >2 fork intersections");
		return {};
	}
	std::vector<V2_int> GetNeighborTiles(const V2_int& tile) {
		std::vector<V2_int> neighbors;
		for (auto [o_tile, visits] : path_visits) {
			if (o_tile == tile) continue;
			V2_int dist = o_tile - tile;
			if (dist.MagnitudeSquared() == 1) {
				neighbors.emplace_back(o_tile);
			}
		}
		return neighbors;
	}
	int GetVisitCount(const V2_int& tile) const {
		auto it = path_visits.find(tile);
		assert(it != path_visits.end() && "Cannot get visit count for tile which does not exist in pathing component");
		return it->second;
	}
	std::unordered_map<V2_int, int> path_visits;
};

ecs::Entity CreatePath(ecs::Manager& manager, const Rectangle<float>& rect, const V2_int& coordinate, std::size_t key) {
	auto entity = manager.CreateEntity();
	entity.Add<PathComponent>();
	entity.Add<StaticComponent>();
	entity.Add<DrawComponent>();
	entity.Add<TextureComponent>(key);
	entity.Add<TileComponent>(coordinate);
	entity.Add<Rectangle<float>>(rect);
	manager.Refresh();
	return entity;
}

ecs::Entity CreateFish(ecs::Manager& manager, const Rectangle<float>& rect, const V2_int& coordinate, std::size_t key, const std::vector<ecs::Entity>& paths) {
	auto entity = manager.CreateEntity();
	entity.Add<DrawComponent>();
	entity.Add<TextureComponent>(key);
	entity.Add<TileComponent>(coordinate);
	entity.Add<OffsetComponent>(V2_int{ 0, 8 });
	entity.Add<PrevTileComponent>(coordinate);
	entity.Add<FishComponent>();
	auto& pathing = entity.Add<PathingComponent>(paths);
	pathing.IncreaseVisitCount(coordinate);
	entity.Add<Rectangle<float>>(Rectangle<float>{ rect.pos, texture::Get(key)->GetSize() });
	manager.Refresh();
	return entity;
}

ecs::Entity CreateSpawn(ecs::Manager& manager, const Rectangle<float>& rect, const V2_int& coordinate) {
	auto entity = manager.CreateEntity();
	entity.Add<SpawnComponent>();
	entity.Add<TileComponent>(coordinate);
	entity.Add<Rectangle<float>>(rect);
	manager.Refresh();
	return entity;
}

class GameScene : public Scene {
public:
	Surface default_layout{ "resources/maps/default_layout.png" };
	V2_int grid_size{ 30, 15 };
	AStarGrid node_grid{ grid_size };
	V2_int tile_size{ 32, 32 };
	ecs::Manager manager;
	
	json j;

	std::vector<ecs::Entity> spawn_points;
	std::vector<ecs::Entity> paths;

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

		Reset();

		// Load textures.
		texture::Load(Hash("sand_path"), "resources/tile/sand_path.png");
		texture::Load(Hash("nemo"), "resources/units/nemo.png");
	}

	void Reset() {
		// Setup node grid for the map.
		default_layout.ForEachPixel([&](const V2_int& coordinate, const Color& color) {
			V2_int position = coordinate * tile_size;
			Rectangle<float> rect{ position, tile_size };
			if (color == color::MAGENTA) {
				paths.push_back(CreatePath(manager, rect, coordinate, Hash("sand_path")));
			}
			if (color == color::BLUE) {
				paths.push_back(CreatePath(manager, rect, coordinate, Hash("sand_path")));
				spawn_points.push_back(CreateSpawn(manager, rect, coordinate));
			}
			//else if (color == color::LIGHT_PINK) {
			//	//CreateWall(rect, coordinate, 500);
			//	//node_grid.SetObstacle(coordinate, true);
			//}
			//
			//else if (color == color::LIME) {
			//	//end = CreateEnd(rect, coordinate);
			//}
		});
	}
	void Update(float dt) final {
		Rectangle<float> bg{ {}, window::GetLogicalSize() };
		bg.DrawSolid(color::CYAN);

		if (input::KeyDown(Key::S)) {
			RNG<int> rng{ 0, static_cast<int>(spawn_points.size()) - 1 };
			int spawn_index = rng();
			ecs::Entity spawn_point = spawn_points.at(spawn_index);
			const Rectangle<float>& spawn_rect = spawn_point.Get<Rectangle<float>>();
			const TileComponent& spawn_tile = spawn_point.Get<TileComponent>();
			CreateFish(manager, spawn_rect, spawn_tile.coordinate, Hash("nemo"), paths);
		}

		/*manager.ForEachEntityWith<Rectangle<float>, TextureComponent, DrawComponent>([](
			ecs::Entity e, Rectangle<float>& rect,
			TextureComponent& texture, DrawComponent& draw) {
				rect.pos = Lerp(V2_float{ tile.coordinate * tile_size },
								V2_float{ (tile.coordinate + direction) * tile_size },
								waypoint.current);
		});*/

		// Draw static rectangular structures with textures.
		manager.ForEachEntityWith<Rectangle<float>, TextureComponent, DrawComponent>([](
			ecs::Entity e, Rectangle<float> rect,
			TextureComponent& texture, DrawComponent& draw) {
				if (e.Has<OffsetComponent>()) {
					rect.pos += e.Get<OffsetComponent>().offset;
				}
				texture::Get(texture.key)->Draw(rect);
		});

		if (input::KeyPressed(Key::N)) {
			manager.ForEachEntityWith<PathingComponent, TileComponent>([&](
				ecs::Entity e, PathingComponent& pathing, TileComponent& tile) {
				std::vector<V2_int> neighbors = pathing.GetNeighborTiles(tile.coordinate);
				for (const V2_int& neighbor : neighbors) {
					Rectangle<int> r{ neighbor * tile_size, tile_size };
					r.DrawSolid(color::RED);
				}
				//texture::Get(texture.key)->Draw(rect);
			});
		}


		if (input::KeyDown(Key::M)) {
			manager.ForEachEntityWith<PathingComponent, TileComponent, PrevTileComponent, Rectangle<float>>([&](
				ecs::Entity e, PathingComponent& pathing, TileComponent& tile, PrevTileComponent& prev_tile, Rectangle<float>& rect) {
				std::vector<V2_int> neighbors = pathing.GetNeighborTiles(tile.coordinate);

				V2_int new_tile = pathing.GetNextTile(prev_tile.coordinate, neighbors);

				assert(new_tile != tile.coordinate && "Algorithm failed to find a new tile to move to");

				V2_int direction = new_tile - tile.coordinate;
				/*float velocity = 2;
				rect.pos = Lerp(tile.coordinate * tile_size, neighbor * tile_size, dt * velocity);*/
				rect.pos = new_tile * tile_size;
				
				prev_tile.coordinate = tile.coordinate;

				tile.coordinate = new_tile;
				pathing.IncreaseVisitCount(tile.coordinate);

				//texture::Get(texture.key)->Draw(rect);
			});
		}

		manager.ForEachEntityWith<PathingComponent, TileComponent, PrevTileComponent>([&](
			ecs::Entity e, PathingComponent& pathing, TileComponent& tile, PrevTileComponent& prev_tile) {
			if (prev_tile.coordinate != tile.coordinate) {
				for (auto& spawn : spawn_points) {
					if (tile.coordinate == spawn.Get<TileComponent>().coordinate && pathing.GetVisitCount(tile.coordinate) > 0) {
						e.Destroy();
					}
				}
			}
		});

		manager.Refresh();
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
		window::SetColor(color::BLACK);
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
