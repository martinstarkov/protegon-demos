/*
#include <cassert>

#include "protegon/protegon.h"

using namespace ptgn;

class StartScreen;
class GameScene;

const inline V2_int grid_size{40, 23};
const inline V2_int tile_size{16, 16};
const inline std::size_t maximum_fish = 15;
const inline seconds day_length{10};
const inline seconds impact_length{day_length};
const inline seconds cycle_length{2 * day_length};
const inline V2_float pixel_scaling{2.0f, 2.0f};

struct OxygenComponent {};

struct TextureComponent {
  TextureComponent(std::size_t key, const std::string& str_key = "")
      : key{key}, str_key{str_key} {}
  std::size_t key{0};
  std::string str_key;
};

struct SpeedComponent {
  SpeedComponent(float speed) : speed{speed} {}
  float speed{0.0f};
};

struct TextureMapComponent {
  TextureMapComponent() = default;
  TextureMapComponent(const V2_int& coordinate) : coordinate{coordinate} {}
  V2_int coordinate;
};

struct TileComponent {
  TileComponent(const V2_int& coordinate) : coordinate{coordinate} {}
  V2_int coordinate;
};

struct PrevTileComponent : public TileComponent {
  using TileComponent::TileComponent;
};

struct ScaleComponent {
  ScaleComponent(const V2_float& scale) : scale{scale} {}
  V2_float scale{1.0f, 1.0f};
};

struct RotationComponent {
  RotationComponent() = default;
  RotationComponent(float angle) : angle{angle} {}
  float angle{0.0f};
};

struct FlipComponent {
  FlipComponent() = default;
  FlipComponent(Flip flip) : flip{flip} {}
  Flip flip{Flip::None};
};

struct WaypointProgressComponent {
  WaypointProgressComponent() = default;
  float progress{0.0f};
  V2_int target_tile;
};

struct StaticComponent {};
struct DrawComponent {};
struct FishComponent {};
struct PathComponent {};
struct StructureComponent {};

struct PlanterComponent {
 public:
  PlanterComponent() = delete;
  PlanterComponent(const V2_int& tile_location, std::size_t max_spawn_count,
                   milliseconds spawn_rate,
                   std::function<ecs::Entity(V2_int source)> func)
      : tile_location{tile_location},
        max_spawn_count{max_spawn_count},
        spawn_rate{spawn_rate},
        func{func} {}
  void Update(ecs::Manager& manager, const std::vector<ecs::Entity>& paths) {
    if (spawn_timer.Elapsed() > spawn_rate &&
        entities.size() < max_spawn_count) {
      V2_int range{3, 3};

      V2_int max = tile_location + range;
      V2_int min = tile_location - range;

      RNG<int> source_rng_x{min.x, max.x};
      RNG<int> source_rng_y{min.y, max.y};

      V2_int src_candidate;

      while (true) {
        src_candidate = {source_rng_x(), source_rng_y()};

        bool on_path = false;

        for (const ecs::Entity& e : paths) {
          PTGN_ASSERT(e.Has<TileComponent>());
          const TileComponent& tile = e.Get<TileComponent>();
          if (src_candidate == tile.coordinate) {
            on_path = true;
            break;
          }
        }

        if (on_path) continue;

        bool on_structure = false;

        for (auto [e, s, tile, t] : manager.EntitiesWith<StructureComponent, TileComponent,
                                  TextureComponent>()) {
              if (src_candidate == tile.coordinate) {
                on_structure = true;
                return;
              }
        }

        if (on_structure) continue;

        if (!on_path && !on_structure) {
          break;
        }
      }

      entities.push_back(func(src_candidate));
      spawn_timer.Start();
    }
    entities.erase(
        std::remove_if(entities.begin(), entities.end(),
                       [](const ecs::Entity& o) { return !o.IsAlive(); }),
        entities.end());
  }
  Timer spawn_timer;

 private:
  std::size_t max_spawn_count{0};
  milliseconds spawn_rate;
  std::function<ecs::Entity(V2_int source)> func;
  std::vector<ecs::Entity> entities;
  V2_int tile_location;
};

struct SpawnerComponent {
 public:
  SpawnerComponent() = delete;
  SpawnerComponent(std::size_t max_spawn_count, milliseconds spawn_rate,
                   std::function<ecs::Entity(V2_int source)> func)
      : max_spawn_count{max_spawn_count}, spawn_rate{spawn_rate}, func{func} {}
  void Update() {
    if (spawn_timer.Elapsed() > spawn_rate &&
        entities.size() < max_spawn_count) {
      entities.push_back(func(source));
      spawn_timer.Start();
    }
    entities.erase(
        std::remove_if(entities.begin(), entities.end(),
                       [](const ecs::Entity& o) { return !o.IsAlive(); }),
        entities.end());
  }
  void SetSource(const V2_int& new_source) { source = new_source; }
  Timer spawn_timer;

 private:
  std::size_t max_spawn_count{0};
  milliseconds spawn_rate;
  std::function<ecs::Entity(V2_int source)> func;
  std::vector<ecs::Entity> entities;
  V2_int source;
};

struct OffsetComponent {
  OffsetComponent() = default;
  OffsetComponent(const V2_int& offset) : offset{offset} {}
  V2_int offset;
};

std::vector<V2_int> GetNeighborTiles(const std::vector<ecs::Entity>& paths,
                                     const V2_int& tile) {
  std::vector<V2_int> neighbors;
  for (const auto& e : paths) {
    PTGN_ASSERT(e.Has<TileComponent>());
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
  PathingComponent(const std::vector<ecs::Entity>& paths,
                   const V2_int& start_tile) {
    for (auto& e : paths) {
      PTGN_ASSERT(e.Has<TileComponent>());
      path_visits.emplace(e.Get<TileComponent>().coordinate, 0);
    }
    IncreaseVisitCount(start_tile);
  }
  void IncreaseVisitCount(const V2_int& tile) {
    auto it = path_visits.find(tile);
    PTGN_ASSERT(it != path_visits.end() &&
           "Cannot increase visit count for tile which does not exist in "
           "pathing component");
    ++(it->second);
  }
  V2_int GetTargetTile(const V2_int& prev_tile, const V2_int& tile) const {
    std::vector<V2_int> neighbors = GetNeighborTiles(tile);
    V2_int new_tile = GetNextTile(prev_tile, neighbors);
    PTGN_ASSERT(new_tile != tile &&
           "Algorithm failed to find a new tile to move to");
    return new_tile;
  }
  V2_int GetNextTile(const V2_int& prev_tile,
                     const std::vector<V2_int>& neighbors) const {
    PTGN_ASSERT(neighbors.size() > 0 &&
           "Cannot get next tile when there exist no neighbors");
    if (neighbors.size() == 1) return neighbors.at(0);

    std::vector<V2_int> candidates;
    for (const V2_int& candidate : neighbors) {
      if (candidate == prev_tile) continue;
      candidates.emplace_back(candidate);
    }
    PTGN_ASSERT(candidates.size() > 0 &&
           "Algorithm failed to find two valid candidate tiles");
    if (candidates.size() == 1) {
      return candidates.at(0);
    }
    // Go through each tile and compare to the previous least visited tile
    // If the new tile is least visited, roll a 50/50 dice to choose which
    // becomes the new least visited path.
    RNG<int> fifty_fifty{0, 1};
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
      RNG<int> rng{0, static_cast<int>(candidates.size()) - 1};
      least_visited_tile = candidates.at(rng());
    }
    // In essence, each tile must roll lucky on two 50/50 rolls to become the
    // chosen path.
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
    PTGN_ASSERT(it != path_visits.end() &&
           "Cannot get visit count for tile which does not exist in pathing "
           "component");
    return it->second;
  }
  std::unordered_map<V2_int, int> path_visits;
};

struct EatingComponent {
  EatingComponent(seconds time) : time{time} {}
  Timer eating_timer;
  seconds time;
  bool has_begun = false;
};

struct VelocityComponent {
  VelocityComponent(const V2_float& vel) : vel{vel} {}
  V2_float vel;
};

struct LifetimeComponent {
  LifetimeComponent(milliseconds time) : time{time} {}
  Timer timer;
  milliseconds time{0};
};

struct IndicatorCondition {
  float crowding{0.0f};
  float pollution{0.0f};
  float oxygen{0.0f};
  float acidity{0.0f};
  float salinity{0.0f};
};

struct IndicatorImpactComponent {
  IndicatorImpactComponent(IndicatorCondition impact_points)
      : original_impact_points{impact_points}, impact_points{impact_points} {}
  IndicatorCondition original_impact_points;
  IndicatorCondition impact_points;
};

struct DeathComponent {
  DeathComponent() = default;
};

struct ParticleComponent {
 public:
  ParticleComponent() = delete;
  ParticleComponent(std::size_t max_particle_count,
                    const std::vector<std::size_t>& texture_keys,
                    milliseconds particle_lifetime, milliseconds spawn_rate)
      : max_particle_count{max_particle_count},
        texture_keys{texture_keys},
        particle_lifetime{particle_lifetime},
        spawn_rate{spawn_rate} {}
  void GenerateParticle() {
    if (manager.Size() >= max_particle_count) return;
    auto entity = manager.CreateEntity();
    RNG<int> rng{0, static_cast<int>(texture_keys.size()) - 1};
    int texture_index = rng();
    PTGN_ASSERT(texture_index < texture_keys.size());
    entity.Add<ScaleComponent>(V2_float{0.5f, 0.5f});
    std::size_t texture_key = texture_keys[texture_index];
    PTGN_ASSERT(game.texture.Has(texture_key));
    V2_int texture_size = game.texture.Get(texture_key).GetSize();
    entity.Add<Rect>(Rect{source, texture_size});

    PTGN_ASSERT(x_min_speed < x_max_speed);
    PTGN_ASSERT(y_min_speed < y_max_speed);

    RNG<float> rng_speed_x{x_min_speed, x_max_speed};
    RNG<float> rng_speed_y{y_min_speed, y_max_speed};

    entity.Add<TextureComponent>(texture_key);
    entity.Add<VelocityComponent>(V2_float{rng_speed_x(), rng_speed_y()});
    entity.Add<OffsetComponent>(-texture_size / 2);
    auto& lifetime = entity.Add<LifetimeComponent>(particle_lifetime);
    lifetime.timer.Start();
    manager.Refresh();
  }
  void Pause() {
    spawn_timer.Pause();
    for (auto [e, life] : manager.EntitiesWith<LifetimeComponent>()) {
      life.timer.Pause();
    }
  }
  void Unpause() {
    spawn_timer.Unpause();

    for (auto [e, life] : manager.EntitiesWith<LifetimeComponent>()) {
      life.timer.Unpause();
    }
  }
  void Update() {
    for (auto [e, rect, velocity, life] : manager.EntitiesWith<Rect, VelocityComponent,
                              LifetimeComponent>()) {
        rect.position += velocity.vel;
        velocity.vel *= 0.99f;
        milliseconds elapsed = life.timer.Elapsed();
        if (elapsed > life.time) {
          e.Destroy();
        }
    }
    if (spawn_timer.Elapsed() > spawn_rate) {
      GenerateParticle();
      spawn_timer.Start();
    }
    manager.Refresh();
  }
  // TODO: Add const ForEachEntityWith to ecs library.
  void Draw() {
    for (auto [e, rect, life, offset, texture, scale] : manager.EntitiesWith<Rect, LifetimeComponent,
                              OffsetComponent, TextureComponent,
                              ScaleComponent>()) {
          float elapsed = life.timer.ElapsedPercentage(life.time);
          std::uint8_t alpha = static_cast<std::uint8_t>((1.0f - elapsed) * 255);
          PTGN_ASSERT(game.texture.Has(texture.key));
          Texture t = game.texture.Get(texture.key);
          Color c{ 255, 255, 255, alpha };
          t.Draw(Rect{ rect.position + offset.offset * scale.scale, rect.size * scale.scale, rect.origin, rect.rotation }, TextureInfo{ {}, {}, Flip::None, c });
    }
    // Draw debug point to identify source of particles.
  }
  void SetSource(const V2_int& new_source) { source = new_source; }
  void SetSpeed(const V2_float& min, const V2_float& max) {
    x_min_speed = min.x;
    y_min_speed = min.y;
    x_max_speed = max.x;
    y_max_speed = max.y;
  }
  Timer spawn_timer;

 private:
  std::size_t max_particle_count{0};
  float x_min_speed{-0.08f};
  float x_max_speed{0.08f};
  float y_min_speed{-0.08f};
  float y_max_speed{-0.03f};
  V2_int source;
  milliseconds particle_lifetime;
  milliseconds spawn_rate;
  ecs::Manager manager;
  std::vector<std::size_t> texture_keys;
};

ecs::Entity CreatePath(ecs::Manager& manager, const Rect& rect,
                       const V2_int& coordinate, std::size_t key) {
  auto entity = manager.CreateEntity();
  entity.Add<PathComponent>();
  entity.Add<StaticComponent>();
  entity.Add<DrawComponent>();
  entity.Add<RotationComponent>();
  entity.Add<FlipComponent>();
  PTGN_ASSERT(game.texture.Has(key));
  entity.Add<TextureComponent>(key);
  entity.Add<TextureMapComponent>();

  entity.Add<TileComponent>(coordinate);
  entity.Add<Rect>(rect);
  manager.Refresh();
  return entity;
}

enum class Particle {
  NONE,
  OXYGEN,
  CARBON_DIOXIDE,
};

ecs::Entity CreateFish(ecs::Manager& manager, const Rect rect,
                       const V2_int coordinate, const std::string& str_key,
                       const std::vector<ecs::Entity>& paths, float speed) {
  auto entity = manager.CreateEntity();
  entity.Add<DrawComponent>();
  std::size_t key = Hash(str_key.c_str());
  PTGN_ASSERT(game.texture.Has(key));
  entity.Add<TextureComponent>(key, str_key);
  auto& waypoint = entity.Add<WaypointProgressComponent>();
  entity.Add<SpeedComponent>(speed);
  entity.Add<FlipComponent>();
  entity.Add<ScaleComponent>(V2_float{0.5f, 0.5f});
  auto& tile = entity.Add<TileComponent>(coordinate);
  V2_int texture_size = game.texture.Get(key).GetSize();
  entity.Add<OffsetComponent>(-texture_size / 2);
  entity.Add<Rect>(Rect{rect.position, texture_size});
  entity.Add<PrevTileComponent>(coordinate);
  entity.Add<FishComponent>();
  auto& pathing = entity.Add<PathingComponent>(paths, coordinate);
  waypoint.target_tile =
      pathing.GetTargetTile(tile.coordinate, tile.coordinate);
  auto& particle_component = entity.Add<ParticleComponent>(
      10,
      std::vector<std::size_t>{Hash("co_2_1"), Hash("co_2_2"), Hash("co_2_2"),
                               Hash("co_2_2")},
      milliseconds{500}, milliseconds{2000});
  particle_component.SetSpeed({-0.1f, -0.2f}, {0.1f, -0.1f});
  particle_component.SetSource(rect.position);
  particle_component.spawn_timer.Start();
  manager.Refresh();
  return entity;
}

enum class Spawner {
  NONE,
  NEMO,
  SHRIMP,
  SUCKER,
};

ecs::Entity CreateGoldfish(ecs::Manager& manager, const Rect rect,
                           const V2_int coordinate,
                           const std::vector<ecs::Entity>& paths) {
  return CreateFish(manager, rect, coordinate, "goldfish", paths, 1.5f);
}

ecs::Entity CreateDory(ecs::Manager& manager, const Rect rect,
                       const V2_int coordinate,
                       const std::vector<ecs::Entity>& paths) {
  return CreateFish(manager, rect, coordinate, "dory", paths, 2.5f);
}

ecs::Entity CreateSucker(ecs::Manager& manager, const Rect rect,
                         const V2_int coordinate,
                         const std::vector<ecs::Entity>& paths) {
  return CreateFish(manager, rect, coordinate, "sucker", paths, 0.8f);
}

ecs::Entity CreateShrimp(ecs::Manager& manager, const Rect rect,
                         const V2_int coordinate,
                         const std::vector<ecs::Entity>& paths) {
  return CreateFish(manager, rect, coordinate, "shrimp", paths, 3.0f);
}

ecs::Entity CreateNemo(ecs::Manager& manager, const Rect rect,
                       const V2_int coordinate,
                       const std::vector<ecs::Entity>& paths) {
  ecs::Entity nemo = CreateFish(manager, rect, coordinate, "nemo", paths, 1.3f);
  auto& eat = nemo.Add<EatingComponent>(seconds{3});
  eat.eating_timer.Start();
  return nemo;
}

ecs::Entity CreateRandomFish(int fish, ecs::Manager& manager,
                             const Rect rect,
                             const V2_int coordinate,
                             const std::vector<ecs::Entity>& paths) {
  switch (fish) {
    case 0:
      return CreateNemo(manager, rect, coordinate, paths);
    case 1:
      return CreateDory(manager, rect, coordinate, paths);
    case 2:
      return CreateGoldfish(manager, rect, coordinate, paths);
    case 3:
      return CreateSucker(manager, rect, coordinate, paths);
    case 4:
      return CreateShrimp(manager, rect, coordinate, paths);
    default: {
      PTGN_ASSERT(!"Fish index out of range");
      return ecs::null;
    }
  }
}

ecs::Entity CreateStructure(ecs::Manager& manager,
                            const Rect pos_rect,
                            const V2_int coordinate, std::size_t key,
                            Particle particle = Particle::NONE,
                            Spawner spawner = Spawner::NONE, V2_int source = {},
                            const std::vector<ecs::Entity>& paths = {}) {
  auto entity = manager.CreateEntity();
  entity.Add<DrawComponent>();
  entity.Add<StructureComponent>();
  PTGN_ASSERT(game.texture.Has(key));
  V2_int texture_size = game.texture.Get(key).GetSize();
  entity.Add<ScaleComponent>(V2_float{0.5f, 0.5f});
  entity.Add<TextureComponent>(key);

  auto& rect = entity.Add<Rect>(
      Rect{pos_rect.position, texture_size});
  entity.Add<OffsetComponent>(
      V2_int{-texture_size.x / 2, -texture_size.y + texture_size.y / 4});

  entity.Add<TileComponent>(coordinate);

  switch (particle) {
    case Particle::NONE:
      break;
    case Particle::OXYGEN: {
      auto& particle_component = entity.Add<ParticleComponent>(
          10,
          std::vector<std::size_t>{Hash("o_2_1"), Hash("o_2_2"), Hash("o_2_2"),
                                   Hash("o_2_2")},
          milliseconds{1000}, milliseconds{500});
      particle_component.SetSource(rect.position);
      particle_component.spawn_timer.Start();
      break;
    }
    case Particle::CARBON_DIOXIDE: {
      auto& particle_component = entity.Add<ParticleComponent>(
          10,
          std::vector<std::size_t>{Hash("co_2_1"), Hash("co_2_2"),
                                   Hash("co_2_2"), Hash("co_2_2")},
          milliseconds{1000}, milliseconds{500});
      particle_component.SetSource(rect.position);
      particle_component.spawn_timer.Start();
      break;
    }
  }
  if (spawner != Spawner::NONE) {
    PTGN_ASSERT(paths.size() > 0 &&
           "Must provide spawned fish with vector of path tiles");
  }

  entity.Add<LifetimeComponent>(impact_length);

  switch (spawner) {
    case Spawner::NONE:
      if (key == Hash("driftwood")) {
        IndicatorCondition impact;
        impact.acidity = 0.15f;
        impact.pollution = 0.08f;
        entity.Add<IndicatorImpactComponent>(impact);
      } else if (key == Hash("calcium_statue_1")) {
        IndicatorCondition impact;
        impact.acidity = -0.1f;
        impact.salinity = 0.08f;
        impact.pollution = 0.07f;
        entity.Add<IndicatorImpactComponent>(impact);
      } else if (key == Hash("seed_dispenser")) {
        IndicatorCondition impact;
        impact.oxygen = 0.3f;
        impact.pollution = 0.05f;
        entity.Add<IndicatorImpactComponent>(impact);
        std::size_t carrying_capacity = 7;
        auto& planter = entity.Add<PlanterComponent>(
            coordinate, carrying_capacity, milliseconds{1000},
            [&](V2_int source) {
              RNG<int> rng{0, 1};
              ecs::Entity kelp = CreateStructure(
                  manager,
                  {pixel_scaling * source * tile_size + tile_size / 2,
                   pixel_scaling * source},
                  source, rng() == 0 ? Hash("kelp_1") : Hash("kelp_2"),
                  Particle::OXYGEN);
              return kelp;
            });
        planter.spawn_timer.Start();
      }
      break;
    case Spawner::NEMO: {
      std::size_t carrying_capacity = 5;
      auto& spwn = entity.Add<SpawnerComponent>(
          carrying_capacity, milliseconds{6000}, [&](V2_int source) {
            ecs::Entity fish = CreateNemo(
                manager, {source * tile_size + tile_size / 2, source}, source,
                paths);
            return fish;
          });
      spwn.SetSource(source);
      spwn.spawn_timer.Start();
      IndicatorCondition impact;
      impact.crowding = 0.1f;
      impact.oxygen = -0.15f;
      entity.Add<IndicatorImpactComponent>(impact);
      break;
    }
    case Spawner::SHRIMP: {
      std::size_t carrying_capacity = 5;
      auto& spwn = entity.Add<SpawnerComponent>(
          carrying_capacity, milliseconds{4000}, [&](V2_int source) {
            ecs::Entity fish = CreateShrimp(
                manager, {source * tile_size + tile_size / 2, source}, source,
                paths);
            return fish;
          });
      spwn.SetSource(source);
      spwn.spawn_timer.Start();
      IndicatorCondition impact;
      impact.crowding = 0.2f;
      impact.oxygen = -0.05f;
      impact.salinity = -0.2f;
      entity.Add<IndicatorImpactComponent>(impact);
      break;
    }
    case Spawner::SUCKER: {
      std::size_t carrying_capacity = 3;
      auto& spwn = entity.Add<SpawnerComponent>(
          carrying_capacity, milliseconds{10000}, [&](V2_int source) {
            ecs::Entity fish = CreateSucker(
                manager, {source * tile_size + tile_size / 2, source}, source,
                paths);
            return fish;
          });
      spwn.SetSource(source);
      spwn.spawn_timer.Start();
      IndicatorCondition impact;
      impact.crowding = 0.07f;
      impact.oxygen = -0.1f;
      impact.pollution = -0.3f;
      entity.Add<IndicatorImpactComponent>(impact);
      break;
    }
  }

  manager.Refresh();
  return entity;
}

ecs::Entity CreateSpawn(ecs::Manager& manager, Rect rect,
                        V2_int coordinate) {
  // Move spawns outside of tile grid so fish go off screen.
  if (coordinate.x == 0) coordinate.x = -1;
  if (coordinate.x == grid_size.x - 1) coordinate.x = grid_size.x;
  if (coordinate.y == 0) coordinate.y = -1;
  if (coordinate.y == grid_size.y - 1) coordinate.y = grid_size.y;
  rect.position = coordinate * tile_size + tile_size / 2;

  auto entity = manager.CreateEntity();
  entity.Add<TileComponent>(coordinate);
  entity.Add<Rect>(rect);
  manager.Refresh();
  return entity;
}

struct UILevelComponent {};

struct TextComponent {
  TextComponent(std::size_t font_key, const char* content, const Color& color)
      : text{content, color, font_key} {}
  Text text;
};

struct UILevelIndicator {
 public:
  virtual ~UILevelIndicator() = default;
  Text level_text{Hash("default_font"), "", color::Black};
  void Create(const char* label, const Color& text_color,
              const V2_float& coordinate, ecs::Manager& manager,
              const V2_float scale = {0.75f, 0.75f}) {
    entity = manager.CreateEntity();
    std::size_t texture_key = Hash("level_indicator");
    PTGN_ASSERT(game.texture.Has(texture_key));
    Texture texture = game.texture.Get(texture_key);
    entity.Add<UILevelComponent>();
    entity.Add<ScaleComponent>(scale);
    entity.Add<TextureComponent>(texture_key);
    std::size_t indicator_font = Hash("default_font");
    entity.Add<TextComponent>(indicator_font, label, text_color);
    entity.Add<Rect>(
        Rect{V2_float{coordinate * tile_size}, texture.GetSize()});
    manager.Refresh();
  }
  virtual void Draw() {
    const Rect& rect = entity.Get<Rect>();
    const TextureComponent& texture = entity.Get<TextureComponent>();
    entity.Get<UILevelComponent>();
    const ScaleComponent& scale = entity.Get<ScaleComponent>();
    const TextComponent& text = entity.Get<TextComponent>();
    PTGN_ASSERT(game.texture.Has(texture.key));
    const Rect rect_scaled = rect.ScaleSize(scale.scale);
    // -1 from radius is so the circle fully fits inside the texture.
    Circle circle{rect_scaled.Center(), rect_scaled.Half().x - 1};
    circle.DrawSolidSliced(
        color, [&](double y_frac) { return y_frac >= 1.0f - level; });
    game.texture.Get(texture.key).Draw(rect_scaled);
    const Rect text_rect{
        V2_float{rect_scaled.position.x, -1.0f},
        V2_float{rect_scaled.size.x, 0.5f * tile_size.y}};
    text.text.Draw(text_rect);
    V2_float text_size = rect_scaled.Half() * 0.5f;
    level_text.SetContent(
        std::to_string(static_cast<int>(std::round(level * 100.0f))));
    level_text.Draw({rect_scaled.Center() - text_size / 2, text_size});
  }
  void UpdateLevel(float level_change) {
    level = std::clamp(level + level_change, 0.0f, 1.0f);
  }
  float GetLevel() const { return level; }
  float GetStartLevel() const { return starting_level; }
  void SetColor(const Color& new_color) { color = new_color; }
  void SetLevel(float new_level) { level = std::clamp(new_level, 0.0f, 1.0f); }
  void SetStartingLevel(float start_level) {
    starting_level = std::clamp(start_level, 0.0f, 1.0f);
  }

 protected:
  Color color;
  float level{0.0f};
  float starting_level{0.0f};
  ecs::Entity entity;
};

struct DayNightIndicator : public UILevelIndicator {
 public:
  using UILevelIndicator::UILevelIndicator;
  void Draw(bool night) {
    const Rect& rect = entity.Get<Rect>();
    const TextureComponent& texture = entity.Get<TextureComponent>();
    entity.Get<UILevelComponent>();
    const ScaleComponent& scale = entity.Get<ScaleComponent>();
    const TextComponent& text = entity.Get<TextComponent>();
    PTGN_ASSERT(game.texture.Has(texture.key));
    const Rect rect_scaled = rect.ScaleSize(scale.scale);

    Circle top_circle{rect_scaled.Center(), rect_scaled.Half().x - 1};
    top_circle.DrawSolidSliced(
        color::DarkBlue, [&](double y_frac) { return y_frac >= 1.0 - level; });

    Circle bottom_circle{rect_scaled.Center(), rect_scaled.Half().x - 1};
    bottom_circle.DrawSolidSliced(
        color::Gold, [&](double y_frac) { return y_frac <= 1.0 - level; });

    game.texture.Get(texture.key).Draw(rect_scaled);
    const Rect text_rect{
        V2_float{rect_scaled.position.x, -1.0f},
        V2_float{rect_scaled.size.x, 0.5f * tile_size.y}};
    text.text.Draw(text_rect);

    V2_float text_size = rect_scaled.Half() * 0.5f;
    int value = (static_cast<int>(std::round(level * 12)) % 12) + 1;
    if (night == true) {
      value = 13 - value;
    }
    level_text.SetContent(std::to_string(value));
    level_text.Draw({rect_scaled.Center() - text_size / 2, text_size});
  }
};

class ChoiceScreen : public Scene {
 public:
  // Text text0{ Hash("0"), "Stroll of the Dice", color::Cyan };

  std::array<Button, 8> choices;

  Text choice_text{Hash("default_font"), "", color::Black};
  Text skip_text{Hash("default_font"), "Skip", color::Black};

  std::size_t choice_count{0};

  ChoiceScreen() {
    for (size_t i = 0; i < 8; i++) {
      std::string name = "choice_" + std::to_string(i + 1);
      std::size_t key = Hash(name.c_str());
      std::size_t hover_key = Hash((name + "_hover").c_str());
      game.texture.Load(key, ("resources/ui/" + name + ".png").c_str());
      game.texture.Load(hover_key, ("resources/ui/" + name + "_hover.png").c_str());
      choices.at(i) = Button{{}, key, hover_key, hover_key};
    }

    game.texture.Load(Hash("choice_menu"), "resources/ui/choice_menu.png");

    V2_int choice_texture_size = game.texture.Get(Hash("choice_1")).GetSize();
    V2_int top_left{97, 63};
    V2_float choice_size = choice_texture_size / 2;

    V2_float choice_offset{choice_size.x + 17, choice_size.y + 17};

    for (auto& c : choices) {
      c.SetInteractable(true);
      c.SetVisibility(true);
    }

    for (size_t i = 0; i < 2; i++) {
      for (size_t j = 0; j < 4; j++) {
        std::size_t index = i * 4 + j;
        choices[index].SetRectangle({V2_float{top_left.x + choice_offset.x * j,
                                              top_left.y + choice_offset.y * i},
                                     choice_size});
      }
    }

    // This would not work in the for loop due to the index variable being
    // captured by reference. I did not have the time to figure out how to pass
    // this as reference and index as copy. Note: Choice index is incremented
    // for human readability.
    choices[0].SetOnActivate([&]() {
      StartChoosing(1);
      game.sound.Get("click").Play(1, 0);
    });
    choices[1].SetOnActivate([&]() {
      StartChoosing(2);
      game.sound.Get("click").Play(1, 0);
    });
    choices[2].SetOnActivate([&]() {
      StartChoosing(3);
      game.sound.Get("click").Play(1, 0);
    });
    choices[3].SetOnActivate([&]() {
      StartChoosing(4);
      game.sound.Get("click").Play(1, 0);
    });
    choices[4].SetOnActivate([&]() {
      StartChoosing(5);
      game.sound.Get("click").Play(1, 0);
    });
    choices[5].SetOnActivate([&]() {
      StartChoosing(6);
      game.sound.Get("click").Play(1, 0);
    });
    choices[6].SetOnActivate([&]() {
      StartChoosing(7);
      game.sound.Get("click").Play(1, 0);
    });
    choices[7].SetOnActivate([&]() {
      StartChoosing(8);
      game.sound.Get("click").Play(1, 0);
    });

    Reset();
  }
  void Reset() { choice_count = 3; }

  void Update() final {
    // TODO: Add a Enter() function for scenes and move this there (only trigger
    // Enter() once an active scene is activated from being inactive).
    for (auto& c : choices) {
      c.SetInteractable(true);
      c.SetVisibility(true);
    }

    // V2_int logical_size{ game.window.GetLogicalSize() };
    V2_int window_size{game.window.GetSize()};
    V2_int texture_size = game.texture.Get(Hash("choice_menu")).GetSize();
    Rect bg{(window_size - texture_size) / 2, texture_size};

    game.texture.Get(Hash("choice_menu")).Draw(bg);

    V2_int choice_text_size{V2_int{75, 15}};
    V2_int choice_text_pos =
        window_size / 2 - choice_text_size / 2 - V2_int{0, 1};

    choice_text_pos.y += 72;

    for (auto& c : choices) {
      c.Draw();
    }

    Rect skip_rect =
        choices[7].GetRectangle().ScaleSize(V2_float{0.9f, 0.3f});

    V2_int choice_texture_size = game.texture.Get(Hash("choice_1")).GetSize();
    V2_float choice_size = choice_texture_size / 2;
    Rect skip_text_rect{ skip_rect };
    skip_text_rect.position += V2_int{ choice_size.x / 2 - skip_rect.size.x / 2, choice_size.y / 2 - skip_rect.size.y / 2 };
    skip_text.Draw(skip_text_rect);

    std::string choice_content =
        "Choices Remaining: " + std::to_string(choice_count);
    choice_text.SetContent(choice_content.c_str());
    choice_text.Draw({choice_text_pos, choice_text_size});
  }
  void StartChoosing(int choice);

  void DisableButtons() {
    for (auto& c : choices) {
      c.SetVisibility(false);
      c.SetInteractable(false);
    }
  }
  void EnableButtons() {
    for (auto& c : choices) {
      c.SetVisibility(true);
      c.SetInteractable(true);
    }
  }
};

class GameScene : public Scene {
 public:
  std::array<Surface, 3> levels{Surface{"resources/maps/level_1.png"},
                                Surface{"resources/maps/level_2.png"},
                                Surface{"resources/maps/level_3.png"}};
  AStarGrid node_grid{grid_size};
  ecs::Manager manager;

  UILevelIndicator crowding_indicator;
  UILevelIndicator pollution_indicator;
  UILevelIndicator oxygen_indicator;
  UILevelIndicator acidity_indicator;
  UILevelIndicator salinity_indicator;
  DayNightIndicator day_indicator;

  json js;

  std::vector<ecs::Entity> spawn_points;
  std::vector<ecs::Entity> paths;

  Timer day_timer;
  Timer cycle_timer;
  bool choosing = false;
  bool paused{false};
  int choice_{-1};

  Button manage_button{
      {}, Hash("manage"), Hash("manage"), Hash("manage")};
  Button confirm_button{
      {}, Hash("confirm"), Hash("confirm"), Hash("confirm")};
  Button cancel_button{
      {}, Hash("cancel"), Hash("cancel"), Hash("cancel")};

  Text t_cancel{Hash("default_font"), "Cancel", color::White};
  Text t_manage{Hash("default_font"), "Manage", color::White};
  Text t_confirm{Hash("default_font"), "Confirm", color::White};

  const IndicatorCondition starting_conditions;

  int level_{0};

  GameScene(const IndicatorCondition& starting_conditions, int level)
      : starting_conditions{starting_conditions}, level_{level} {
    PTGN_ASSERT(level_ < levels.size() &&
           "Could not find level from list of levels");
    // game.window.SetLogicalSize(grid_size * tile_size);

    // Load textures.
    game.texture.Load(Hash("floor"), "resources/tile/floor.png");

    game.texture.Load(Hash("nemo"), "resources/units/nemo_right.png");
    game.texture.Load(Hash("nemo_up"), "resources/units/nemo_up.png");
    game.texture.Load(Hash("nemo_down"), "resources/units/nemo_down.png");
    game.texture.Load(Hash("sucker"), "resources/units/sucker_right.png");
    game.texture.Load(Hash("sucker_up"), "resources/units/sucker_up.png");
    game.texture.Load(Hash("sucker_down"), "resources/units/sucker_down.png");
    game.texture.Load(Hash("shrimp"), "resources/units/shrimp_right.png");
    game.texture.Load(Hash("shrimp_up"), "resources/units/shrimp_up.png");
    game.texture.Load(Hash("shrimp_down"), "resources/units/shrimp_down.png");
    game.texture.Load(Hash("dory"), "resources/units/dory_right.png");
    game.texture.Load(Hash("dory_up"), "resources/units/dory_up.png");
    game.texture.Load(Hash("dory_down"), "resources/units/dory_down.png");
    game.texture.Load(Hash("goldfish"), "resources/units/goldfish_right.png");
    game.texture.Load(Hash("goldfish_up"), "resources/units/goldfish_up.png");
    game.texture.Load(Hash("goldfish_down"), "resources/units/goldfish_down.png");

    game.texture.Load(Hash("kelp_1"), "resources/structure/kelp_1.png");
    game.texture.Load(Hash("kelp_2"), "resources/structure/kelp_2.png");
    game.texture.Load(Hash("driftwood"), "resources/structure/driftwood.png");
    game.texture.Load(Hash("anenome"), "resources/structure/anenome.png");
    game.texture.Load(Hash("yellow_anenome"),
                  "resources/structure/yellow_anenome.png");
    game.texture.Load(Hash("calcium_statue_1"),
                  "resources/structure/calcium_statue_1.png");
    game.texture.Load(Hash("coral"), "resources/structure/coral.png");
    game.texture.Load(Hash("bleached_coral"),
                  "resources/structure/bleached_coral.png");
    game.texture.Load(Hash("cave"), "resources/structure/cave.png");
    game.texture.Load(Hash("seed_dispenser"),
                  "resources/structure/seed_dispenser.png");
    game.texture.Load(Hash("delete"), "resources/structure/delete.png");

    game.texture.Load(Hash("o_2_1"), "resources/particle/o_2_1.png");
    game.texture.Load(Hash("o_2_2"), "resources/particle/o_2_2.png");
    game.texture.Load(Hash("co_2_1"), "resources/particle/co_2_1.png");
    game.texture.Load(Hash("co_2_2"), "resources/particle/co_2_2.png");

    game.texture.Load(Hash("level_indicator"), "resources/ui/level_indicator.png");
    game.texture.Load(Hash("choice_menu"), "resources/ui/choice_menu.png");
    game.texture.Load(Hash("manage"), "resources/ui/manage.png");
    game.texture.Load(Hash("confirm"), "resources/ui/confirm.png");
    game.texture.Load(Hash("cancel"), "resources/ui/cancel.png");

    game.texture.Load(Hash("exit"), "resources/ui/exit.png");
    game.texture.Load(Hash("exit_hover"), "resources/ui/exit_hover.png");

    game.sound.Load(Hash("sand"), "resources/sound/sand.wav");

    mute_button.SetOnActivate([&]() {
      game.sound.Get("click").Play(1, 0);
      game.music.Toggle();
      if (game.music.GetVolume() == 0) {
        game.sound.HaltChannel(0);
      } else {
        game.sound.ResumeChannel(0);
        game.sound.Get(Hash("aqualife_theme")).Play(0, -1);
      }
    });

    exit_button.SetOnActivate([&]() {
      game.sound.Get("click").Play(1, 0);
      Exit();
    });

    // game.texture.Load(Hash("blue_nemo"), "resources/units/blue_nemo.png");
    // game.texture.Load(Hash("jelly"), "resources/units/jelly.png");

    Reset();
  }

  Button mute_button{
      {},
      {Hash("mute"), Hash("mute_disabled")},
      {Hash("mute_hover"), Hash("mute_disabled_hover")},
      {Hash("mute_hover"), Hash("mute_hover")}};

  Button exit_button{
      {}, Hash("exit"), Hash("exit_hover"), Hash("exit_hover")};

  const std::size_t full_days_before_choices{1};

  std::size_t day{0};

  std::size_t cycles_since_choices{
      0};  // Replace with this to start with manage: // {
           // full_days_before_choices * 2 };

  void Reset() {
    // TODO: Do stuff with starting_conditios.
    V2_int window_size{game.window.GetSize()};

    // Setup node grid for the map.
    levels[level_].ForEachPixel([&](const V2_int& coordinate,
                                    const Color& color) {
      Rect rect{
          pixel_scaling * (coordinate * tile_size + tile_size / 2),
          pixel_scaling * tile_size};
      if (color == color::Silver) {
        paths.push_back(CreatePath(manager, rect, coordinate, Hash("floor")));
      } else if (color == color::Blue) {
        paths.push_back(CreatePath(manager, rect, coordinate, Hash("floor")));
        ecs::Entity spawn = CreateSpawn(manager, rect, coordinate);
        spawn_points.push_back(spawn);
        Rect r{spawn.Get<Rect>()};
        paths.push_back(
            CreatePath(manager, {pixel_scaling * r.position, pixel_scaling * r.size},
                       spawn.Get<TileComponent>().coordinate, Hash("floor")));
      } else if (color == color::DarkGreen) {
        RNG<int> rng{0, 1};
        CreateStructure(manager, rect, coordinate,
                        rng() == 0 ? Hash("kelp_1") : Hash("kelp_2"),
                        Particle::OXYGEN);
      } else if (color == color::Magenta) {
        CreateStructure(manager, rect, coordinate, Hash("coral"),
                        Particle::CARBON_DIOXIDE);
      }
      // else if (color == color::LightPink) {
      //	//CreateWall(rect, coordinate, 500);
      //	//node_grid.SetObstacle(coordinate, true);
      // }
      //
      // else if (color == color::Lime) {
      //	//end = CreateEnd(rect, coordinate);
      // }
    });

    day_timer.Start();
    cycle_timer.Start();

    day = 0;

    RerollFish();

    V2_int manage_texture_size =
        manage_button.GetCurrentTexture().GetSize() / 4;
    float manage_offset = 3;
    manage_button.SetRectangle(
        {{manage_offset, window_size.y - manage_texture_size.y - manage_offset},
         manage_texture_size});
    manage_button.SetOnActivate([&]() {
      game.sound.Get("click").Play(1, 0);
      PresentChoices();
    });
    manage_button.SetOnHover([&]() { t_manage.SetColor(color::Black); },
                             [&]() { t_manage.SetColor(color::White); });

    mute_button.SetRectangle(Rect{
        window_size - tile_size - V2_float{manage_offset, manage_offset},
        tile_size});
    exit_button.SetRectangle(
        Rect{V2_float{manage_offset, manage_offset}, tile_size});

    V2_int confirm_texture_size =
        confirm_button.GetCurrentTexture().GetSize() / 4;
    float choice_lock_offset = 0.05f * confirm_texture_size.x;
    confirm_button.SetRectangle(
        {{window_size.x / 2 - confirm_texture_size.x - choice_lock_offset,
          window_size.y - confirm_texture_size.y - manage_offset},
         confirm_texture_size});
    confirm_button.SetOnActivate([&]() {
      game.sound.Get("click").Play(1, 0);
      PTGN_ASSERT(game.scene.Has(Hash("choices")));
      if (choice_structure != ecs::null || delete_structure != ecs::null) {
        game.scene.Get<ChoiceScreen>(Hash("choices"))->choice_count--;
      }
      choice_structure = ecs::null;
      if (delete_structure != ecs::null) {
        if (delete_structure.Has<IndicatorImpactComponent>()) {
          auto impact =
              delete_structure.Get<IndicatorImpactComponent>().impact_points;
          // Reverse effects of deleted entity.
          auto reverser = manager.CreateEntity();

          IndicatorCondition reverser_impact;
          reverser_impact.acidity = impact.acidity * -1;
          reverser_impact.crowding = impact.crowding * -1;
          if (impact.pollution < 0) {
            reverser_impact.pollution = 0;
          } else {
            reverser_impact.pollution = impact.pollution * -1;
          }
          reverser_impact.salinity = impact.salinity * -1;
          reverser_impact.oxygen = impact.oxygen * -1;

          reverser.Add<IndicatorImpactComponent>(reverser_impact);
          // Repeat life of structure "backward" except this time with a death
          // component to clean it up at the end.
          reverser.Add<LifetimeComponent>(impact_length);
          reverser.Add<StructureComponent>();
          reverser.Add<DeathComponent>();
        }
        delete_structure.Destroy();
        manager.Refresh();
      }
      delete_structure = ecs::null;
      PresentChoices();
    });
    confirm_button.SetOnHover([&]() { t_confirm.SetColor(color::Black); },
                              [&]() { t_confirm.SetColor(color::White); });

    V2_int cancel_texture_size =
        cancel_button.GetCurrentTexture().GetSize() / 4;
    cancel_button.SetRectangle(
        {{window_size.x / 2 + choice_lock_offset,
          window_size.y - cancel_texture_size.y - manage_offset},
         cancel_texture_size});
    cancel_button.SetOnActivate([&]() {
      game.sound.Get("click").Play(1, 0);
      DestroyChoiceEntity();
      PresentChoices();
    });
    cancel_button.SetOnHover([&]() { t_cancel.SetColor(color::Black); },
                             [&]() { t_cancel.SetColor(color::White); });

    // Text t3{ Hash("2"), "Tower Offense", color::DarkGreen };
    // t3.Draw({ play_text_pos - V2_int{ 250, 160 }, { play_text_size.x + 500,
    // play_text_size.y } });

    bool manage_button_start = false;

    manage_button.SetInteractable(manage_button_start);
    manage_button.SetVisibility(manage_button_start);

    confirm_button.SetInteractable(false);
    confirm_button.SetVisibility(false);
    cancel_button.SetInteractable(false);
    cancel_button.SetVisibility(false);

    float tiles_from_left = 16.0f;
    float tiles_from_top = 0.5f;
    float spacing = 0.2f;

    crowding_indicator.Create(
        "Crowding", color::Black,
        V2_float{tiles_from_left + 1.5f * 0 + spacing * 0, tiles_from_top},
        manager);
    pollution_indicator.Create(
        "Pollution", color::Black,
        V2_float{tiles_from_left + 1.5f * 1 + spacing * 1, tiles_from_top},
        manager);
    oxygen_indicator.Create(
        "Oxygen", color::Black,
        V2_float{tiles_from_left + 1.5f * 2 + spacing * 2, tiles_from_top},
        manager);
    acidity_indicator.Create(
        "Acidity", color::Black,
        V2_float{tiles_from_left + 1.5f * 3 + spacing * 3, tiles_from_top},
        manager);
    salinity_indicator.Create(
        "Salinity", color::Black,
        V2_float{tiles_from_left + 1.5f * 4 + spacing * 4, tiles_from_top},
        manager);
    day_indicator.Create(
        "Day/Night", color::Black,
        V2_float{tiles_from_left + 1.5f * 5 + spacing * 5, tiles_from_top},
        manager, V2_float{1.2f, 1.2f});

    crowding_indicator.SetLevel(starting_conditions.crowding);
    pollution_indicator.SetLevel(starting_conditions.pollution);
    oxygen_indicator.SetLevel(starting_conditions.oxygen);
    acidity_indicator.SetLevel(starting_conditions.acidity);
    salinity_indicator.SetLevel(starting_conditions.salinity);

    crowding_indicator.SetStartingLevel(starting_conditions.crowding);
    pollution_indicator.SetStartingLevel(starting_conditions.pollution);
    oxygen_indicator.SetStartingLevel(starting_conditions.oxygen);
    acidity_indicator.SetStartingLevel(starting_conditions.acidity);
    salinity_indicator.SetStartingLevel(starting_conditions.salinity);

    for (auto [e, path, tile, texture_map, rotation, flip] : manager.EntitiesWith<PathComponent, TileComponent, TextureMapComponent,
                              RotationComponent, FlipComponent>()) {
          std::vector<V2_int> neighbors =
              GetNeighborTiles(paths, tile.coordinate);

          int x = 0;
          int y = 0;

          RNG<int> rng_2{0, 1};
          x = rng_2();

          if (neighbors.size() == 1) {
            // Either a spawn point or a dead end.
            bool spawn_point = false;
            for (const auto& ep : spawn_points) {
              if (tile.coordinate == ep.Get<TileComponent>().coordinate) {
                spawn_point = true;
              }
            }
            V2_int neighbor = neighbors.at(0);
            if (neighbor.y == tile.coordinate.y) {
              rotation.angle = 0.0f;
            } else {
              rotation.angle = 90.0f + 180.0f * rng_2();
            }
            if (spawn_point) {
              y = 1;
            } else {
              y = 2;
              rotation.angle = 90.0f * (tile.coordinate.y - neighbor.y);
              if (neighbor.x > tile.coordinate.x) flip.flip = Flip::Horizontal;
            }
          } else if (neighbors.size() == 2) {
            V2_int neighborA = neighbors.at(0);
            V2_int neighborB = neighbors.at(1);
            if (neighborA.x == neighborB.x) {
              y = 1;
              rotation.angle = 90.0f + 180.0f * rng_2();
            } else if (neighborA.y == neighborB.y) {
              y = 1;
              rotation.angle = 0.0f;
            } else {
              y = 3;
              RNG<int> rng_4{0, 3};
              x = rng_4();

              if (neighborA.x > tile.coordinate.x ||
                  neighborB.x > tile.coordinate.x)
                flip.flip = Flip::Horizontal;
              if (neighborA.y < tile.coordinate.y ||
                  neighborB.y < tile.coordinate.y)
                rotation.angle = 90.0f + 180.0f * static_cast<int>(flip.flip);
            }
          } else if (neighbors.size() == 3) {
            V2_int neighborA = neighbors.at(0);
            V2_int neighborB = neighbors.at(1);
            V2_int neighborC = neighbors.at(2);

            V2_int diffs =
                neighborA + neighborB + neighborC - 3 * tile.coordinate;
            rotation.angle = 90.0f * diffs.x;
            if (diffs.y > 0) rotation.angle += 180.0f;
            x = 0;
            y = 4;
          }
          texture_map.coordinate = {x, y};
        });

    if (manage_button_start) {
      Pause();
    } else {
      Unpause();
    }
    if (game.music.GetVolume() == 0)
      mute_button.SetToggleState(true);
    else
      mute_button.SetToggleState(false);
  }

  // spawn rect, spawn coordinate, spawn time, spawned
  std::vector<std::tuple<Rect, V2_int, seconds, bool>> fish_spawns;

  void RerollFish() {
    ++day;
    day_text.SetContent("Beginning of Day " + std::to_string(day));

    fade_in.Reset();
    fade_in.Start();

    fish_spawns.clear();

    float crowding_level = crowding_indicator.GetLevel();

    PTGN_ASSERT(crowding_level <= 1.0f);
    PTGN_ASSERT(crowding_level >= 0.0f);

    int potential_maximum = (int)(maximum_fish * (1.0f - crowding_level));

    RNG<int> fish_spawn_count_rng{potential_maximum / 4, potential_maximum};

    int fish_to_spawn = fish_spawn_count_rng();

    RNG<long long> spawn_time_rng{0, day_length.count()};

    for (int i = 0; i < fish_to_spawn; i++) {
      auto get_spawn_location = [&]() -> std::pair<Rect, V2_int> {
        RNG<int> rng{0, static_cast<int>(spawn_points.size()) - 1};
        int spawn_index = rng();
        ecs::Entity spawn_point = spawn_points.at(spawn_index);
        const Rect& rect = spawn_point.Get<Rect>();
        const TileComponent& tile = spawn_point.Get<TileComponent>();
        return {rect, tile.coordinate};
      };

      auto [spawn_rect, spawn_coordinate] = get_spawn_location();
      seconds spawn_time = seconds{spawn_time_rng()};

      fish_spawns.emplace_back(spawn_rect, spawn_coordinate, spawn_time, false);
    }
  }

  float day_speed = 1.0f;

  IndicatorCondition sum;

  void Update(float dt) final {
    // V2_int window_size{ game.window.GetLogicalSize() / window_scale };
    V2_int window_size{game.window.GetSize()};
    V2_int mouse_pos = game.input.GetMousePosition();
    V2_int mouse_tile = V2_int{V2_float{mouse_pos} / V2_float{tile_size}};
    Rect mouse_box{mouse_tile * tile_size + tile_size / 2,
                               tile_size};

    seconds time_passed{cycle_timer.Elapsed<seconds>()};

    RNG<int> fish_rng{0, 4};

    for (auto& [spawn_rect, spawn_coordinate, spawn_time, spawned] :
         fish_spawns) {
      if (!spawned && time_passed >= spawn_time / day_speed) {
        ecs::Entity fish = CreateRandomFish(fish_rng(), manager, spawn_rect,
                                            spawn_coordinate, paths);
        spawned = true;
      }
    }

    manager.Refresh();

    if (!paused) {
    }

    // Draw background tiles
    for (std::size_t i = 0; i < grid_size.x; i++) {
      for (std::size_t j = 0; j < grid_size.y; j++) {
        Rect r{V2_int{i, j} * tile_size, tile_size};
        game.texture.Get(Hash("floor")).Draw(r, {{0, 0}, tile_size});
      }
    }

    if (!paused) {
      // if (game.input.KeyDown(Key::N)) {
      //	CreateFish(manager, spawn_rect, spawn_coordinate, "nemo",
      // paths, 1.0f);
      // }
      // if (game.input.KeyDown(Key::S)) {
      //	CreateFish(manager, spawn_rect, spawn_coordinate, "sucker",
      // paths, 0.8f);
      // }
      // if (game.input.KeyDown(Key::D)) {
      //	CreateFish(manager, spawn_rect, spawn_coordinate, "dory",
      // paths, 2.5f);
      //}
      // if (game.input.KeyDown(Key::G)) {
      //	CreateFish(manager, spawn_rect, spawn_coordinate, "goldfish",
      // paths, 1.5f);
      //}
      // if (game.input.KeyDown(Key::R)) {
      //	CreateFish(manager, spawn_rect, spawn_coordinate, "shrimp",
      // paths, 3.0f);
      //}

      manager.ForEachEntityWith<ParticleComponent>(
          [](ecs::Entity e, ParticleComponent& particle) {
            particle.Update();
          });
    }

    auto draw_texture = [&](const ecs::Entity& e, Rect rect,
                            std::size_t texture_key) {
      V2_int og_pos = rect.position;
      bool has_scale = e.Has<ScaleComponent>();
      V2_float scale =
          has_scale ? e.Get<ScaleComponent>().scale : V2_float{1.0f, 1.0f};
      rect.size *= scale;
      if (e.Has<OffsetComponent>()) {
        rect.position += e.Get<OffsetComponent>().offset * scale;
      } else {
        rect.position -= tile_size / 2;
      }
      Rect source;
      if (e.Has<TextureMapComponent>()) {
        source.position = e.Get<TextureMapComponent>().coordinate * tile_size;
        source.size = tile_size;
      }
      float angle{0.0f};
      if (e.Has<RotationComponent>()) {
        angle = e.Get<RotationComponent>().angle;
      }
      Flip flip{Flip::None};
      if (e.Has<FlipComponent>()) {
        flip = e.Get<FlipComponent>().flip;
      }
      game.texture.Get(texture_key).Draw(rect, source, angle, flip);
    };

    manager
        .ForEachEntityWith<Rect, TextureComponent, DrawComponent>(
            [&](ecs::Entity e, Rect& rect,
                TextureComponent& texture, DrawComponent&) {
              if (texture.key == Hash("coral")) return;
              draw_texture(e, rect, texture.key);
            });

    // if (game.input.KeyPressed(Key::N)) {
    //	manager.ForEachEntityWith<PathingComponent, TileComponent>([&](
    //		ecs::Entity e, PathingComponent& pathing, TileComponent& tile) {
    //		std::vector<V2_int> neighbors =
    // pathing.GetNeighborTiles(tile.coordinate); 		for (const
    // V2_int& neighbor : neighbors) { 			Rect r{
    // neighbor * tile_size, tile_size };
    // r.DrawSolid(color::Red);
    //		}
    //		//game.texture.Get(texture.key).Draw(rect);
    //	});
    // }

    if (!paused) {
      manager.ForEachEntityWith<PathingComponent, TileComponent,
                                PrevTileComponent, Rect,
                                WaypointProgressComponent, SpeedComponent>(
          [&](ecs::Entity e, PathingComponent& pathing, TileComponent& tile,
              PrevTileComponent& prev_tile, Rect& rect,
              WaypointProgressComponent& waypoint, SpeedComponent& speed) {
            if (e.Has<EatingComponent>()) {
              auto& eating = e.Get<EatingComponent>();
              if (eating.eating_timer.ElapsedPercentage(eating.time) >= 1.0f) {
                RNG<int> rng_eat{0, 1};
                if (rng_eat() == 0) {
                  eating.has_begun = true;
                }
              }
            }

            waypoint.progress += dt * speed.speed * day_speed;

            while (waypoint.progress >= 1.0f) {
              prev_tile.coordinate = tile.coordinate;
              tile.coordinate = waypoint.target_tile;
              waypoint.target_tile =
                  pathing.GetTargetTile(prev_tile.coordinate, tile.coordinate);
              pathing.IncreaseVisitCount(tile.coordinate);
              waypoint.progress -= 1.0f;
              if (waypoint.progress < 1.0f) {
                waypoint.target_tile = pathing.GetTargetTile(
                    prev_tile.coordinate, tile.coordinate);
              }
            }

            rect.position = Lerp(tile.coordinate * tile_size + tile_size / 2,
                            waypoint.target_tile * tile_size + tile_size / 2,
                            waypoint.progress);

            auto& particle_component = e.Get<ParticleComponent>();
            particle_component.SetSource(rect.position);
          });

      manager.ForEachEntityWith<
          PathingComponent, TileComponent, Rect, TextureComponent,
          FlipComponent, OffsetComponent, WaypointProgressComponent>(
          [&](ecs::Entity e, PathingComponent&, TileComponent& tile,
              Rect& rect, TextureComponent& texture,
              FlipComponent& flip, OffsetComponent& offset,
              WaypointProgressComponent& waypoint) {
            if (texture.str_key != "") {
              bool right = waypoint.target_tile.x > tile.coordinate.x;
              bool up = waypoint.target_tile.y < tile.coordinate.y;
              bool horizontal = waypoint.target_tile.x != tile.coordinate.x;
              flip.flip = right || !horizontal ? Flip::None : Flip::Horizontal;
              std::string dir = horizontal ? "" : up ? "_up" : "_down";
              std::size_t key = Hash((texture.str_key + dir).c_str());
              if (key != texture.key && game.texture.Has(key)) {
                texture.key = key;
                V2_int texture_size = game.texture.Get(texture.key).GetSize();
                rect.size = texture_size;
                offset.offset = -texture_size / 2;
              }
            }
          });

      manager.ForEachEntityWith<PathingComponent, TileComponent,
                                PrevTileComponent, WaypointProgressComponent>(
          [&](ecs::Entity e, PathingComponent& pathing, TileComponent& tile,
              PrevTileComponent& prev_tile, WaypointProgressComponent&) {
            if (prev_tile.coordinate != tile.coordinate) {
              for (auto& spawn : spawn_points) {
                if (tile.coordinate == spawn.Get<TileComponent>().coordinate &&
                    pathing.GetVisitCount(tile.coordinate) > 0) {
                  e.Destroy();
                }
              }
            }
          });
    }

    manager.ForEachEntityWith<ParticleComponent>(
        [](ecs::Entity e, ParticleComponent& particle) { particle.Draw(); });

    if (!paused) {
      manager.ForEachEntityWith<SpawnerComponent>(
          [](ecs::Entity e, SpawnerComponent& spawner) { spawner.Update(); });

      manager.ForEachEntityWith<PlanterComponent>(
          [&](ecs::Entity e, PlanterComponent& planter) {
            planter.Update(manager, paths);
          });
    }

    if (game.input.KeyPressed(Key::S)) {
      day_speed = 10.0f;
    } else {
      day_speed = 1.0f;
    }

    // Draw cyan filter on everything
    float elapsed = std::clamp(
        day_timer.ElapsedPercentage(day_length) * day_speed, 0.0f, 1.0f);

    static bool flip_day = false;

    std::uint8_t night_alpha = 128;
    std::uint8_t acidity_alpha = 50;
    std::uint8_t pollution_alpha = 128;
    std::uint8_t salinity_alpha = 255;

    float day_level = (flip_day ? (1.0f - elapsed) : elapsed);

    std::uint8_t time = static_cast<std::uint8_t>(night_alpha * day_level);
    std::uint8_t acidity = static_cast<std::uint8_t>(
        acidity_alpha *
        std::clamp(acidity_indicator.GetLevel() - 0.5f, 0.0f, 1.0f));
    std::uint8_t pollution = static_cast<std::uint8_t>(
        pollution_alpha *
        std::clamp(pollution_indicator.GetLevel(), 0.0f, 1.0f));
    float salinity_elapsed = static_cast<std::uint8_t>(
        std::clamp(salinity_indicator.GetLevel(), 0.0f, 1.0f));
    float bleaching_start_threshold = 0.3f;
    std::uint8_t salinity =
        salinity_alpha * static_cast<std::uint8_t>(
                             salinity_elapsed < bleaching_start_threshold
                                 ? salinity_elapsed / bleaching_start_threshold
                                 : 1.0f);

    if (!paused) {
      if (elapsed >= 1.0f) {
        cycles_since_choices++;
        flip_day = !flip_day;
        day_timer.Reset();
        day_timer.Start();
      }
    }

    Rect bg{{}, game.window.GetResolution()};
    bg.DrawSolid({6, 64, 75, time});
    Color acidity_color = color::Yellow;
    Color pollution_color = color::Brown;
    Color salinity_bleacing = color::White;
    // salinity_bleacing.a = salinity;

    acidity_color.a = acidity;
    pollution_color.a = pollution;
    std::size_t bleached_key = Hash("bleached_coral");
    std::size_t coral_key = Hash("coral");

    manager.ForEachEntityWith<Rect, TextureComponent, DrawComponent,
                              StructureComponent>(
        [&](ecs::Entity e, Rect& rect, TextureComponent& texture,
            DrawComponent&, StructureComponent&) {
          if (texture.key == coral_key) {
            PTGN_ASSERT(game.texture.Has(texture.key));
            PTGN_ASSERT(game.texture.Has(bleached_key));
            game.texture.Get(texture.key).SetAlpha(salinity);
            game.texture.Get(bleached_key).SetAlpha(255 - salinity);
            draw_texture(e, rect, texture.key);
            draw_texture(e, rect, bleached_key);
          }
        });

    bg.DrawSolid(acidity_color);
    bg.DrawSolid(pollution_color);

    day_indicator.SetLevel(day_level);
    day_indicator.Draw(flip_day);

    if (!choosing && !paused) {
      sum = {};
      manager.ForEachEntityWith<IndicatorImpactComponent, LifetimeComponent>(
          [&](ecs::Entity e, IndicatorImpactComponent& impact,
              LifetimeComponent& life) {
            if (life.timer.IsRunning()) {
              float elapsed = std::clamp(
                  life.timer.ElapsedPercentage(life.time), 0.0f, 1.0f);
              sum.crowding += impact.impact_points.crowding * elapsed;
              sum.pollution += impact.impact_points.pollution * elapsed;
              sum.oxygen += impact.impact_points.oxygen * elapsed;
              sum.salinity += impact.impact_points.salinity * elapsed;
              sum.acidity += impact.impact_points.acidity * elapsed;
            }
          });
      crowding_indicator.SetLevel(crowding_indicator.GetStartLevel() +
                                  sum.crowding);
      pollution_indicator.SetLevel(pollution_indicator.GetStartLevel() +
                                   sum.pollution);
      oxygen_indicator.SetLevel(oxygen_indicator.GetStartLevel() + sum.oxygen);
      salinity_indicator.SetLevel(salinity_indicator.GetStartLevel() +
                                  sum.salinity);
      acidity_indicator.SetLevel(acidity_indicator.GetStartLevel() +
                                 sum.acidity);
    }

    std::array<std::reference_wrapper<UILevelIndicator>, 2> bot_indicators{
        pollution_indicator, crowding_indicator};

    std::array<std::reference_wrapper<UILevelIndicator>, 3> mid_indicators{
        oxygen_indicator, acidity_indicator, salinity_indicator};

    float indicator_tolerance = 0.1f;

    for (UILevelIndicator& indicator : bot_indicators) {
      float level = indicator.GetLevel();
      Color indicator_color = color::Green;
      if (level > indicator_tolerance) {
        indicator_color = Lerp(color::Green, color::Red,
                               std::clamp((level - indicator_tolerance) /
                                              (1.0f - indicator_tolerance),
                                          0.0f, 1.0f));
      }
      indicator.SetColor(indicator_color);
      indicator.Draw();
    }

    for (UILevelIndicator& indicator : mid_indicators) {
      Color indicator_color = color::Green;
      float level = indicator.GetLevel();
      if (level < 0.5f - indicator_tolerance) {
        indicator_color =
            Lerp(color::Green, color::Red,
                 std::clamp(1.0f - level / (0.5f - indicator_tolerance), 0.0f,
                            1.0f));
      } else if (level > 0.5f + indicator_tolerance) {
        indicator_color =
            Lerp(color::Green, color::Red,
                 std::clamp((level - (0.5f + indicator_tolerance)) /
                                (0.5f - indicator_tolerance),
                            0.0f, 1.0f));
      }
      indicator.SetColor(indicator_color);
      indicator.Draw();
    }

    manage_button.Draw();
    confirm_button.Draw();
    cancel_button.Draw();
    mute_button.Draw();
    exit_button.Draw();

    V2_int manage_texture_size{manage_button.GetRectangle().size * 0.8};
    V2_int manage_text_size{manage_texture_size};
    V2_int manage_text_pos = manage_button.GetRectangle().position +
                             manage_button.GetRectangle().size / 2 -
                             manage_text_size / 2;
    t_manage.SetVisibility(manage_button.GetVisibility());
    t_manage.Draw({manage_text_pos, manage_text_size});

    V2_int confirm_texture_size{confirm_button.GetRectangle().size * 0.8};
    V2_int confirm_text_size{confirm_texture_size};
    V2_int confirm_text_pos = confirm_button.GetRectangle().position +
                              confirm_button.GetRectangle().size / 2 -
                              confirm_text_size / 2;
    t_confirm.SetVisibility(confirm_button.GetVisibility());
    t_confirm.Draw({confirm_text_pos, confirm_text_size});

    V2_int cancel_texture_size{cancel_button.GetRectangle().size * 0.8};
    V2_int cancel_text_size{cancel_texture_size};
    V2_int cancel_text_pos = cancel_button.GetRectangle().position +
                             cancel_button.GetRectangle().size / 2 -
                             cancel_text_size / 2;
    t_cancel.SetVisibility(cancel_button.GetVisibility());
    t_cancel.Draw({cancel_text_pos, cancel_text_size});

    if (choosing && choice_ != -1) {
      V2_int source;
      bool near_path = false;
      bool on_path = false;
      for (const ecs::Entity& e : paths) {
        PTGN_ASSERT(e.Has<TileComponent>());
        const TileComponent& tile = e.Get<TileComponent>();
        if (mouse_tile == tile.coordinate) {
          near_path = false;
          on_path = true;
          break;
        }
        V2_int dist = mouse_tile - tile.coordinate;
        if (dist.MagnitudeSquared() == 1) {
          source = tile.coordinate;
          near_path = true;
        }
      }

      auto mouse_rect = mouse_box.Offset(-tile_size / 2);

      bool can_place = true;

      std::size_t key = Hash("invalid");
      Particle particle = Particle::NONE;
      Spawner spawner = Spawner::NONE;

      bool removing = false;

      switch (choice_) {
        case 1: {
          key = Hash("anenome");
          particle = Particle::CARBON_DIOXIDE;
          spawner = Spawner::NEMO;
          can_place = near_path;
          break;
        }
        case 2: {
          key = Hash("cave");
          particle = Particle::CARBON_DIOXIDE;
          spawner = Spawner::SUCKER;
          can_place = near_path;
          break;
        }
        case 3: {
          key = Hash("yellow_anenome");
          particle = Particle::CARBON_DIOXIDE;
          spawner = Spawner::SHRIMP;
          can_place = near_path;
          break;
        }
        case 4: {
          // Special case for deletion choice.
          // RNG<int> rng{ 0, 1 };
          // key = rng() == 0 ? Hash("kelp_1") : Hash("kelp_2");
          // particle = Particle::OXYGEN;
          removing = true;
          can_place = true;
          break;
        }
        case 5: {
          key = Hash("seed_dispenser");
          can_place = !on_path && !near_path;
          break;
        }
        case 6: {
          key = Hash("calcium_statue_1");
          can_place = !on_path;
          break;
        }
        case 7: {
          key = Hash("driftwood");
          can_place = !on_path;
          break;
        }
        case 8: {
          PTGN_ASSERT(!"Choice 8 should be dealt with beforehand");
          break;
        }

        default:
          PTGN_ASSERT(!"Choice outside of range, something went wrong");
          break;
      }

      ecs::Entity mouse_entity = ecs::null;

      manager.ForEachEntityWith<StructureComponent, TileComponent,
                                TextureComponent>(
          [&](ecs::Entity e, StructureComponent&, TileComponent& tile,
              TextureComponent& texture) {
            if (mouse_tile == tile.coordinate) {
              if (!removing) can_place = false;
              if (texture.key == Hash("coral") ||
                  texture.key == Hash("kelp_1") ||
                  texture.key == Hash("kelp_2"))
                can_place = false;
              if (removing && !can_place) return;
              mouse_entity = e;
            }
          });

      bool over_structure = mouse_entity != ecs::null;

      bool over_button =
          confirm_button.InsideRectangle(mouse_pos) ||
          cancel_button.InsideRectangle(mouse_pos) ||
          mute_button.InsideRectangle(mouse_pos) ||
          exit_button.InsideRectangle(mouse_pos) ||
          overlap::RectangleRectangle(mouse_rect,
                                      confirm_button.GetRectangle()) ||
          overlap::RectangleRectangle(mouse_rect,
                                      cancel_button.GetRectangle()) ||
          overlap::RectangleRectangle(mouse_rect, mute_button.GetRectangle()) ||
          overlap::RectangleRectangle(mouse_rect, exit_button.GetRectangle());

      bool permit = can_place && !over_button;

      Color mouse_box_color = color::Red;

      if (permit) {
        mouse_box_color = color::Green;
      }

      std::uint8_t mouse_box_opacity = 128;

      if (removing && permit && over_structure) {
        if (game.input.MouseHeld(Mouse::LEFT, milliseconds{10})) {
          delete_structure = mouse_entity;
        } else if (game.input.MouseUp(Mouse::RIGHT)) {
          delete_structure = ecs::null;
        }
        mouse_box_color = color::Red;
      } else {
        if (!removing && permit &&
            game.input.MouseHeld(Mouse::LEFT, milliseconds{10})) {
          if (choice_structure != ecs::null) {
            DestroyChoiceEntity();
          }
          game.sound.HaltChannel(2);
          game.sound.Get(Hash("sand")).Play(2, 0);
          choice_structure =
              CreateStructure(manager, mouse_box, mouse_tile, key, particle,
                              spawner, source, paths);
        }

        if (!removing && choice_structure != ecs::null) {
          PTGN_ASSERT(choice_structure.Has<TileComponent>());
          const TileComponent& tile = choice_structure.Get<TileComponent>();
          Rect choice_rect{tile.coordinate * tile_size, tile_size};
          choice_rect.Draw(color::Green, 3);
          if (tile.coordinate == mouse_tile) {
            mouse_box_color = color::Green;
            mouse_box_opacity = 255;
            if (game.input.MouseUp(Mouse::RIGHT)) {
              DestroyChoiceEntity();
            }
          }
        }
      }

      if ((!removing && !over_button) ||
          (removing && !over_button && over_structure)) {
        mouse_box_color.a = mouse_box_opacity;
        mouse_rect.Draw(mouse_box_color, 3);
      }

      if (!over_button && !removing && key != Hash("invalid")) {
        PTGN_ASSERT(game.texture.Has(key));
        V2_int texture_size = game.texture.Get(key).GetSize();
        V2_float scale{0.5f, 0.5f};
        Rect rect{mouse_box.position, texture_size};
        V2_int offset{-texture_size.x / 2,
                      -texture_size.y + texture_size.y / 4};

        rect.size *= scale;
        rect.position += offset * scale;
        Texture texture = game.texture.Get(key);
        texture.SetAlpha(180);
        texture.Draw(rect);
      }

      if (removing && delete_structure != ecs::null) {
        PTGN_ASSERT(delete_structure.Has<TileComponent>());
        const TileComponent& delete_tile =
            delete_structure.Get<TileComponent>();
        Rect delete_rect{delete_tile.coordinate * tile_size,
                                     tile_size};
        delete_rect.Draw(color::DarkRed, 3);
        std::size_t delete_key = Hash("delete");
        PTGN_ASSERT(game.texture.Has(delete_key));
        Texture texture = game.texture.Get(delete_key);
        texture.Draw(delete_rect);
      }
      // Text choice_text{ Hash("default_font"), std::to_string(choice_),
      // color::Black }; choice_text.Draw(mouse_rect);
    }

    Color day_text_color = color::Black;

    if (fade_in.IsRunning()) {
      milliseconds elapsed_fade_in = fade_in.Elapsed<milliseconds>();
      if (elapsed_fade_in <= milliseconds{2000}) {
        day_text.SetVisibility(true);
        day_text_color = {
            color::Black.r, color::Black.g, color::Black.b,
            static_cast<std::uint8_t>(
                255 * (std::clamp(fade_in.ElapsedPercentage(milliseconds{2000}),
                                  0.0f, 1.0f)))};
      } else if (elapsed_fade_in <= milliseconds{3000}) {
        day_text_color = color::Black;
        day_text.SetVisibility(false);
      } else if (elapsed_fade_in <= milliseconds{4000} &&
                 !fade_out.IsRunning()) {
        day_text_color = color::Black;
        fade_out.Start();
        fade_in.Stop();
        day_text.SetVisibility(false);
      }
    } else {
      day_text.SetVisibility(false);
    }

    if (day_text.GetVisibility()) {
      V2_float day_text_size{75, 15};
      day_text.SetColor(day_text_color);
      day_text.Draw({window_size / 2 - day_text_size / 2, day_text_size});
    }

    manager.Refresh();

    if (cycles_since_choices >= 2 * full_days_before_choices) {
      StartChoices();
    }

    if (!choosing && day_timer.IsPaused() && game.input.KeyDown(Key::ESCAPE)) {
      manage_button.SetInteractable(true);
      manage_button.SetVisibility(true);
      if (game.scene.Has(Hash("choices"))) game.scene.RemoveActive(Hash("choices"));
    }
  }

  ecs::Entity delete_structure;
  ecs::Entity choice_structure;
  Text day_text{ "", color::Black, "default_font" };
  Timer fade_in;
  Timer fade_out;

  void DestroyChoiceEntity() {
    choice_structure.Destroy();
    manager.Refresh();
    delete_structure = ecs::null;
    choice_structure = ecs::null;
  }

  void StopChoices() {
    choice_ = -1;
    choosing = false;
    RerollFish();
    cycle_timer.Start();
    manager.ForEachEntityWith<LifetimeComponent, StructureComponent>(
        [](ecs::Entity e, LifetimeComponent& life, StructureComponent&) {
          if (!life.timer.IsRunning()) life.timer.Start();
        });
    game.scene.Get<ChoiceScreen>(Hash("choices"))->DisableButtons();
    Unpause();
    manage_button.SetInteractable(false);
    manage_button.SetVisibility(false);
  }
  void StartChoices() {
    crowding_indicator.SetStartingLevel(crowding_indicator.GetLevel());
    pollution_indicator.SetStartingLevel(pollution_indicator.GetLevel());
    oxygen_indicator.SetStartingLevel(oxygen_indicator.GetLevel());
    salinity_indicator.SetStartingLevel(salinity_indicator.GetLevel());
    acidity_indicator.SetStartingLevel(acidity_indicator.GetLevel());

    manager.ForEachEntityWith<LifetimeComponent, StructureComponent>(
        [](ecs::Entity e, LifetimeComponent&, StructureComponent&) {
          e.Remove<LifetimeComponent>();
          if (e.Has<DeathComponent>()) {
            e.Destroy();
          }
        });

    day_text.SetVisibility(false);

    manager.Refresh();

    cycle_timer.Reset();
    cycle_timer.Stop();
    cycles_since_choices = 0;
    Pause();
    if (game.scene.Has(Hash("choices"))) {
      game.scene.Get<ChoiceScreen>(Hash("choices"))->Reset();
    }

    manage_button.SetInteractable(true);
    manage_button.SetVisibility(true);
  }
  void Pause() {
    paused = true;
    manager.ForEachEntityWith<SpawnerComponent>(
        [](ecs::Entity e, SpawnerComponent& spawner) {
          spawner.spawn_timer.Pause();
        });
    manager.ForEachEntityWith<PlanterComponent>(
        [](ecs::Entity e, PlanterComponent& planter) {
          planter.spawn_timer.Pause();
        });
    manager.ForEachEntityWith<EatingComponent>(
        [](ecs::Entity e, EatingComponent& eat) { eat.eating_timer.Pause(); });
    manager.ForEachEntityWith<LifetimeComponent>(
        [](ecs::Entity e, LifetimeComponent& life) { life.timer.Pause(); });
    manager.ForEachEntityWith<ParticleComponent>(
        [](ecs::Entity e, ParticleComponent& particle) { particle.Pause(); });
    day_timer.Pause();
    cycle_timer.Pause();
  }
  void Unpause() {
    paused = false;
    manager.ForEachEntityWith<SpawnerComponent>(
        [](ecs::Entity e, SpawnerComponent& spawner) {
          spawner.spawn_timer.Unpause();
        });
    manager.ForEachEntityWith<EatingComponent>(
        [](ecs::Entity e, EatingComponent& eat) {
          eat.eating_timer.Unpause();
        });
    manager.ForEachEntityWith<PlanterComponent>(
        [](ecs::Entity e, PlanterComponent& planter) {
          planter.spawn_timer.Unpause();
        });
    manager.ForEachEntityWith<LifetimeComponent>(
        [](ecs::Entity e, LifetimeComponent& life) { life.timer.Unpause(); });
    manager.ForEachEntityWith<ParticleComponent>(
        [](ecs::Entity e, ParticleComponent& particle) { particle.Unpause(); });
    day_timer.Unpause();
    cycle_timer.Unpause();
  }
  void Exit();
  void PresentChoices() {
    manage_button.SetVisibility(false);
    manage_button.SetInteractable(false);
    choosing = false;
    ToggleChoiceButtons();
    if (!game.scene.Has(Hash("choices"))) {
      game.scene.Load<ChoiceScreen>(Hash("choices"));
    }
    game.scene.Get<ChoiceScreen>(Hash("choices"))->EnableButtons();
    if (game.scene.Get<ChoiceScreen>(Hash("choices"))->choice_count > 0) {
      game.scene.AddActive(Hash("choices"));
    } else {
      RanOutOfChoices();
    }
  }
  void RanOutOfChoices() {
    StopChoices();
    // TODO: Perhaps add an animation sequence here for start of the day?
  }
  void Choosing(int choice) {
    choice_ = choice;
    choosing = true;
    ToggleChoiceButtons();
  }
  void ToggleChoiceButtons() {
    confirm_button.SetVisibility(choosing);
    confirm_button.SetInteractable(choosing);
    cancel_button.SetVisibility(choosing);
    cancel_button.SetInteractable(choosing);
  }
};

class LevelScene : public Scene {
 public:
  // Text text0{ Hash("0"), "Stroll of the Dice", color::Cyan };

  Button level1{{}, Hash("level"), Hash("level"), Hash("level")};
  Button level2{{}, Hash("level"), Hash("level"), Hash("level")};
  Button level3{{}, Hash("level"), Hash("level"), Hash("level")};

  Button back{{}, Hash("back"), Hash("back"), Hash("back")};

  Color level1_text_color{color::White};
  Color level2_text_color{color::White};
  Color level3_text_color{color::White};

  Color back_text_color{color::White};

  TexturedToggleButton mute_button{
      {},
      {Hash("mute"), Hash("mute_disabled")},
      {Hash("mute_hover"), Hash("mute_disabled_hover")},
      {Hash("mute_hover"), Hash("mute_hover")}};

  LevelScene() {
    // game.window.SetLogicalSize({ 1689, 1001 });

    game.texture.Load(Hash("level"), "resources/ui/level.png");
    game.texture.Load(Hash("back"), "resources/ui/back.png");
    game.texture.Load(Hash("level_background"),
                  "resources/ui/level_background.png");

    mute_button.SetOnActivate([&]() {
      game.sound.Get("click").Play(1, 0);
      game.music.Toggle();
      if (game.music.GetVolume() == 0) {
        game.sound.HaltChannel(0);
      } else {
        game.sound.ResumeChannel(0);
        game.sound.Get(Hash("aqualife_theme")).Play(0, -1);
      }
    });

    V2_int window_size{game.window.GetSize()};

    V2_float mute_offset = {12, 12};
    V2_float mute_size{62, 62};
    mute_button.SetRectangle(
        Rect{window_size - mute_size - mute_offset, mute_size});
    if (game.music.GetVolume() == 0)
      mute_button.SetToggleState(true);
    else
      mute_button.SetToggleState(false);
  }
  static void Exit();
  void Update() final {
    V2_int window_size{game.window.GetSize()};
    V2_int texture_size = game.texture.Get(Hash("level_background")).GetSize();
    Rect bg{(window_size - texture_size) / 2, texture_size};
    // bg.DrawSolid(color::Blue);
    game.texture.Get(Hash("level_background")).Draw(bg);

    V2_int level_texture_size = level1.GetCurrentTexture().GetSize();
    V2_int back_texture_size = back.GetCurrentTexture().GetSize();

    V2_int button_offset{static_cast<int>(level_texture_size.x * 1.12), 0};
    V2_int back_offset{0, static_cast<int>(level_texture_size.y * 1.16)};

    level1.SetRectangle(
        {window_size / 2 - level_texture_size / 2 - button_offset,
         level_texture_size});
    level2.SetRectangle(
        {window_size / 2 - level_texture_size / 2, level_texture_size});
    level3.SetRectangle(
        {window_size / 2 - level_texture_size / 2 + button_offset,
         level_texture_size});
    back.SetRectangle({window_size / 2 - back_texture_size / 2 + back_offset,
                       back_texture_size});

    V2_int level_text_size{level_texture_size.x * 0.8,
                           level_texture_size.y * 0.3};
    V2_int back_text_size{back_texture_size.x * 0.7, back_texture_size.y * 0.7};

    V2_int level1_text_pos =
        window_size / 2 - level_text_size / 2 - button_offset;
    V2_int level2_text_pos = window_size / 2 - level_text_size / 2;
    V2_int level3_text_pos =
        window_size / 2 - level_text_size / 2 + button_offset;
    V2_int back_text_pos = window_size / 2 - back_text_size / 2 + back_offset;

    auto play_press = [&](const IndicatorCondition& starting_conditions,
                          int level) {
      game.scene.Load<GameScene>(Hash("game"), starting_conditions, level);
      game.scene.Unload(Hash("level_select"));
      game.scene.SetActive(Hash("game"));
    };

    level1.SetOnActivate([&]() {
      play_press({0.3f, 0.7f, 0.5f, 0.3f, 0.6f}, 0);
      game.sound.Get("click").Play(1, 0);
    });
    level2.SetOnActivate([&]() {
      play_press({0.4f, 0.7f, 0.1f, 0.1f, 0.6f}, 1);
      game.sound.Get("click").Play(1, 0);
    });
    level3.SetOnActivate([&]() {
      play_press({0.4f, 1.0f, 0.8f, 1.0f, 1.0f}, 2);
      game.sound.Get("click").Play(1, 0);
    });

    back.SetOnActivate([&]() {
      game.sound.Get("click").Play(1, 0);
      Exit();
    });

    level1.SetOnHover([&]() { level1_text_color = color::Black; },
                      [&]() { level1_text_color = color::White; });
    level2.SetOnHover([&]() { level2_text_color = color::Black; },
                      [&]() { level2_text_color = color::White; });
    level3.SetOnHover([&]() { level3_text_color = color::Black; },
                      [&]() { level3_text_color = color::White; });

    back.SetOnHover([&]() { back_text_color = color::Black; },
                    [&]() { back_text_color = color::White; });

    level1.Draw();
    level2.Draw();
    level3.Draw();
    back.Draw();
    mute_button.Draw();

    // Text t3{ Hash("2"), "Tower Offense", color::DarkGreen };
    // t3.Draw({ play_text_pos - V2_int{ 250, 160 }, { play_text_size.x + 500,
    // play_text_size.y } });

    Text t1{Hash("default_font"), "Level 1", level1_text_color};
    Text t2{Hash("default_font"), "Level 2", level2_text_color};
    Text t3{Hash("default_font"), "Level 3", level3_text_color};
    Text back_text{Hash("default_font"), "Back", back_text_color};
    t1.Draw({level1_text_pos, level_text_size});
    t2.Draw({level2_text_pos, level_text_size});
    t3.Draw({level3_text_pos, level_text_size});
    back_text.Draw({back_text_pos, back_text_size});

    if (game.input.KeyDown(Key::ESCAPE)) {
      Exit();
    }
  }
};

class StartScreen : public Scene {
 public:
  // Text text0{ Hash("0"), "Stroll of the Dice", color::Cyan };

  TexturedToggleButton mute_button{
      {},
      {Hash("mute"), Hash("mute_disabled")},
      {Hash("mute_hover"), Hash("mute_disabled_hover")},
      {Hash("mute_hover"), Hash("mute_hover")}};

  Button play{{}, Hash("play"), Hash("play"), Hash("play")};
  Color play_text_color{color::White};

  StartScreen() {
    // game.window.SetLogicalSize({ 1689, 1001 });

    game.texture.Load(Hash("play"), "resources/ui/play.png");
    game.texture.Load(Hash("start_background"),
                  "resources/ui/start_background.png");

    mute_button.SetOnActivate([&]() {
      game.sound.Get("click").Play(1, 0);
      game.music.Toggle();
      if (game.music.GetVolume() == 0) {
        game.sound.HaltChannel(0);
      } else {
        game.sound.ResumeChannel(0);
        game.sound.Get(Hash("aqualife_theme")).Play(0, -1);
      }
    });
    V2_int window_size{game.window.GetSize()};
    if (game.music.GetVolume() == 0)
      mute_button.SetToggleState(true);
    else
      mute_button.SetToggleState(false);

    V2_float mute_offset = {12, 12};
    V2_float mute_size{62, 62};
    mute_button.SetRectangle(
        Rect{window_size - mute_size - mute_offset, mute_size});
  }
  void Update() final {
    V2_int window_size{game.window.GetSize()};
    V2_int texture_size = game.texture.Get(Hash("start_background")).GetSize();
    Rect bg{(window_size - texture_size) / 2, texture_size};
    // bg.DrawSolid(color::Blue);
    game.texture.Get(Hash("start_background")).Draw(bg);

    V2_int play_texture_size = play.GetCurrentTexture().GetSize();

    int offset_y = 230;

    play.SetRectangle(
        {window_size / 2 - play_texture_size / 2 + V2_int{0, offset_y},
         play_texture_size});

    V2_int play_text_size{play_texture_size / 2};
    V2_int play_text_pos = window_size / 2 - play_text_size / 2;

    play_text_pos.y += offset_y;

    play.SetOnActivate([&]() {
      game.sound.Get("click").Play(1, 0);
      game.scene.Load<LevelScene>(Hash("level_select"));
      game.scene.Unload(Hash("start_menu"));
      game.scene.SetActive(Hash("level_select"));
    });
    play.SetOnHover([&]() { play_text_color = color::Black; },
                    [&]() { play_text_color = color::White; });

    play.Draw();

    mute_button.Draw();

    // Text t3{ Hash("2"), "Tower Offense", color::DarkGreen };
    // t3.Draw({ play_text_pos - V2_int{ 250, 160 }, { play_text_size.x + 500,
    // play_text_size.y } });

    Text t{Hash("default_font"), "Play", play_text_color};
    t.Draw({play_text_pos, play_text_size});
  }
};

class PixelJam2024 : public Scene {
 public:
  PixelJam2024() {
    // Setup window configuration.
    game.window.SetTitle("Aqualife");
    game.window.SetSize({1280, 720}, true);
    game.window.SetResolution({1280, 720});
    game.window.SetColor(color::Black);
    game.window.SetResizeable(true);
    game.window.Maximize();

    game.music.Load(Hash("ocean_loop"), "resources/music/ocean_loop.mp3");
    game.sound.Load(Hash("aqualife_theme"), "resources/music/aqualife_theme.mp3");
    game.sound.Load("click", "resources/sound/bubble.wav");
    font::Load(Hash("04B_30"), path{"resources/font/04B_30.ttf"}, 32);
    font::Load(Hash("default_font"), path{"resources/font/retro_gaming.ttf"},
               32);

    game.texture.Load(Hash("mute"), "resources/ui/mute.png");
    game.texture.Load(Hash("mute_hover"), "resources/ui/mute_hover.png");
    game.texture.Load(Hash("mute_disabled"), "resources/ui/mute_disabled.png");
    game.texture.Load(Hash("mute_disabled_hover"),
                  "resources/ui/mute_disabled_hover.png");

    if (!game.music.IsPlaying()) {
      game.sound.Get(Hash("aqualife_theme")).Play(0, -1);
      game.music.Get(Hash("ocean_loop")).Play(-1);
    }

    game.scene.Load<StartScreen>(Hash("start_menu"));
    game.scene.SetActive(Hash("start_menu"));
    // game.scene.Load<GameScene>(Hash("game"), IndicatorCondition{ 0.0f, 1.0f,
    // 0.7f, 0.7f, 0.7f }, 0); game.scene.SetActive(Hash("game"));
  }
};

void GameScene::Exit() {
  game.scene.Load<LevelScene>(Hash("level_select"));
  game.scene.Unload(Hash("game"));
  game.scene.Unload(Hash("choices"));
  game.scene.SetActive(Hash("level_select"));
}

void LevelScene::Exit() {
  game.scene.Load<StartScreen>(Hash("start_menu"));
  game.scene.Unload(Hash("level_select"));
  game.scene.SetActive(Hash("start_menu"));
}

void ChoiceScreen::StartChoosing(int choice) {
  if (choice == 8) {
    if (choice_count > 0) {
      choice_count--;
      for (auto& c : choices) {
        c.RecheckState();
        c.SetInteractable(true);
        c.SetVisibility(true);
      }
    }
    if (choice_count == 0) {
      game.scene.Get<GameScene>(Hash("game"))->RanOutOfChoices();
      game.scene.RemoveActive(Hash("choices"));
    }
  } else {
    for (auto& c : choices) {
      c.SetInteractable(false);
      c.SetVisibility(false);
    }
    game.scene.Get<GameScene>(Hash("game"))->Choosing(choice);
    game.scene.RemoveActive(Hash("choices"));
  }
}
*/

/*
int main() {
  game.Start<PixelJam2024>();
  return 0;
}
*/
