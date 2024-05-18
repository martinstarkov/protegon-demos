#include "protegon/protegon.h"

using namespace ptgn;

class StartScreen;
class GameScene;

const inline V2_int grid_size{ 27, 16 };
const inline V2_int tile_size{ 16, 16 };

struct OxygenComponent {};

struct TextureComponent {
	TextureComponent(std::size_t key, const std::string& str_key = "") : key{ key }, str_key{ str_key } {}
	std::size_t key{ 0 };
	std::string str_key;
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

struct ScaleComponent {
	ScaleComponent(const V2_float& scale) : scale{ scale } {}
	V2_float scale{ 1.0f, 1.0f };
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
struct FishComponent {};
struct PathComponent {};

struct SpawnerComponent {
public:
	SpawnerComponent() = delete;
	SpawnerComponent(std::size_t max_spawn_count, milliseconds spawn_rate, std::function<ecs::Entity(V2_int source)> func) :
		max_spawn_count{ max_spawn_count }, spawn_rate{ spawn_rate }, func{ func } {}
	void Start() {
		spawn_timer.Start();
	}
	void Stop() {
		spawn_timer.Stop();
	}
	void Update() {
		/*manager.ForEachEntityWith<Rectangle<float>, VelocityComponent, LifetimeComponent>([&](
			ecs::Entity e, Rectangle<float>& rect, VelocityComponent& velocity, LifetimeComponent& life) {
		});*/
		if (spawn_timer.Elapsed() > spawn_rate && entities.size() < max_spawn_count) {
			entities.push_back(func(source));
			spawn_timer.Reset();
			spawn_timer.Start();
		}
		entities.erase(
			std::remove_if(entities.begin(), entities.end(),
				[](const ecs::Entity& o) { return !o.IsAlive(); }),
			entities.end());
	}
	void SetSource(const V2_int& new_source) {
		source = new_source;
	}
private:
	std::size_t max_spawn_count{ 0 };
	milliseconds spawn_rate;
	std::function<ecs::Entity(V2_int source)> func{ nullptr };
	std::vector<ecs::Entity> entities;
	Timer spawn_timer;
	V2_int source;
};

struct OffsetComponent {
	OffsetComponent() = default;
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
	PathingComponent(const std::vector<ecs::Entity>& paths, const V2_int& start_tile) {
		for (auto& e : paths) {
			assert(e.Has<TileComponent>());
			path_visits.emplace(e.Get<TileComponent>().coordinate, 0);
		}
		IncreaseVisitCount(start_tile);
	}
	void IncreaseVisitCount(const V2_int& tile) {
		auto it = path_visits.find(tile);
		assert(it != path_visits.end() && "Cannot increase visit count for tile which does not exist in pathing component");
		++(it->second);
	}
	V2_int GetTargetTile(const V2_int& prev_tile, const V2_int& tile) const {
		std::vector<V2_int> neighbors = GetNeighborTiles(tile);
		V2_int new_tile = GetNextTile(prev_tile, neighbors);
		assert(new_tile != tile && "Algorithm failed to find a new tile to move to");
		return new_tile;
	}
	V2_int GetNextTile(const V2_int& prev_tile, const std::vector<V2_int>& neighbors) const {
		assert(neighbors.size() > 0 && "Cannot get next tile when there exist no neighbors");
		if (neighbors.size() == 1) return neighbors.at(0);

		std::vector<V2_int> candidates;
		for (const V2_int& candidate : neighbors) {
			if (candidate == prev_tile) continue;
			candidates.emplace_back(candidate);
		}
		assert(candidates.size() > 0 && "Algorithm failed to find two valid candidate tiles");
		if (candidates.size() == 1) {
			return candidates.at(0);
		}
		// Go through each tile and compare to the previous least visited tile
		// If the new tile is least visited, roll a 50/50 dice to choose which becomes the new least visited path.
		RNG<int> fifty_fifty{ 0, 1 };
		bool equal_visits = true;
		V2_int first_tile = candidates.at(0);
		int first_visits = GetVisitCount(first_tile);
		V2_int least_visited_tile = first_tile;
		int least_visits = first_visits;
		for (const V2_int& candidate : candidates) {
			if (candidate == least_visited_tile) continue;
			int visits = GetVisitCount(candidate);
			if (visits == least_visits) {
				// This prevents biasing direction toward candidates.at(0).
				if (fifty_fifty() == 0) {
					least_visits = visits;
					least_visited_tile = candidate;
				}
			} else if (visits < least_visits) {
				least_visits = visits;
				least_visited_tile = candidate;
				equal_visits = false;
			}
		}
		// This prevents biasing direction toward the final least visited candidate.
		if (first_visits == least_visits) {
			// This prevents biasing direction toward candidates.at(0).
			if (fifty_fifty() == 0) {
				least_visits = first_visits;
				least_visited_tile = first_tile;
			}
		}
		if (equal_visits) {
			RNG<int> rng{ 0, static_cast<int>(candidates.size()) - 1 };
			least_visited_tile = candidates.at(rng());
		}
		// In essence, each tile must roll lucky on two 50/50 rolls to become the chosen path.
		return least_visited_tile;
	}
	std::vector<V2_int> GetNeighborTiles(const V2_int& tile) const {
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



struct VelocityComponent {
	VelocityComponent(const V2_float& vel) : vel{ vel } {}
	V2_float vel;
};

struct LifetimeComponent {
	LifetimeComponent(milliseconds time) : time{ time } {}
	Timer timer;
	milliseconds time{ 0 };
};

struct ParticleComponent {
public:
	ParticleComponent() = delete;
	ParticleComponent(std::size_t max_particle_count, const std::vector<std::size_t>& texture_keys, milliseconds particle_lifetime, milliseconds spawn_rate) :
		max_particle_count{ max_particle_count }, texture_keys{ texture_keys }, particle_lifetime{ particle_lifetime }, spawn_rate{ spawn_rate } {}
	void Start() {
		spawn_timer.Start();
	}
	void Stop() {
		spawn_timer.Stop();
	}
	void GenerateParticle() {
		if (manager.Size() >= max_particle_count) return;
		auto entity = manager.CreateEntity();
		RNG<int> rng{ 0, static_cast<int>(texture_keys.size()) - 1 };
		int texture_index = rng();
		assert(texture_index < texture_keys.size());
		auto& scale = entity.Add<ScaleComponent>(V2_float{ 0.5f, 0.5f });
		std::size_t texture_key = texture_keys[texture_index];
		assert(texture::Has(texture_key));
		V2_int texture_size = texture::Get(texture_key)->GetSize();
		auto& rect = entity.Add<Rectangle<float>>(Rectangle<float>{ source, texture_size });

		assert(x_min_speed < x_max_speed);
		assert(y_min_speed < y_max_speed);

		RNG<float> rng_speed_x{ x_min_speed, x_max_speed };
		RNG<float> rng_speed_y{ y_min_speed, y_max_speed };

		entity.Add<TextureComponent>(texture_key);
		entity.Add<VelocityComponent>(V2_float{ rng_speed_x(), rng_speed_y() });
		entity.Add<OffsetComponent>(-texture_size / 2);
		auto& lifetime = entity.Add<LifetimeComponent>(particle_lifetime);
		lifetime.timer.Start();
		manager.Refresh();
	}
	void Update() {
		manager.ForEachEntityWith<Rectangle<float>, VelocityComponent, LifetimeComponent>([&](
			ecs::Entity e, Rectangle<float>& rect, VelocityComponent& velocity, LifetimeComponent& life) {
			rect.pos += velocity.vel;
			velocity.vel *= 0.99f;
			milliseconds elapsed = life.timer.Elapsed();
			if (elapsed > life.time) {
				e.Destroy();
			}
		});
		if (spawn_timer.Elapsed() > spawn_rate) {
			GenerateParticle();
			spawn_timer.Reset();
			spawn_timer.Start();
		}
		manager.Refresh();
	}
	// TODO: Add const ForEachEntityWith to ecs library.
	void Draw() {
		manager.ForEachEntityWith<Rectangle<float>, LifetimeComponent, OffsetComponent, TextureComponent, ScaleComponent>([&](
			ecs::Entity e, const Rectangle<float>& rect, const LifetimeComponent& life,
			const OffsetComponent& offset, const TextureComponent& texture, ScaleComponent& scale) {
			float elapsed = life.timer.ElapsedPercentage(life.time);
			std::uint8_t alpha = (1.0f - elapsed) * 255;
			assert(texture::Has(texture.key));
			Texture& t = *texture::Get(texture.key);
			t.SetAlpha(alpha);
			t.Draw(rect.Scale(scale.scale).Offset(offset.offset * scale.scale));
		});
		// Draw debug point to identify source of particles.
		/*Circle<float> c{ source, 2 };
		c.DrawSolid(color::RED);*/
	}
	void SetSource(const V2_int& new_source) {
		source = new_source;
	}
	void SetSpeed(const V2_float& min, const V2_float& max) {
		x_min_speed = min.x;
		y_min_speed = min.y;
		x_max_speed = max.x;
		y_max_speed = max.y;
	}
private:
	std::size_t max_particle_count{ 0 };
	float x_min_speed{ -0.25 };
	float x_max_speed{ 0.25 };
	float y_min_speed{ -0.5 };
	float y_max_speed{ -0.15 };
	V2_int source;
	milliseconds particle_lifetime;
	milliseconds spawn_rate;
	Timer spawn_timer;
	ecs::Manager manager;
	std::vector<std::size_t> texture_keys;
};

ecs::Entity CreatePath(ecs::Manager& manager, const Rectangle<float>& rect, const V2_int& coordinate, std::size_t key) {

	auto entity = manager.CreateEntity();
	entity.Add<PathComponent>();
	entity.Add<StaticComponent>();
	entity.Add<DrawComponent>();
	entity.Add<RotationComponent>();
	entity.Add<FlipComponent>();
	assert(texture::Has(key));
	entity.Add<TextureComponent>(key);
	entity.Add<TextureMapComponent>();


	entity.Add<TileComponent>(coordinate);
	entity.Add<Rectangle<float>>(rect);
	manager.Refresh();
	return entity;
}

enum class Particle {
	NONE,
	OXYGEN,
	CARBON_DIOXIDE,
};

ecs::Entity CreateFish(ecs::Manager& manager, const Rectangle<float> rect, const V2_int coordinate, const std::string& str_key, const std::vector<ecs::Entity>& paths, float speed) {
	auto entity = manager.CreateEntity();
	entity.Add<DrawComponent>();
	std::size_t key = Hash(str_key.c_str());
	assert(texture::Has(key));
	entity.Add<TextureComponent>(key, str_key);
	auto& waypoint = entity.Add<WaypointProgressComponent>();
	entity.Add<SpeedComponent>(speed);
	entity.Add<FlipComponent>();
	auto& scale = entity.Add<ScaleComponent>(V2_float{ 0.5f, 0.5f });
	auto& tile = entity.Add<TileComponent>(coordinate);
	V2_int texture_size = texture::Get(key)->GetSize();
	auto& offset = entity.Add<OffsetComponent>(-texture_size / 2);
	auto& rectangle = entity.Add<Rectangle<float>>(Rectangle<float>{ rect.pos, texture_size });
	entity.Add<PrevTileComponent>(coordinate);
	entity.Add<FishComponent>();
	auto& pathing = entity.Add<PathingComponent>(paths, coordinate);
	waypoint.target_tile = pathing.GetTargetTile(tile.coordinate, tile.coordinate);
	auto& particle_component = entity.Add<ParticleComponent>(10, std::vector<std::size_t>{ Hash("co_2_1"), Hash("co_2_2"), Hash("co_2_2"), Hash("co_2_2") }, milliseconds{ 500 }, milliseconds{ 2000 });
	particle_component.SetSpeed({ -0.1f, -0.2f }, { 0.1f, -0.1f });
	particle_component.SetSource(rect.pos);
	particle_component.Start();
	manager.Refresh();
	return entity;
}

enum class Spawner {
	NONE,
	NEMO,
};

ecs::Entity CreateStructure(ecs::Manager& manager, const Rectangle<float> pos_rect, const V2_int coordinate, std::size_t key, Particle particle = Particle::NONE, Spawner spawner = Spawner::NONE, V2_int source = {}, const std::vector<ecs::Entity>& paths = {}) {
	auto entity = manager.CreateEntity();
	entity.Add<DrawComponent>();
	assert(texture::Has(key));
	V2_int texture_size = texture::Get(key)->GetSize();
	auto& scale = entity.Add<ScaleComponent>(V2_float{ 0.5f, 0.5f });
	entity.Add<TextureComponent>(key);
	
	auto& rect = entity.Add<Rectangle<float>>(Rectangle<float>{ pos_rect.pos, texture_size });
	auto& offset = entity.Add<OffsetComponent>(V2_int{ -texture_size.x / 2, -texture_size.y + texture_size.y / 4 });
	
	entity.Add<TileComponent>(coordinate);

	switch (particle) {
		case Particle::NONE:
			break;
		case Particle::OXYGEN: {
			auto& particle_component = entity.Add<ParticleComponent>(10, std::vector<std::size_t>{ Hash("o_2_1"), Hash("o_2_2"), Hash("o_2_2"), Hash("o_2_2") }, milliseconds{ 1000 }, milliseconds{ 500 });
			particle_component.SetSource(rect.pos);
			particle_component.Start();
			break;
		}
		case Particle::CARBON_DIOXIDE:
		{
			auto& particle_component = entity.Add<ParticleComponent>(10, std::vector<std::size_t>{ Hash("co_2_1"), Hash("co_2_2"), Hash("co_2_2"), Hash("co_2_2") }, milliseconds{ 1000 }, milliseconds{ 500 });
			particle_component.SetSource(rect.pos);
			particle_component.Start();
			break;
		}
	}
	if (spawner != Spawner::NONE) {
		assert(paths.size() > 0 && "Must provide spawned fish with vector of path tiles");
	}
	switch (spawner) {
		case Spawner::NONE:
			break;
		case Spawner::NEMO:
		{
			auto& spawner = entity.Add<SpawnerComponent>(5, milliseconds{ 7000 }, [&](V2_int source) {
				ecs::Entity fish = CreateFish(manager, { source * tile_size + tile_size / 2, source }, source, "nemo", paths, 1.0f);
				return fish;
			});
			spawner.SetSource(source);
			spawner.Start();
			break;
		}
	}

	manager.Refresh();
	return entity;
}

ecs::Entity CreateSpawn(ecs::Manager& manager, Rectangle<float> rect, V2_int coordinate) {
	// Move spawns outside of tile grid so fish go off screen.
	if (coordinate.x == 0) coordinate.x = -1;
	if (coordinate.x == grid_size.x - 1) coordinate.x = grid_size.x;
	if (coordinate.y == 0) coordinate.y = -1;
	if (coordinate.y == grid_size.y - 1) coordinate.y = grid_size.y;
	rect.pos = coordinate * tile_size + tile_size / 2;

	auto entity = manager.CreateEntity();
	entity.Add<TileComponent>(coordinate);
	entity.Add<Rectangle<float>>(rect);
	manager.Refresh();
	return entity;
}

struct UILevelComponent {};

struct TextComponent {
	TextComponent(std::size_t font_key, const char* content, const Color& color) : text{ font_key, content, color } {}
	Text text;
};

struct UILevelIndicator {
public:
	void Create(const std::string& label, const Color& text_color, const V2_float& coordinate, ecs::Manager& manager, const V2_float scale = { 0.75f, 0.75f }) {
		entity = manager.CreateEntity();
		std::size_t texture_key = Hash("level_indicator");
		assert(texture::Has(texture_key));
		Texture& texture = *texture::Get(texture_key);
		entity.Add<UILevelComponent>();
		entity.Add<ScaleComponent>(scale);
		entity.Add<TextureComponent>(texture_key);
		std::size_t indicator_font = Hash("default_font");
		auto& text = entity.Add<TextComponent>(indicator_font, label.c_str(), text_color);
		entity.Add<Rectangle<float>>(Rectangle<float>{ V2_float{ coordinate * tile_size }, texture.GetSize() });
		manager.Refresh();
	}
	virtual void Draw() {
		const Rectangle<float>& rect = entity.Get<Rectangle<float>>();
		const TextureComponent& texture = entity.Get<TextureComponent>();
		const UILevelComponent& ui_level = entity.Get<UILevelComponent>();
		const ScaleComponent& scale = entity.Get<ScaleComponent>();
		const TextComponent& text = entity.Get<TextComponent>();
		assert(texture::Has(texture.key));
		const Rectangle<float> rect_scaled = rect.Scale(scale.scale);
		// -1 from radius is so the circle fully fits inside the texture.
		Circle<float> circle{ rect_scaled.Center(), rect_scaled.Half().x - 1 };
		circle.DrawSolidSliced(color, [&](float y_frac) {
			return y_frac >= 1.0f - level;
		});
		texture::Get(texture.key)->Draw(rect_scaled);
		const Rectangle<float> text_rect{ V2_float{ rect_scaled.pos.x, -1.0f }, V2_float{ rect_scaled.size.x, 0.5f * tile_size.y } };
		text.text.Draw(text_rect);
	}
	void UpdateLevel(float level_change) {
		level = std::clamp(level + level_change, 0.0f, 1.0f);
	}
	float GetLevel() const {
		return level;
	}
	void SetColor(const Color& new_color) {
		color = new_color;
	}
protected:
	Color color;
	float level{ 0.0f };
	ecs::Entity entity;
};

struct DayNightIndicator : public UILevelIndicator {
public:
	using UILevelIndicator::UILevelIndicator;
	virtual void Draw() override final {
		const Rectangle<float>& rect = entity.Get<Rectangle<float>>();
		const TextureComponent& texture = entity.Get<TextureComponent>();
		const UILevelComponent& ui_level = entity.Get<UILevelComponent>();
		const ScaleComponent& scale = entity.Get<ScaleComponent>();
		const TextComponent& text = entity.Get<TextComponent>();
		assert(texture::Has(texture.key));
		const Rectangle<float> rect_scaled = rect.Scale(scale.scale);

		Circle<float> top_circle{ rect_scaled.Center(), rect_scaled.Half().x - 1 };
		top_circle.DrawSolidSliced(color::DARK_BLUE, [&](float y_frac) {
			return y_frac >= 1.0f - level;
		});

		Circle<float> bottom_circle{ rect_scaled.Center(), rect_scaled.Half().x - 1 };
		bottom_circle.DrawSolidSliced(color::GOLD, [&](float y_frac) {
			return y_frac <= 1.0f - level;
		});

		texture::Get(texture.key)->Draw(rect_scaled);
		const Rectangle<float> text_rect{ V2_float{ rect_scaled.pos.x, -1.0f }, V2_float{ rect_scaled.size.x, 0.5f * tile_size.y } };
		text.text.Draw(text_rect);
	}
	void SetLevel(float new_level) {
		level = std::clamp(new_level, 0.0f, 1.0f);
	}
};



class GameScene : public Scene {
public:
	Surface default_layout{ "resources/maps/default_layout.png" };
	AStarGrid node_grid{ grid_size };
	ecs::Manager manager;

	UILevelIndicator crowding_indicator;
	UILevelIndicator pollution_indicator;
	UILevelIndicator oxygen_indicator;
	UILevelIndicator acidity_indicator;
	UILevelIndicator salinity_indicator;
	DayNightIndicator day_indicator;
	
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

		// Load textures.
		texture::Load(Hash("floor"), "resources/tile/floor.png");
		texture::Load(Hash("nemo"), "resources/units/nemo_right.png");
		texture::Load(Hash("nemo_up"), "resources/units/nemo_up.png");
		texture::Load(Hash("nemo_down"), "resources/units/nemo_down.png");
		texture::Load(Hash("sucker"), "resources/units/sucker_right.png");
		texture::Load(Hash("sucker_up"), "resources/units/sucker_up.png");
		texture::Load(Hash("sucker_down"), "resources/units/sucker_down.png");
		texture::Load(Hash("shrimp"), "resources/units/shrimp_right.png");
		texture::Load(Hash("shrimp_up"), "resources/units/shrimp_up.png");
		texture::Load(Hash("shrimp_down"), "resources/units/shrimp_down.png");
		texture::Load(Hash("blue_nemo"), "resources/units/blue_nemo.png");
		texture::Load(Hash("jelly"), "resources/units/jelly.png");
		texture::Load(Hash("dory"), "resources/units/dory_right.png");
		texture::Load(Hash("dory_up"), "resources/units/dory_up.png");
		texture::Load(Hash("dory_down"), "resources/units/dory_down.png");
		texture::Load(Hash("goldfish"), "resources/units/goldfish.png");
		texture::Load(Hash("kelp"), "resources/structure/kelp.png");
		texture::Load(Hash("driftwood"), "resources/structure/driftwood.png");
		texture::Load(Hash("anenome"), "resources/structure/anenome.png");
		texture::Load(Hash("calcium_statue_1"), "resources/structure/calcium_statue_1.png");
		texture::Load(Hash("coral"), "resources/structure/coral.png");
		texture::Load(Hash("o_2_1"), "resources/particle/o_2_1.png");
		texture::Load(Hash("o_2_2"), "resources/particle/o_2_2.png");
		texture::Load(Hash("co_2_1"), "resources/particle/co_2_1.png");
		texture::Load(Hash("co_2_2"), "resources/particle/co_2_2.png");
		texture::Load(Hash("level_indicator"), "resources/ui/level_indicator.png");
		music::Load(Hash("ocean_loop"), "resources/music/ocean_loop.mp3");

		Reset();
	}

	void Reset() {
		// Setup node grid for the map.
		default_layout.ForEachPixel([&](const V2_int& coordinate, const Color& color) {
			Rectangle<float> rect{ coordinate * tile_size + tile_size / 2, tile_size };
			if (color == color::MAGENTA) {
				paths.push_back(CreatePath(manager, rect, coordinate, Hash("floor")));
			} else if (color == color::BLUE) {
				paths.push_back(CreatePath(manager, rect, coordinate, Hash("floor")));
				ecs::Entity spawn = CreateSpawn(manager, rect, coordinate);
				spawn_points.push_back(spawn);
				paths.push_back(CreatePath(manager, spawn.Get<Rectangle<float>>(), spawn.Get<TileComponent>().coordinate, Hash("floor")));
			} else if (color == color::DARK_GREEN) {
				CreateStructure(manager, rect, coordinate, Hash("kelp"), Particle::OXYGEN);
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

		music::Stop();
		music::Get(Hash("ocean_loop"))->Play(-1);

		day_timer.Reset();
		day_timer.Start();

		float tiles_from_left = 16.0f;
		float tiles_from_top = 0.5f;
		float spacing = 0.2f;

		crowding_indicator.Create("Crowding", color::BLACK, V2_float{tiles_from_left + 1.5f * 0 + spacing * 0, tiles_from_top}, manager);
		pollution_indicator.Create("Pollution", color::BLACK, V2_float{ tiles_from_left + 1.5f * 1 + spacing * 1, tiles_from_top }, manager);
		oxygen_indicator.Create("Oxygen", color::BLACK, V2_float{ tiles_from_left + 1.5f * 2 + spacing * 2, tiles_from_top }, manager);
		acidity_indicator.Create("Acidity", color::BLACK, V2_float{ tiles_from_left + 1.5f * 3 + spacing * 3, tiles_from_top }, manager);
		salinity_indicator.Create("Salinity", color::BLACK, V2_float{ tiles_from_left + 1.5f * 4 + spacing * 4, tiles_from_top }, manager);
		day_indicator.Create("Day/Night", color::BLACK, V2_float{ tiles_from_left + 1.5f * 5 + spacing * 5, tiles_from_top }, manager, V2_float{ 1.2f, 1.2f });

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
	Timer day_timer;
	void Update(float dt) final {
		V2_int mouse_pos = input::GetMousePosition();
		V2_int mouse_tile = V2_int{ V2_float{ mouse_pos } / V2_float{ tile_size } };
		Rectangle<float> mouse_box{ mouse_tile * tile_size + tile_size / 2, tile_size };
		Color mouse_box_color = color::GOLD;


		V2_int source;
		bool near_path = false;
		for (const ecs::Entity& e : paths) {
			assert(e.Has<TileComponent>());
			const TileComponent& tile = e.Get<TileComponent>();
			if (mouse_tile == tile.coordinate) {
				near_path = false;
				break;
			}
			V2_int dist = mouse_tile - tile.coordinate;
			if (dist.MagnitudeSquared() == 1) {
				source = tile.coordinate;
				near_path = true;
			}
		}

		bool can_place = near_path;
		if (can_place) {
			mouse_box_color = color::GREEN;
		}

		if (input::KeyDown(Key::ONE)) {
			CreateStructure(manager, mouse_box, mouse_tile, Hash("kelp"), Particle::OXYGEN);
		}
		if (input::KeyDown(Key::TWO)) {
			CreateStructure(manager, mouse_box, mouse_tile, Hash("coral"), Particle::CARBON_DIOXIDE);
		}
		if (input::KeyDown(Key::THREE)) {
			CreateStructure(manager, mouse_box, mouse_tile, Hash("calcium_statue_1"), Particle::NONE);
		}
		if (input::KeyDown(Key::FOUR)) {
			CreateStructure(manager, mouse_box, mouse_tile, Hash("driftwood"), Particle::NONE);
		}
		if (input::KeyDown(Key::FIVE) && can_place) {
			CreateStructure(manager, mouse_box, mouse_tile, Hash("anenome"), Particle::CARBON_DIOXIDE, Spawner::NEMO, source, paths);
		}

		if (input::MouseScroll() > 0) {
			oxygen_indicator.UpdateLevel(+0.1f);
		}
		if (input::MouseScroll() < 0) {
			oxygen_indicator.UpdateLevel(-0.1f);
		}
		if (input::KeyPressed(Key::UP)) {
			acidity_indicator.UpdateLevel(+0.1f);
		}
		if (input::KeyPressed(Key::DOWN)) {
			acidity_indicator.UpdateLevel(-0.1f);
		}
		

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

		if (input::KeyDown(Key::N)) {
			CreateFish(manager, spawn_rect, spawn_coordinate, "nemo", paths, 1.0f);
		}
		if (input::KeyDown(Key::S)) {
			CreateFish(manager, spawn_rect, spawn_coordinate, "sucker", paths, 0.8f);
		}
		if (input::KeyDown(Key::B)) {
			CreateFish(manager, spawn_rect, spawn_coordinate, "blue_nemo", paths, 1.0f);
		}
		if (input::KeyDown(Key::J)) {
			CreateFish(manager, spawn_rect, spawn_coordinate, "jelly", paths, 1.0f);
		}
		if (input::KeyDown(Key::D)) {
			CreateFish(manager, spawn_rect, spawn_coordinate, "dory", paths, 2.5f);
		}
		if (input::KeyDown(Key::G)) {
			CreateFish(manager, spawn_rect, spawn_coordinate, "goldfish", paths, 1.5f);
		}
		if (input::KeyDown(Key::R)) {
			CreateFish(manager, spawn_rect, spawn_coordinate, "shrimp", paths, 3.0f);
		}

		manager.ForEachEntityWith<ParticleComponent>([](
			ecs::Entity e, ParticleComponent& particle) {
			particle.Update();
		});

		/*manager.ForEachEntityWith<Rectangle<float>, TextureComponent, DrawComponent>([](
			ecs::Entity e, Rectangle<float>& rect,
			TextureComponent& texture, DrawComponent& draw) {
				rect.pos = Lerp(V2_float{ tile.coordinate * tile_size },
								V2_float{ (tile.coordinate + direction) * tile_size },
								waypoint.current);
		});*/

		auto draw_texture = [&](const ecs::Entity& e, Rectangle<float> rect, std::size_t texture_key) {
			V2_int og_pos = rect.pos;
			bool has_scale = e.Has<ScaleComponent>();
			V2_float scale = has_scale ? e.Get<ScaleComponent>().scale : V2_float{ 1.0f, 1.0f };
			rect.size *= scale;
			if (e.Has<OffsetComponent>()) {
				rect.pos += e.Get<OffsetComponent>().offset * scale;
			} else {
				rect.pos -= tile_size / 2;
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
			/*
			// DEBUG: Display positions.
			Circle<float> circle{ og_pos, 1 };
			circle.DrawSolid(color::RED);*/
		};

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

			while (waypoint.progress >= 1.0f) {
				prev_tile.coordinate = tile.coordinate;
				tile.coordinate = waypoint.target_tile;
				waypoint.target_tile = pathing.GetTargetTile(prev_tile.coordinate, tile.coordinate);
				pathing.IncreaseVisitCount(tile.coordinate);
				waypoint.progress -= 1.0f;
				if (waypoint.progress < 1.0f) {
					waypoint.target_tile = pathing.GetTargetTile(prev_tile.coordinate, tile.coordinate);
				}
			}

			rect.pos = Lerp(tile.coordinate * tile_size + tile_size / 2, waypoint.target_tile * tile_size + tile_size / 2, waypoint.progress);

			auto& particle_component = e.Get<ParticleComponent>();
			particle_component.SetSource(rect.pos);
		});

		manager.ForEachEntityWith<PathingComponent, TileComponent, Rectangle<float>, TextureComponent, FlipComponent, OffsetComponent, WaypointProgressComponent>([&](
			ecs::Entity e, PathingComponent& pathing, TileComponent& tile, Rectangle<float>& rect, TextureComponent& texture, FlipComponent& flip, OffsetComponent& offset, WaypointProgressComponent& waypoint) {
			if (texture.str_key != "") {
				bool right = waypoint.target_tile.x > tile.coordinate.x;
				bool up = waypoint.target_tile.y < tile.coordinate.y;
				bool horizontal = waypoint.target_tile.x != tile.coordinate.x;
				flip.flip = right || !horizontal ? Flip::NONE : Flip::HORIZONTAL;
				std::string dir = horizontal ? "" : up ? "_up" : "_down";
				std::size_t key = Hash((texture.str_key + dir).c_str());
				if (key != texture.key && texture::Has(key)) {
					texture.key = key;
					V2_int texture_size = texture::Get(texture.key)->GetSize();
					rect.size = texture_size;
					offset.offset = -texture_size / 2;
				}
			}
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

		manager.ForEachEntityWith<ParticleComponent>([](
			ecs::Entity e, ParticleComponent& particle) {
			particle.Draw();
		});

		manager.ForEachEntityWith<SpawnerComponent>([](
			ecs::Entity e, SpawnerComponent& spawner) {
			spawner.Update();
		});

		seconds day_length{ 10 };
		// Draw cyan filter on everything
		float elapsed = std::clamp(day_timer.ElapsedPercentage(day_length), 0.0f, 1.0f);



		static bool flip_day = false;

		std::uint8_t night_alpha = 128;

		float day_level = (flip_day ? (1.0 - elapsed) : elapsed);

		std::uint8_t time = night_alpha * day_level;

		if (elapsed >= 1.0f) {
			flip_day = !flip_day;
			day_timer.Reset();
			day_timer.Start();
		}

		Rectangle<float> bg{ {}, window::GetLogicalSize() };
		bg.DrawSolid({ 6, 64, 75, time });

		Color acidity_color = color::GREEN;
		if (acidity_indicator.GetLevel() < 0.5) {
			acidity_color.r = Lerp(255, 0, 2 * acidity_indicator.GetLevel());
			acidity_color.g = SmoothStepInterpolate(0.0f, 128.0f, 2 * acidity_indicator.GetLevel());
		} else {
			acidity_color.r = Lerp(0, 255, 2 * (acidity_indicator.GetLevel() - 0.5f));
			acidity_color.g = SmoothStepInterpolate(128.0f, 0.0f, 2 * (acidity_indicator.GetLevel() - 0.5f));
		}
		crowding_indicator.SetColor(color::GOLD);
		pollution_indicator.SetColor(color::GOLD);
		oxygen_indicator.SetColor(Lerp(Color{ 214, 255, 255, 255 }, color::BLACK, oxygen_indicator.GetLevel()));
		acidity_indicator.SetColor(acidity_color);
		salinity_indicator.SetColor(color::GOLD);
		day_indicator.SetLevel(day_level);

		crowding_indicator.Draw();
		pollution_indicator.Draw();
		oxygen_indicator.Draw();
		acidity_indicator.Draw();
		salinity_indicator.Draw();
		day_indicator.Draw();

		mouse_box.Offset(-tile_size / 2).Draw(mouse_box_color, 3);

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
		window::SetScale({ 16.0f, 16.0f });
		window::SetLogicalSize(grid_size * tile_size * window::GetScale());

		font::Load(Hash("04B_30"), "resources/font/04B_30.ttf", 32);
		font::Load(Hash("default_font"), "resources/font/retro_gaming.ttf", 32);
		//scene::Load<StartScreen>(Hash("menu"));
		scene::Load<GameScene>(Hash("game"));
		scene::SetActive(Hash("game"));
	}
};

int main(int c, char** v) {
	ptgn::game::Start<PixelJam2024>();
	return 0;
}
