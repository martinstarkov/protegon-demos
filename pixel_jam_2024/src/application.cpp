#include "protegon/protegon.h"

using namespace ptgn;

class StartScreen;
class GameScene;

const inline V2_int grid_size{ 27, 16 };
const inline V2_int tile_size{ 16, 16 };

struct TextureComponent {
	TextureComponent(std::size_t key) : key{ key } {}
	std::size_t key{ 0 };
};

struct SpeedComponent {
	SpeedComponent(float speed) : speed{ speed } {}
	float speed{ 0.0f };
};

struct TextureMapComponent {
	TextureMapComponent() = default;
	TextureMapComponent(const V2_int& coordinate) : coordinate{ coordinate } {}
	V2_int coordinate;
};


struct TileComponent {
	TileComponent(const V2_int& coordinate) : coordinate{ coordinate } {}
	V2_int coordinate;
};

struct PrevTileComponent : public TileComponent {
	using TileComponent::TileComponent;
};

struct RotationComponent {
	RotationComponent() = default;
	RotationComponent(float angle) : angle{ angle } {}
	float angle{ 0.0f };
};

struct FlipComponent {
	FlipComponent() = default;
	FlipComponent(Flip flip) : flip{ flip } {}
	Flip flip{ Flip::NONE };
};

struct WaypointProgressComponent {
	WaypointProgressComponent() = default;
	float progress{ 0.0f };
	V2_int target_tile;
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

std::vector<V2_int> GetNeighborTiles(const std::vector<ecs::Entity>& paths, const V2_int& tile) {
	std::vector<V2_int> neighbors;
	for (const auto& e : paths) {
		assert(e.Has<TileComponent>());
		V2_int o_tile = e.Get<TileComponent>().coordinate;
		if (o_tile == tile) continue;
		V2_int dist = o_tile - tile;
		if (dist.MagnitudeSquared() == 1) {
			neighbors.emplace_back(o_tile);
		}
	}
	return neighbors;
}

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
		++(it->second);
	}
	V2_int GetNextTile(const V2_int& prev_tile, const std::vector<V2_int>& neighbors) {
		assert(neighbors.size() > 0 && "Cannot get next tile when there exist no neighbors");
		if (neighbors.size() == 1) {
			return neighbors.at(0);
		}
		else {
			std::vector<V2_int> candidates;
			for (const V2_int& candidate : neighbors) {
				if (candidate == prev_tile) continue;
				candidates.emplace_back(candidate);
			}
			if (candidates.size() == 1) {
				return candidates.at(0);
			}
			else if (candidates.size() == 2) {
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
	entity.Add<RotationComponent>();
	entity.Add<FlipComponent>();
	entity.Add<TextureComponent>(key);
	entity.Add<TextureMapComponent>();


	entity.Add<TileComponent>(coordinate);
	entity.Add<Rectangle<float>>(rect);
	manager.Refresh();
	return entity;
}

ecs::Entity CreateFish(ecs::Manager& manager, const Rectangle<float> rect, const V2_int coordinate, std::size_t key, const std::vector<ecs::Entity>& paths, float speed, V2_int offset) {
	auto entity = manager.CreateEntity();
	entity.Add<DrawComponent>();
	entity.Add<TextureComponent>(key);
	entity.Add<WaypointProgressComponent>();
	entity.Add<SpeedComponent>(speed);
	entity.Add<TileComponent>(coordinate);
	entity.Add<OffsetComponent>(offset);
	entity.Add<PrevTileComponent>(coordinate);
	entity.Add<FishComponent>();
	auto& pathing = entity.Add<PathingComponent>(paths);
	pathing.IncreaseVisitCount(coordinate);
	entity.Add<Rectangle<float>>(Rectangle<float>{ rect.pos, texture::Get(key)->GetSize() / 2 });
	manager.Refresh();
	return entity;
}

ecs::Entity CreateSpawn(ecs::Manager& manager, Rectangle<float> rect, V2_int coordinate) {
	// Move spawns outside of tile grid so fish go off screen.
	if (coordinate.x == 0) coordinate.x = -1;
	if (coordinate.x == grid_size.x - 1) coordinate.x = grid_size.x;
	if (coordinate.y == 0) coordinate.y = -1;
	if (coordinate.y == grid_size.y - 1) coordinate.y = grid_size.y;
	rect.pos = coordinate * tile_size;

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
	AStarGrid node_grid{ grid_size };
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
		texture::Load(Hash("floor"), "resources/tile/floor.png");
		texture::Load(Hash("nemo"), "resources/units/nemo.png");
		texture::Load(Hash("blue_nemo"), "resources/units/blue_nemo.png");
		texture::Load(Hash("jelly"), "resources/units/jelly.png");
	}

	void Reset() {
		// Setup node grid for the map.
		default_layout.ForEachPixel([&](const V2_int& coordinate, const Color& color) {
			V2_int position = coordinate * tile_size;
			Rectangle<float> rect{ position, tile_size };
			if (color == color::MAGENTA) {
				paths.push_back(CreatePath(manager, rect, coordinate, Hash("floor")));
			}
			if (color == color::BLUE) {
				paths.push_back(CreatePath(manager, rect, coordinate, Hash("floor")));
				ecs::Entity spawn = CreateSpawn(manager, rect, coordinate);
				spawn_points.push_back(spawn);
				paths.push_back(CreatePath(manager, spawn.Get<Rectangle<float>>(), spawn.Get<TileComponent>().coordinate, Hash("floor")));
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

		manager.ForEachEntityWith<PathComponent, TileComponent, TextureMapComponent, RotationComponent, FlipComponent>([&](
			ecs::Entity e, PathComponent& path, TileComponent& tile, TextureMapComponent& texture_map, RotationComponent& rotation, FlipComponent& flip) {
			std::vector<V2_int> neighbors = GetNeighborTiles(paths, tile.coordinate);

			int x = 0;
			int y = 0;

			RNG<int> rng_2{ 0, 1 };
			x = rng_2();

			if (neighbors.size() == 1) {
				// Either a spawn point or a dead end.
				bool spawn_point = false;
				for (const auto& e : spawn_points) {
					if (tile.coordinate == e.Get<TileComponent>().coordinate) {
						spawn_point = true;
					}
				}
				V2_int neighbor = neighbors.at(0);
				if (neighbor.y == tile.coordinate.y) {
					rotation.angle = 0;
				} else {
					rotation.angle = 90 + 180 * rng_2();
				}
				if (spawn_point) {
					y = 1;
				} else {
					y = 2;
					rotation.angle = 90 * (tile.coordinate.y - neighbor.y);
					if (neighbor.x > tile.coordinate.x) flip.flip = Flip::HORIZONTAL;
				}
			} else if (neighbors.size() == 2) {
				V2_int neighborA = neighbors.at(0);
				V2_int neighborB = neighbors.at(1);
				if (neighborA.x == neighborB.x) {
					y = 1;
					rotation.angle = 90 + 180 * rng_2();
				} else if (neighborA.y == neighborB.y) {
					y = 1;
					rotation.angle = 0;
				} else {
					y = 3;
					RNG<int> rng_4{ 0, 3 };
					x = rng_4();

					if (neighborA.x > tile.coordinate.x || neighborB.x > tile.coordinate.x) flip.flip = Flip::HORIZONTAL;
					if (neighborA.y < tile.coordinate.y || neighborB.y < tile.coordinate.y) rotation.angle = 90 + 180 * static_cast<int>(flip.flip);
				}
			} else if (neighbors.size() == 3) {
				V2_int neighborA = neighbors.at(0);
				V2_int neighborB = neighbors.at(1);
				V2_int neighborC = neighbors.at(2);

				V2_int diffs = neighborA + neighborB + neighborC - 3 * tile.coordinate;
				rotation.angle = 90 * diffs.x;
				if (diffs.y > 0) rotation.angle += 180;
				x = 0;
				y = 4;
			}
			texture_map.coordinate = { x, y };
		});
	}
	void Update(float dt) final {
		
		
		// Draw cyan background
		Rectangle<float> bg{ {}, window::GetLogicalSize() };
		bg.DrawSolid(color::CYAN);
		

		// Draw background tiles
		for (size_t i = 0; i < grid_size.x; i++) {
			for (size_t j = 0; j < grid_size.y; j++) {
				Rectangle<int> r{ V2_int{ i, j } * tile_size, tile_size };
				texture::Get(Hash("floor"))->Draw(r, { { 0, 0 }, tile_size });
			}
		}

		auto get_spawn_location = [&]() -> std::pair<Rectangle<float>, V2_int> {
			RNG<int> rng{ 0, static_cast<int>(spawn_points.size()) - 1 };
			int spawn_index = rng();
			ecs::Entity spawn_point = spawn_points.at(spawn_index);
			const Rectangle<float>& rect = spawn_point.Get<Rectangle<float>>();
			const TileComponent& tile = spawn_point.Get<TileComponent>();
			return { rect, tile.coordinate };
		};

		auto [spawn_rect, spawn_coordinate] = get_spawn_location();

		if (input::KeyDown(Key::S)) {
			CreateFish(manager, spawn_rect, spawn_coordinate, Hash("nemo"), paths, 1.0f, V2_int{ 0, 2 });
		}
		if (input::KeyDown(Key::B)) {
			CreateFish(manager, spawn_rect, spawn_coordinate, Hash("blue_nemo"), paths, 1.0f, V2_int{ 0, 2 });
		}
		if (input::KeyDown(Key::J)) {
			CreateFish(manager, spawn_rect, spawn_coordinate, Hash("jelly"), paths, 1.0f, V2_int{ 4, 4 });
		}

		/*manager.ForEachEntityWith<Rectangle<float>, TextureComponent, DrawComponent>([](
			ecs::Entity e, Rectangle<float>& rect,
			TextureComponent& texture, DrawComponent& draw) {
				rect.pos = Lerp(V2_float{ tile.coordinate * tile_size },
								V2_float{ (tile.coordinate + direction) * tile_size },
								waypoint.current);
		});*/

		auto draw_texture = [&](const ecs::Entity& e, Rectangle<float> rect, std::size_t texture_key) {
			if (e.Has<OffsetComponent>()) {
				rect.pos += e.Get<OffsetComponent>().offset;
			}
			Rectangle<int> source;
			if (e.Has<TextureMapComponent>()) {
				source.pos = e.Get<TextureMapComponent>().coordinate * tile_size;
				source.size = tile_size;
			}
			float angle{ 0.0f };
			if (e.Has<RotationComponent>()) {
				angle = e.Get<RotationComponent>().angle;
			}
			Flip flip{ Flip::NONE };
			if (e.Has<FlipComponent>()) {
				flip = e.Get<FlipComponent>().flip;
			}
			texture::Get(texture_key)->Draw(rect, source, angle, flip);
		};

		// Draw static rectangular structures with textures.
		manager.ForEachEntityWith<Rectangle<float>, TextureComponent, DrawComponent>([&](
			ecs::Entity e, Rectangle<float>& rect, TextureComponent& texture, DrawComponent& draw) {
			draw_texture(e, rect, texture.key);
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


		manager.ForEachEntityWith<PathingComponent, TileComponent, PrevTileComponent, Rectangle<float>, WaypointProgressComponent, SpeedComponent>([&](
			ecs::Entity e, PathingComponent& pathing, TileComponent& tile,
			PrevTileComponent& prev_tile, Rectangle<float>& rect,
			WaypointProgressComponent& waypoint, SpeedComponent& speed) {
			bool set_new_tile = false;

			waypoint.progress += dt * speed.speed;

			auto get_target_tile = [&](const V2_int& prev_tile, const V2_int& tile) {
				std::vector<V2_int> neighbors = pathing.GetNeighborTiles(tile);

				V2_int new_tile = pathing.GetNextTile(prev_tile, neighbors);

				assert(new_tile != tile && "Algorithm failed to find a new tile to move to");

				return new_tile;
			};

			if (prev_tile.coordinate == tile.coordinate) {
				waypoint.target_tile = get_target_tile(prev_tile.coordinate, tile.coordinate);
			}

			while (waypoint.progress >= 1.0f) {
				prev_tile.coordinate = tile.coordinate;
				tile.coordinate = waypoint.target_tile;
				waypoint.target_tile = get_target_tile(prev_tile.coordinate, tile.coordinate);
				pathing.IncreaseVisitCount(tile.coordinate);
				waypoint.progress -= 1.0f;
				if (waypoint.progress < 1.0f) {
					waypoint.target_tile = get_target_tile(prev_tile.coordinate, tile.coordinate);
				}
			}

			rect.pos = Lerp(tile.coordinate * tile_size, waypoint.target_tile * tile_size, waypoint.progress);
		});

		manager.ForEachEntityWith<PathingComponent, TileComponent, PrevTileComponent, WaypointProgressComponent>([&](
			ecs::Entity e, PathingComponent& pathing, TileComponent& tile, PrevTileComponent& prev_tile, WaypointProgressComponent& waypoint) {
			if (prev_tile.coordinate != tile.coordinate) {
				for (auto& spawn : spawn_points) {
					if (tile.coordinate == spawn.Get<TileComponent>().coordinate &&
						pathing.GetVisitCount(tile.coordinate) > 0) {
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
		window::SetSize({ 1280, 720 }, true);
		window::SetColor(color::BLACK);
		window::SetResizeable(true);
		window::Maximize();
		window::SetScale({ 3.0f, 3.0f });
		window::SetLogicalSize({ 1280, 720 });

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
