#include <cassert>

#include "protegon/protegon.h"

using namespace ptgn;

class StartScreen;
class GameScene;

struct WallComponent {};

struct StartComponent {};

struct EndComponent {};

struct DrawComponent {};

struct EnemyComponent {};

struct StaticComponent {};

struct ColliderComponent {};

struct TurretComponent {};

struct DamageComponent {
	DamageComponent(int damage) : damage{ damage } {}

	int damage{ 10 };
};

struct BulletComponent {};

struct ShooterComponent {};

struct PulserComponent {};

struct FadeComponent {
	FadeComponent(milliseconds time) : time{ time } {}

	bool IsFaded() const {
		return countdown.IsRunning() && countdown.Elapsed<milliseconds>() >= time;
	}

	bool IsFading() const {
		return countdown.IsRunning();
	}

	float GetFraction() const {
		return 1.0f - countdown.ElapsedPercentage(time);
	}

	milliseconds time{};
	Timer countdown;
};

struct RingComponent {
	RingComponent(int thickness) : thickness{ thickness } {}

	bool HasPassed(const ecs::Entity& entity) const {
		for (const auto& passed_entity : passed_entities) {
			if (entity == passed_entity) {
				return true;
			}
		}
		return false;
	}

	int thickness{ 0 };
	std::vector<ecs::Entity> passed_entities;
};

struct LaserComponent {
	LaserComponent(milliseconds damage_delay) : damage_delay{ damage_delay } {}

	milliseconds damage_delay{};

	bool CanDamage() const {
		return !cooldown.IsRunning() || cooldown.Elapsed<milliseconds>() >= damage_delay;
	}

	Timer cooldown;
};

struct ReloadComponent {
	ReloadComponent(milliseconds delay) : delay{ delay } {}

	milliseconds delay{};

	bool CanShoot() const {
		return !timer.IsRunning() || timer.Elapsed<milliseconds>() >= delay;
	}

	Timer timer;
};

struct RangeComponent {
	RangeComponent(float range) : range{ range } {}

	float range{ 0.0f };
};

struct TargetComponent {
	TargetComponent(const ecs::Entity& target, milliseconds begin) :
		target{ target }, begin{ begin } {}

	ecs::Entity target;
	milliseconds begin{};
	Timer timer;
};

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

struct VelocityComponent {
	VelocityComponent(float maximum, float initial = 0.0f) :
		maximum{ maximum }, velocity{ initial } {}

	float maximum{ 0.0f };
	float velocity{ 0.0f };
};

struct DirectionComponent {
	enum Direction {
		DOWN  = 0,
		RIGHT = 2,
		UP	  = 4,
		Left  = 6,
	};

	DirectionComponent(Direction current = Direction::DOWN) : current{ current } {}

	void RecalculateCurrentDirection(const V2_int& normalized_direction) {
		if (normalized_direction != previous_direction) {
			if (normalized_direction.x < 0) {
				current = Left;
			} else if (normalized_direction.x > 0) {
				current = RIGHT;
			} else if (normalized_direction.y < 0) {
				current = UP;
			} else { // Default direction.
				current = DOWN;
			}
		}
		previous_direction = normalized_direction;
	}

	Direction current;

private:
	V2_int previous_direction;
};

struct Velocity2DComponent {
	Velocity2DComponent(const V2_float& initial_direction, float magnitude) :
		direction{ initial_direction }, magnitude{ magnitude } {}

	float magnitude{ 0.0f };
	V2_float direction;
};

struct WaypointComponent {
	float current{ 0.0f };
};

struct HealthComponent {
	HealthComponent(int start_health) : current{ start_health }, original{ current } {}

	int current{ 0 };

	// Returns true if the health was decreased by the given amount.
	bool Decrease(int amount) {
		int potential_new = current - amount;
		if (potential_new < 0) {
			current = 0;
			return true;
		}
		if (potential_new >= 0 && potential_new <= original) {
			current = potential_new;
			return true;
		}
		return false;
	}

	int GetOriginal() const {
		return original;
	}

	bool IsDead() const {
		return current <= 0;
	}

private:
	int original{ 0 };
};

struct LifetimeComponent {
	LifetimeComponent(milliseconds lifetime) : lifetime{ lifetime } {}

	bool IsDead() const {
		return countdown.Elapsed<milliseconds>() >= lifetime;
	}

	milliseconds lifetime{};
	Timer countdown;
};

enum class Enemy {
	REGULAR = 0,
	WIZARD	= 1,
	ELF		= 2,
	FAIRY	= 3
};

struct ClosestInfo {
	ecs::Entity entity{ ecs::null };
	float distance2{ INFINITY };
	V2_float dir;
};

template <typename T>
ClosestInfo GetClosestInfo(ecs::Manager& manager, const V2_float& position, float range) {
	float closest_dist2{ INFINITY };
	float range2{ range * range };
	ecs::Entity closest_target{ ecs::null };
	V2_float closest_dir;
	manager.EntitiesWith<Rect, T>()([&](ecs::Entity target, Rect& target_r,
													T& e) {
		V2_float dir = target_r.Center() - position;
		float dist2	 = dir.MagnitudeSquared();
		if (dist2 < closest_dist2 && dist2 <= range2) {
			closest_dir	   = dir;
			closest_dist2  = dist2;
			closest_target = target;
		}
	});
	return ClosestInfo{ closest_target, closest_dist2, closest_dir };
}

class GameScene : public Scene {
public:
	Surface test_map{ "resources/maps/test_map.png" };
	V2_int grid_size{ 30, 15 };
	V2_int tile_size{ 32, 32 };
	V2_int map_size{ grid_size * tile_size };
	AStarGrid node_grid{ grid_size };
	ecs::Manager manager;
	ecs::Entity start;
	ecs::Entity end;
	std::deque<V2_int> waypoints;
	// damage, health, speed
	std::array<std::tuple<std::string, int, int, float>, 4> values{
		std::tuple<std::string, int, int, float>{ "Normie", 10, 150, 3.0f },
		std::tuple<std::string, int, int, float>{ "Wizard", 20, 120, 3.5f },
		std::tuple<std::string, int, int, float>{ "Elf", 40, 80, 4.5f },
		std::tuple<std::string, int, int, float>{ "Fairy", 60, 40, 5.0f }
	};
	json j;
	std::size_t current_level{ 0 };
	std::size_t levels{ 0 };
	std::size_t current_wave{ 0 };
	std::size_t current_max_waves{ 0 };
	bool music_muted{ false };
	int money{ 0 };

	Text sell_hint{ "Click unit to refund", color::Black, "2" };
	Text buy_hint{ "Press 'b' between waves to buy units", color::Black, "2" };
	Text info_hint{ "Press 'i' to see instructions", color::Black, "2" };

	int max_queue_size{ 8 };
	std::deque<Enemy> enemy_queue;
	std::array<int, 4> prices{ 50, 100, 150, 200 };
	milliseconds enemy_release_delay{ 500 };
	Timer enemy_release_timer;

	ecs::Entity CreateWall(const Rect& rect, const V2_int& coordinate, int key) {
		auto entity = manager.CreateEntity();
		entity.Add<WallComponent>();
		entity.Add<StaticComponent>();
		entity.Add<DrawComponent>();
		entity.Add<TextureComponent>(key);
		entity.Add<TileComponent>(coordinate);
		entity.Add<Rect>(rect);
		manager.Refresh();
		return entity;
	}

	ecs::Entity CreateStart(const Rect& rect, const V2_int& coordinate) {
		auto entity = manager.CreateEntity();
		entity.Add<StartComponent>();
		entity.Add<StaticComponent>();
		entity.Add<DrawComponent>();
		entity.Add<TextureComponent>(1002);
		entity.Add<TileComponent>(coordinate);
		entity.Add<Rect>(rect);
		manager.Refresh();
		return entity;
	}

	ecs::Entity CreateEnd(const Rect& rect, const V2_int& coordinate) {
		auto entity = manager.CreateEntity();
		entity.Add<EndComponent>();
		entity.Add<StaticComponent>();
		entity.Add<DrawComponent>();
		entity.Add<TextureComponent>(1003);
		entity.Add<TileComponent>(coordinate);
		entity.Add<Rect>(rect);
		entity.Add<HealthComponent>(100);
		manager.Refresh();
		return entity;
	}

	ecs::Entity CreateEnemy(const Rect& rect, const V2_int& coordinate, Enemy index) {
		auto entity = manager.CreateEntity();

		int ei							   = (int)index;
		auto [name, damage, health, speed] = values[ei];
		entity.Add<DrawComponent>();
		entity.Add<ColliderComponent>();
		entity.Add<EnemyComponent>();
		entity.Add<WaypointComponent>();
		entity.Add<DirectionComponent>();
		entity.Add<DamageComponent>(damage);
		entity.Add<TextureComponent>(2000, static_cast<int>(index));
		entity.Add<TileComponent>(coordinate);
		entity.Add<Rect>(rect);
		entity.Add<HealthComponent>(health);
		entity.Add<VelocityComponent>(10.0f, speed);
		manager.Refresh();
		return entity;
	}

	ecs::Entity CreateShooterTurret(const Rect& rect, const V2_int& coordinate) {
		auto entity = manager.CreateEntity();
		entity.Add<DrawComponent>();
		entity.Add<TurretComponent>();
		entity.Add<StaticComponent>();
		entity.Add<ShooterComponent>();
		entity.Add<ClosestInfo>();
		entity.Add<TextureComponent>(int(j.at("turrets").at("shooter").at("texture_key")));
		entity.Add<TileComponent>(coordinate);
		entity.Add<Rect>(rect);
		entity.Add<RangeComponent>(300.0f);
		entity.Add<ReloadComponent>(milliseconds{ 300 });
		manager.Refresh();
		return entity;
	}

	ecs::Entity CreateBullet(
		const V2_float& start_position, const V2_float& normalized_direction,
		ecs::Entity target = ecs::null
	) {
		auto entity = manager.CreateEntity();
		// entity.Add<TextureComponent>(int(j.at("turrets").at("laser").at("texture_key")));
		entity.Add<DrawComponent>();
		entity.Add<BulletComponent>();
		entity.Add<ColliderComponent>();
		entity.Add<Circle>(Circle{ start_position, 5.0f });
		entity.Add<Color>(color::Black);
		entity.Add<TargetComponent>(target, milliseconds{ 3000 });
		entity.Add<Velocity2DComponent>(normalized_direction, 1000.0f);
		entity.Add<LifetimeComponent>(milliseconds{ 6000 }).countdown.Start();
		manager.Refresh();
		return entity;
	}

	ecs::Entity CreateLaserTurret(const Rect& rect, const V2_int& coordinate) {
		auto entity = manager.CreateEntity();
		entity.Add<DrawComponent>();
		entity.Add<TurretComponent>();
		entity.Add<LaserComponent>(milliseconds{ 50 });
		entity.Add<StaticComponent>();
		entity.Add<ClosestInfo>();
		entity.Add<TextureComponent>(int(j.at("turrets").at("laser").at("texture_key")));
		entity.Add<TileComponent>(coordinate);
		entity.Add<Rect>(rect);
		entity.Add<RangeComponent>(300.0f);
		manager.Refresh();
		return entity;
	}

	ecs::Entity CreatePulserTurret(const Rect& rect, const V2_int& coordinate) {
		auto entity = manager.CreateEntity();
		entity.Add<DrawComponent>();
		entity.Add<TurretComponent>();
		entity.Add<StaticComponent>();
		entity.Add<PulserComponent>();
		entity.Add<ClosestInfo>();
		entity.Add<TextureComponent>(int(j.at("turrets").at("pulser").at("texture_key")));
		entity.Add<TileComponent>(coordinate);
		entity.Add<Rect>(rect);
		entity.Add<RangeComponent>(300.0f);
		entity.Add<ReloadComponent>(milliseconds{ 3000 });
		manager.Refresh();
		return entity;
	}

	ecs::Entity CreateRing(const V2_float& start_position) {
		auto entity = manager.CreateEntity();
		entity.Add<DrawComponent>();
		entity.Add<ColliderComponent>();
		entity.Add<RingComponent>(3);
		entity.Add<FadeComponent>(milliseconds{ 1000 });
		entity.Add<Circle>(Circle{ start_position, 2.0f });
		entity.Add<Color>(color::LightPink);
		entity.Add<VelocityComponent>(100.0f, 100.0f);
		entity.Add<LifetimeComponent>(milliseconds{ 1000 }).countdown.Start();
		manager.Refresh();
		return entity;
	}

	void Reset() {
		releasing_enemies = false;
		release_done	  = false;
		manager.Reset();
		waypoints.clear();
		enemy_queue.clear();
		node_grid.Reset();
		enemy_release_timer.Stop();
		// Setup node grid for the map.
		test_map.ForEachPixel([&](const V2_int& coordinate, const Color& color) {
			V2_int position = coordinate * tile_size;
			Rect rect{ position, tile_size };
			if (color == color::Magenta) {
				CreateWall(rect, coordinate, 501);
				node_grid.SetObstacle(coordinate, true);
			} else if (color == color::LightPink) {
				CreateWall(rect, coordinate, 500);
				node_grid.SetObstacle(coordinate, true);
			} else if (color == color::Blue) {
				start = CreateStart(rect, coordinate);
			} else if (color == color::Lime) {
				end = CreateEnd(rect, coordinate);
			}
		});

		assert(start.Has<TileComponent>());
		assert(end.Has<TileComponent>());
		// Calculate waypoints for the current map.
		waypoints = node_grid.FindWaypoints(
			start.Get<TileComponent>().coordinate, end.Get<TileComponent>().coordinate
		);

		DestroyTurrets();
		CreateTurrets();
		money = j.at("levels").at(current_level).at("waves").at(current_wave).at("money");
	}

	void DestroyTurrets() {
		manager.EntitiesWith<TurretComponent>().ForEach([](auto e) { e.Destroy(); });
		manager.Refresh();
	}

	void CreateTurrets() {
		auto& enemies = j.at("levels").at(current_level).at("waves").at(current_wave).at("enemies");
		for (auto& enemy : enemies) {
			V2_int coordinate{ enemy.at("position").at(0), enemy.at("position").at(1) };
			Rect rect{ coordinate * tile_size, tile_size };
			if (enemy.at("type") == "shooter") {
				CreateShooterTurret(rect, coordinate);
			} else if (enemy.at("type") == "laser") {
				CreateLaserTurret(rect, coordinate);
			} else if (enemy.at("type") == "pulser") {
				CreatePulserTurret(rect, coordinate);
			}
		}
	}

	Button mute_button_b{ Rect{ map_size - tile_size, tile_size } };
	Button start_wave_button{ Rect{ V2_float{ 0, map_size.y - 50 }, { 100, 50 } } };

	GameScene() {
		game.music.Unmute();
		game.music.Load("in_game", "resources/music/in_game.wav");
		game.music.Get("in_game").Play(-1);

		// Load json data.
		std::ifstream f{ "resources/data/level_data.json" };
		if (f.fail()) {
			f = std::ifstream{ GetAbsolutePath("resources/data/level_data.json") };
		}
		assert(!f.fail() && "Failed to load json file");
		j = json::parse(f);

		levels = j.at("levels").size();
		// Create turrets for the current wave.
		current_max_waves = j.at("levels").at(current_level).at("waves").size();

		// Load textures.
		game.texture.Load(500, "resources/tile/wall.png");
		game.texture.Load(501, "resources/tile/top_wall.png");
		game.texture.Load(502, "resources/tile/path.png");
		game.texture.Load(1002, "resources/tile/start.png");
		game.texture.Load(1003, "resources/tile/end.png");
		game.texture.Load(1004, "resources/tile/enemy.png");
		game.texture.Load(
			int(j.at("turrets").at("shooter").at("texture_key")), "resources/turret/shooter.png"
		);
		game.texture.Load(
			int(j.at("turrets").at("laser").at("texture_key")), "resources/turret/laser.png"
		);
		game.texture.Load(
			int(j.at("turrets").at("pulser").at("texture_key")), "resources/turret/pulser.png"
		);
		game.texture.Load(2000, "resources/enemy/enemy.png");
		game.texture.Load(3000, "resources/ui/queue_frame.png");
		game.texture.Load(3001, "resources/ui/arrow.png");
		game.texture.Load(1, "resources/background/level.png");

		game.sound.Load("enemy_death_sound", "resources/sound/death.wav");
		game.sound.Load("shoot_bullet", "resources/sound/bullet.wav");
		game.sound.Load("pulse_attack", "resources/sound/pulse_attack.wav");
		game.sound.Load("laser_buzz", "resources/sound/laser_buzz.wav");

		mute_button_b.Set<ButtonProperty::Texture>(Texture{ "resources/ui/mute.png" });
		mute_button_b.Set<ButtonProperty::Texture>(Texture{ "resources/ui/mute_hover.png" }, ButtonState::Hover);
		mute_button_b.Set<ButtonProperty::Texture>(Texture{ "resources/ui/mute_grey.png" }, ButtonState::Default, true);
		mute_button_b.Set<ButtonProperty::Texture>(Texture{ "resources/ui/mute_grey_hover.png" }, ButtonState::Hover, true);
		mute_button_b.Set<ButtonProperty::Toggleable>(true);
		mute_button_b.Set<ButtonProperty::OnActivate>([]() {
			game.sound.Get("click").Play(3, 0);
			game.music.Toggle();
		});

		start_wave_button.Set<ButtonProperty::Text>(Text{ "Start", color::Gold, "2" });
		start_wave_button.Set<ButtonProperty::BackgroundColor>(color::Red);
		start_wave_button.Set<ButtonProperty::BackgroundColor>(color::Black, ButtonState::Hover);
		start_wave_button.Set<ButtonProperty::BackgroundColor>(color::Black, ButtonState::Pressed);
		start_wave_button.Set<ButtonProperty::OnActivate>([&]() {
			if (!releasing_enemies && !release_done && enemy_queue.size() > 0) {
				releasing_enemies = true;
				game.sound.Get("click").Play(3, 0);
			}
		});

		Reset();
	}

	void Init() override {
		game.draw.SetClearColor(color::Black);
	}

	bool paused			   = false;
	bool releasing_enemies = false;
	bool release_done	   = false;

	void Update() final {
		if (game.scene.GetActive().back().get() == this) {
			paused = false;
		}

		if (!paused) {
			game.draw.Texture(game.texture.Get(1));

			// Get mouse position on screen and tile grid.
			V2_int mouse_pos = game.input.GetMousePosition();
			V2_int mouse_tile = V2_int{ V2_float{ mouse_pos } / V2_float{ tile_size } };
			Rect mouse_box{ mouse_tile * tile_size, tile_size };

			/*
			bool new_wave = false;

			if (game.input.KeyDown(Key::Q)) { // Decrement wave.
				current_wave = ModFloor(current_wave - 1, current_max_waves);
				new_wave = true;
			} else if (game.input.KeyDown(Key::E)) { // Increment wave.
				current_wave = ModFloor(current_wave + 1, current_max_waves);
				new_wave = true;
			}

			if (new_wave) { // Remake turrets upon wave change.
				Reset();
			}
			*/

			// Determine nearest enemy to a turret.
			manager.EntitiesWith<RangeComponent, Rect, TurretComponent, ClosestInfo>()(
				[&](ecs::Entity entity, RangeComponent& s, Rect& r, TurretComponent& t,
					ClosestInfo& closest) {
					closest = GetClosestInfo<EnemyComponent>(manager, r.Center(), s.range);
				}
			);

			// Fire bullet from shooter turret if there is an enemy nearby.
			manager.EntitiesWith<
				RangeComponent, Rect, TurretComponent, ClosestInfo, ReloadComponent,
				ShooterComponent>()([&](ecs::Entity entity, RangeComponent& s, Rect& r,
										TurretComponent& t, ClosestInfo& closest,
										ReloadComponent& reload, ShooterComponent& shooter) {
				if (closest.entity.IsAlive()) {
					if (reload.CanShoot()) {
						reload.timer.Start();
						CreateBullet(r.Center(), closest.dir.Normalized(), closest.entity);
						game.sound.Get("shoot_bullet").Play(1, 0);
					}
				}
			});

			// Draw laser turret beam toward closest enemy.
			manager.EntitiesWith<
				RangeComponent, Rect, TurretComponent, ClosestInfo, LaserComponent>()(
				[&](ecs::Entity entity, RangeComponent& s, Rect& r, TurretComponent& t,
					ClosestInfo& closest, LaserComponent& laser) {
					if (closest.entity.IsAlive()) {
						if (laser.CanDamage()) {
							laser.cooldown.Start();
							if (closest.entity.Has<HealthComponent>()) {
								auto& h = closest.entity.Get<HealthComponent>();
								h.Decrease(1);
							}
						}
					}
				}
			);

			// Expand ring from pulser if there is an enemy nearby.
			manager.EntitiesWith<
				RangeComponent, Rect, TurretComponent, ClosestInfo, ReloadComponent,
				PulserComponent>()([&](ecs::Entity entity, RangeComponent& s, Rect& r,
									   TurretComponent& t, ClosestInfo& closest,
									   ReloadComponent& reload, PulserComponent& pulser) {
				if (closest.entity.IsAlive()) {
					if (reload.CanShoot()) {
						reload.timer.Start();
						CreateRing(r.Center());
						game.sound.Get("pulse_attack").Play(2, 0);
					}
				}
			});

			// Add enemies to queue using number keys when enemies are not being released.
			// TODO: Make these push from buy menu buttons.

			V2_float queue_frame_size{ 28, 32 };
			const Rect queue_frame{ { map_size.x / 2 -
													  queue_frame_size.x * max_queue_size / 2,
												  map_size.y - queue_frame_size.y },
												queue_frame_size };

			start_wave_button.Draw();

			if (releasing_enemies) {
				// Start the queue release timer.
				if (!enemy_release_timer.IsRunning()) {
					enemy_release_timer.Start();
				}
				// Every time the delay has been passed, send one enemy from the queue.
				if (enemy_release_timer.Elapsed<milliseconds>() >= enemy_release_delay) {
					if (enemy_queue.size() > 0) {
						Enemy queue_element = enemy_queue.front();
						switch (queue_element) {
							// TODO: Will eventually break these up once enemies get custom
							// mechanics.
							case Enemy::REGULAR:
							case Enemy::WIZARD:
							case Enemy::ELF:
							case Enemy::FAIRY:	 {
								CreateEnemy(
									start.Get<Rect>(),
									start.Get<TileComponent>().coordinate, queue_element
								);
								break;
							}
						}
						enemy_queue.pop_front();
					} else {
						// Once the queue is empty, stop the timer and stop releasing enemies.
						if (enemy_release_timer.IsRunning()) {
							enemy_release_timer.Stop();
						}
						release_done	  = true;
						releasing_enemies = false;
					}
				}
			}

			// Increase enemy velocity on right click.
			/*if (game.input.MouseDown(Mouse::Left)) {
				manager.EntitiesWith<VelocityComponent, EnemyComponent>([](
					auto e, VelocityComponent& vel, EnemyComponent& enemy) {
					vel.velocity = std::min(vel.maximum, vel.velocity + 1.0f);
				});
			}*/

			// Decrease enemy velocity on right click.
			/*if (game.input.MouseDown(Mouse::RIGHT)) {
				manager.EntitiesWith<VelocityComponent, EnemyComponent>([](
					auto e, VelocityComponent& vel, EnemyComponent& enemy) {
					vel.velocity = std::max(0.0f, vel.velocity - 1.0f);
				});
			}*/

			// Collide bullets with enemies, decrease health of enemies, and destroy bullets.
			manager.EntitiesWith<BulletComponent, Circle, ColliderComponent>()(
				[&](auto e, BulletComponent& d, Circle& c, ColliderComponent& collider) {
					manager.EntitiesWith<Rect, ColliderComponent, EnemyComponent>()(
						[&](auto e2, Rect& r2, ColliderComponent& c2,
							EnemyComponent& enemy2) {
							if (e.IsAlive() && c.Overlaps(r2)) {
								if (e2.template Has<HealthComponent>()) {
									HealthComponent& h = e2.template Get<HealthComponent>();
									h.Decrease(2);
								}
								e.Destroy();
							}
						}
					);
				}
			);

			// Collide rings with enemies, decrease health of enemies once.
			manager.EntitiesWith<RingComponent, Circle, ColliderComponent>()(
				[&](auto e, RingComponent& r, Circle& c, ColliderComponent& collider) {
					manager.EntitiesWith<Rect, ColliderComponent, EnemyComponent>()(
						[&](auto e2, Rect& r2, ColliderComponent& c2,
							EnemyComponent& enemy2) {
							if (e.IsAlive() && c.Overlaps(r2) &&
								!r.HasPassed(e2)) {
								if (e2.template Has<HealthComponent>()) {
									HealthComponent& h = e2.template Get<HealthComponent>();
									h.Decrease(10);
								}
								r.passed_entities.push_back(e2);
							}
						}
					);
				}
			);

			for (auto coordinate : waypoints) {
				V2_int pos = coordinate * tile_size;
				Rect rect{ pos, tile_size };
				game.draw.Texture(game.texture.Get(502), rect);
			}

			// Draw shooter tower range.
			manager.EntitiesWith<RangeComponent, Rect, TurretComponent>()(
				[&](ecs::Entity entity, RangeComponent& s, Rect& r,
					TurretComponent& t) {
					Circle circle{ r.Center(), s.range };
					circle.Draw(Color{ 128, 0, 0, 30 });
				}
			);

			float dt = game.dt();

			// Move bullet position forward by their velocity.
			manager.EntitiesWith<Circle, Velocity2DComponent>()([&](auto e, Circle& c,
																		   Velocity2DComponent& v) {
				c.center += v.direction * v.magnitude * dt;
			});

			manager.EntitiesWith<Circle, VelocityComponent, RingComponent>()(
				[&](ecs::Entity entity, Circle& c, VelocityComponent& v, RingComponent& r) {
					c.radius += v.velocity * dt;
				}
			);

			// Move targetted projectile bullets toward targets.
			manager.EntitiesWith<Circle, Velocity2DComponent, TargetComponent>()(
				[](auto e, Circle& c, Velocity2DComponent& v, TargetComponent& t) {
					if (t.target.IsAlive()) {
						V2_float target_position;
						// TODO: Add generalized shape parent with position function.
						if (t.target.Has<Circle>()) {
							target_position = t.target.Get<Circle>().center;
						} else if (t.target.Has<Rect>()) {
							target_position = t.target.Get<Rect>().Center();
							assert((t.target.HasAny<Circle, Rect>()));
							v.direction = (target_position - c.center).Normalized();
						}
					}
				}
			);

			// Draw static rectangular structures with textures.
			manager
				.EntitiesWith<Rect, TextureComponent, DrawComponent, StaticComponent>()(
					[](ecs::Entity e, Rect& rect, TextureComponent& texture,
					   DrawComponent& draw, StaticComponent& s) {
						game.draw.Texture(game.texture.Get(texture.key), rect);
					}
				);

			// Display node grid paths from start to finish.
			node_grid.DisplayWaypoints(waypoints, tile_size, color::Purple);

			bool quit = false;
			// Move enemies along their path.
			manager.EntitiesWith<
				TileComponent, Rect, TextureComponent, VelocityComponent,
				EnemyComponent, WaypointComponent, DirectionComponent, DamageComponent>()(
				[&](ecs::Entity e, TileComponent& tile, Rect& rect,
					TextureComponent& texture, VelocityComponent& vel, EnemyComponent& enemy,
					WaypointComponent& waypoint, DirectionComponent& dir, DamageComponent& dam) {
					bool path_exists = tile.coordinate != end.Get<TileComponent>().coordinate;
					int idx			 = -1;
					if (path_exists) {
						idx = AStarGrid::FindWaypointIndex(waypoints, tile.coordinate);
					}
					path_exists = idx != -1;
					if (path_exists) {
						waypoint.current += dt * vel.velocity;
						assert(idx >= 0);
						assert(idx < waypoints.size());
						assert(idx + 1 < waypoints.size());
						// Keep moving character 1 tile forward on its path
						// until there is no longer enough "speed" for 1 full tile
						// in which case exit the loop and linearly interpolate
						// the position between the "in progress" tiles.
						while (waypoint.current >= 1.0f && idx + 1 < waypoints.size()) {
							tile.coordinate	 += waypoints[idx + 1] - waypoints[idx];
							waypoint.current -= 1.0f;
							idx++;
						}
					}
					if (path_exists && idx + 1 < waypoints.size()) {
						assert(waypoint.current <= 1.0f);
						assert(waypoint.current >= 0.0f);
						assert(idx >= 0);
						assert(idx < waypoints.size());
						assert(idx + 1 < waypoints.size());
						V2_int direction = waypoints[idx + 1] - waypoints[idx];
						// Linearly interpolate between the turret tile coordinate and the next one.
						rect.position = Lerp(
							V2_float{ tile.coordinate * tile_size },
							V2_float{ (tile.coordinate + direction) * tile_size }, waypoint.current
						);
						dir.RecalculateCurrentDirection(direction);
						game.draw.Texture(game.texture.Get(texture.key), rect, { V2_float{
														  static_cast<float>(dir.current),
														  static_cast<float>(texture.index) } *
														  tile_size,
													  tile_size });
					} else {
						// Destroy enemy when it reaches the end or when no path remains for it.
						e.Destroy();
						// Decrease health of end tower by 1.
						assert(end.Has<HealthComponent>());
						HealthComponent& h = end.Get<HealthComponent>();
						// TODO: Set this to damage of unit.
						h.Decrease(dam.damage);
						if (h.IsDead()) {
							current_wave++;
							if (current_wave >= current_max_waves) {
								game.scene.Unload("game");
								game.scene.AddActive("game_win");
							} else {
								Reset();
							}
							quit = true;
						}
					}
				}
			);
			if (quit) {
				return;
			}
			// Draw bullet circles.
			manager.EntitiesWith<DrawComponent, Circle, Color, BulletComponent>()(
				[](auto e, DrawComponent& d, Circle& c, Color& color, BulletComponent& b) {
					c.Draw(color);
				}
			);

			// Draw ring circles.
			manager.EntitiesWith<DrawComponent, Circle, Color, RingComponent>()(
				[](ecs::Entity e, DrawComponent& d, Circle& c, const Color& col,
				   RingComponent& r) {
					Color color = col;
					if (e.Has<FadeComponent>()) {
						FadeComponent& f = e.Get<FadeComponent>();
						if (f.IsFading()) {
							color.a = static_cast<std::uint8_t>(col.a * f.GetFraction());
						}
					}
					c.Draw(
						Color{ color.r, color.g, color.b,
								  static_cast<std::uint8_t>(0.2f * color.a) }, -1.0f
					); // color, r.thickness);
					c.Draw(color, (float)r.thickness);
				}
			);

			// Draw laser turret laser toward closest enemy.
			manager.EntitiesWith<
				RangeComponent, Rect, TurretComponent, ClosestInfo,
				LaserComponent>()([&](ecs::Entity entity, RangeComponent& s, Rect& r,
									  TurretComponent& t, ClosestInfo& closest,
									  LaserComponent& laser) {
				if (closest.entity.IsAlive()) {
					assert(closest.entity.Has<Rect>());
					Line beam{ r.Center(), closest.entity.Get<Rect>().Center() };
					beam.Draw(color::Red, 3.0f);
				}
			});

			// Draw healthbars
			manager.EntitiesWith<Rect, HealthComponent, EnemyComponent>()(
				[&](auto e, const Rect& p, const HealthComponent& h,
					const EnemyComponent& ene) {
					assert(h.current >= 0);
					assert(h.current <= h.GetOriginal());
					float fraction{ 0.0f };
					if (h.GetOriginal() > 0) {
						fraction = (float)h.current / h.GetOriginal();
					}
					Rect full_bar{ p.position, V2_float{ 20, 2.0f } };
					full_bar.position += V2_int{ 6, 3 };
					game.draw.Rect(full_bar, color::Red, -1.0f);
					Rect remaining_bar{ full_bar };
					if (fraction >= 0.1f) { // Stop drawing green bar after health reaches below 1%.
						remaining_bar.size.x = full_bar.size.x * fraction;
						game.draw.Rect(remaining_bar, color::Green, -1.0f);
					}
				}
			);

			V2_float full_end_bar_size{ 300, 30 };
			Rect full_end_bar{ { game.window.GetCenter().x - full_end_bar_size.x / 2, 0 },
										   full_end_bar_size };

			// Draw "end block" health bar
			manager.EntitiesWith<Rect, HealthComponent, EndComponent>()(
				[&](auto e, const Rect& p, const HealthComponent& h,
					const EndComponent& end_comp) {
					assert(h.current >= 0);
					assert(h.current <= h.GetOriginal());
					float fraction{ 0.0f };
					if (h.GetOriginal() > 0) {
						fraction = (float)h.current / h.GetOriginal();
					}
					game.draw.Rect(full_end_bar, color::Red, -1.0f);
					Rect remaining_bar{ full_end_bar };
					if (fraction >= 0.1f) { // Stop drawing green bar after health reaches below 1%.
						remaining_bar.size.x = full_end_bar.size.x * fraction;
						game.draw.Rect(remaining_bar, color::Green, -1.0f);
					}
				}
			);

			// Draw border around "end block" health bar.
			Rect health_bar_border = full_end_bar;
			health_bar_border.position += V2_int{ -4, -4 };
			health_bar_border.size += V2_int{ 8, 8 };
			game.draw.Rect(health_bar_border, color::DarkBrown, 6);
			game.draw.Rect(health_bar_border, color::Black, 3);

			// Draw border around queue frame.
			Rect queue_frame_border = queue_frame;
			queue_frame_border.position += V2_int{ -4, -4 };
			queue_frame_border.size += V2_int{ queue_frame.size.x * (max_queue_size - 1) + 8, 8 };
			game.draw.Rect(queue_frame_border, color::DarkBrown, 6.0f);
			game.draw.Rect(queue_frame_border, color::Black, 3.0f);

			/*Rect sell_hint_box{ { queue_frame_border.pos.x + queue_frame_border.size.x
			+ 10, queue_frame_border.pos.y + 3 }, { 160, queue_frame_border.size.y - 6  } };
			sell_hint.Draw(sell_hint_box);*/

			Rect buy_hint_box{ { queue_frame_border.position.x + queue_frame_border.size.x +
												 10,
											 queue_frame_border.position.y + 3 },
										   { 280, queue_frame_border.size.y - 6 } };
			buy_hint.Draw(buy_hint_box);

			V2_float info_hint_box_size{ 230, queue_frame_border.size.y - 6 };
			Rect info_hint_box{ { queue_frame_border.position.x - info_hint_box_size.x - 10,
											  queue_frame_border.position.y + 3 },
											info_hint_box_size };
			info_hint.Draw(info_hint_box);

			// Draw queue.
			for (int i = 0; i < max_queue_size; i++) {
				Rect frame = queue_frame;
				frame.position += V2_int{ queue_frame.size.x * i, 0 };
				game.draw.Texture(game.texture.Get(3000), frame);
			}

			// Draw hover.
			/*for (int i = 0; i < max_queue_size; i++) {
				Rect frame = queue_frame.Offset({ queue_frame.size.x * i, 0 });
				if (game.collision.overlap.PointRectangle(mouse_pos, frame)) {
					frame.Draw(color::Gold, 3);
					break;
				}
			}*/

			// Draw UI displaying enemies in queue.
			int facing_direction = 7; // characters point to the bottom left.
			for (int i = 0; i < enemy_queue.size(); i++) {
				Enemy type = enemy_queue[i];
				Rect texture_rect{ queue_frame };
				texture_rect.position += V2_int{ queue_frame.size.x * i, 0 };
				game.draw.Texture(
					game.texture.Get(2000), texture_rect,
					{ V2_float{ static_cast<float>(facing_direction),
														static_cast<float>(type) } *
												  tile_size, tile_size }
				);
			}
			// Draw arrow over first enemy in queue.
			if (enemy_queue.size() > 0) {
				V2_float arrow_size{ 15, 21 };
				Rect arrow = queue_frame;
				arrow.Offset({ 0.0f, -arrow_size.y });
				game.draw.Texture(game.texture.Get(3001), arrow);
			}

			// Draw money box.
			std::string money_str = "Money: " + std::to_string(money);
			Text money_text{ money_str.c_str(), color::Gold, "2" };
			V2_int money_text_size{ 150, 30 };
			Rect money_text_box{ { game.window.GetSize().x - money_text_size.x - 5, 0 },
										   { money_text_size.x, money_text_size.y } };
			Rect money_text_frame = money_text_box;
			money_text_frame.position += V2_int{ -10, -4 };
			money_text_frame.size += V2_int{ 20, 8 };
			game.draw.Rect(money_text_frame, color::Black, -1.0f);
			game.draw.Rect(money_text_frame, color::DarkBrown, 6.0f);
			game.draw.Rect(money_text_frame, color::Black, 3.0f);
			money_text.Draw(money_text_box);

			// Draw mouse hover square.
			// if (game.collision.overlap.PointRectangle(mouse_pos, bg) &&
			// node_grid.IsObstacle(mouse_tile))
			//	mouse_box.Draw(color::Gold, 3);

			mute_button_b.Draw();

			// Destroy enemies which run out of lifetime.
			manager.EntitiesWith<LifetimeComponent>()([](ecs::Entity e, LifetimeComponent& l) {
				if (l.IsDead()) {
					if (e.Has<FadeComponent>()) {
						auto& f = e.Get<FadeComponent>();
						if (f.IsFaded()) {
							e.Destroy();
						} else if (!f.IsFading()) {
							f.countdown.Start();
						}
					} else {
						e.Destroy();
					}
				}
			});

			// Destroy enemies which run out of health.
			manager.EntitiesWith<HealthComponent>()([](auto e, HealthComponent& h) {
				if (h.IsDead()) {
					if (e.template Has<EnemyComponent>()) {
						game.sound.Get("enemy_death_sound").Play(4, 0);
					}
					e.Destroy();
				}
			});

			manager.Refresh();

			if (game.input.KeyDown(Key::ESCAPE) && !paused) {
				game.scene.AddActive("menu");
				game.scene.Unload("game");
			}
			if (game.input.KeyDown(Key::I) && !paused) {
				game.scene.AddActive("instructions");
				paused = true;
			}
			if (game.input.KeyDown(Key::B) && !releasing_enemies && !paused && !release_done) {
				game.scene.AddActive("buy_menu");
				paused = true;
			}

			int alive_entities = 0;
			manager.EntitiesWith<EnemyComponent>()([&](auto e, EnemyComponent& en) {
				alive_entities++;
			});

			if (alive_entities == 0 && release_done && !releasing_enemies) {
				if (end.Has<HealthComponent>()) {
					auto& end_health_temp = end.Get<HealthComponent>();
					if (!end_health_temp.IsDead()) {
						Reset();
					}
				}
			}

		} else {
			if (game.input.KeyDown(Key::ESCAPE) || game.input.KeyDown(Key::B) ||
				game.input.KeyDown(Key::I)) {
				game.scene.RemoveActive("instructions");
				game.scene.RemoveActive("buy_menu");
			}
		}
	}
};

class InstructionScreen : public Scene {
public:
	InstructionScreen() {}

	void Update() final {
		V2_int window_size{ game.window.GetSize() };

		game.draw.Texture(game.texture.Get(2));

		auto mouse = game.input.GetMousePosition();
		V2_int s{ 960, 480 };

		V2_int play_size{ 463, 204 };
		V2_int play_pos{ window_size.x / 2 - play_size.x / 2 - 10,
						 window_size.y / 2 - play_size.y / 2 - 18 };

		V2_int play_text_size{ 220, 50 };
		V2_int play_text_pos{ window_size.x / 2 - play_text_size.x / 2,
							  window_size.y / 2 - play_text_size.y / 2 };

		Text t{ "'i' to exit instructions page", color::Black, "2" };
		t.Draw(Rect{ play_text_pos - V2_int{ 250, 160 },
							   { play_text_size.x + 500, play_text_size.y } });

		Text t2{ "'b' between waves to open purchase menu", color::Brown, "2" };
		t2.Draw(Rect{ play_text_pos - V2_int{ 250, 160 - 70 },
								{ play_text_size.x + 500, play_text_size.y } });

		Text t3{ "'Space' to send the units on their way", color::DarkGray, "2" };
		t3.Draw(Rect{ play_text_pos - V2_int{ 250, 160 - 70 - 70 },
								{ play_text_size.x + 500, play_text_size.y } });

		Text t4{ "If units do not kill end goal, wave resets", color::Gold, "2" };
		t4.Draw(Rect{ play_text_pos - V2_int{ 250, 160 - 70 - 70 - 70 },
								{ play_text_size.x + 500, play_text_size.y } });
	}
};

class BuyScreen : public Scene {
public:
	Texture menu{ "resources/ui/menu.png" };
	Texture enemies{ "resources/enemy/enemy.png" };
	Texture buy{ "resources/ui/buy.png" };

	BuyScreen() {
		// TODO: Do this elsewhere.
		rotate.Start();
	}

	V2_int grid_size{ 30, 15 };
	V2_int tile_size{ 32, 32 };
	milliseconds delay{ 200 };
	int directions = 8;
	Timer rotate;
	int direction = 0;
	Text sell_hint{ "Click unit to refund", color::White, "2" };

	void Update() final {
		GameScene& game_scene = *game.scene.Get<GameScene>("game");
		V2_int window_size{ game.window.GetSize() };

		auto mouse_pos = game.input.GetMousePosition();
		Rect bg{ {}, window_size };
		game.draw.Texture(game.texture.Get(2), bg);

		Rect menu_bg{ { 30, 30 }, { window_size.x - 60, window_size.y - 60 } };
		game.draw.Texture(menu, menu_bg);
		// Draw border around queue frame.
		Rect menu_bg_border = menu_bg;
		menu_bg_border.position += V2_int{ -10, -10 };
		menu_bg_border.size += V2_int{ 20, 20 };
		game.draw.Rect(menu_bg_border, color::DarkBrown, 20.0f);
		game.draw.Rect(menu_bg_border, color::Black, 10.0f);

		V2_float unit_frame_size{ window_size.x * 0.160416667f, window_size.y * 0.334375f };

		V2_float first_button_fraction{ 217.0f / 1920.0f, 583.0f / 960.0f };
		V2_float first_button_size_fraction{ 165.0f / 1920.0f, 36.0f / 960.0f };
		V2_float first_button_left{ window_size * first_button_fraction };
		V2_float first_button_size{ window_size * first_button_size_fraction };
		float button_offset{ window_size.x * 274.0f / 1920.0f };
		for (auto i = 0; i < 4; ++i) {
			V2_float pos = { first_button_left.x + (first_button_size.x + button_offset) * i,
							 first_button_left.y };
			Rect first_button{ pos, first_button_size };
			int index = 0;
			if (mouse_pos.Overlaps(first_button)) {
				index = 1;
				// Buy item if player has money and spaces in queue.
				if (game.input.MouseDown(Mouse::Left) && game_scene.prices[i] <= game_scene.money &&
					game_scene.enemy_queue.size() < game_scene.max_queue_size) {
					game.sound.Get("click").Play(3, 0);
					game_scene.enemy_queue.push_back(static_cast<Enemy>(i));
					game_scene.money -= game_scene.prices[i];
				}
			}
			game.draw.Texture(
				buy, first_button,
				{ V2_float{ 0.0f, 32.0f * index }, V2_float{ 64, 32 } }
			);
			std::string price = "Price: " + std::to_string(game_scene.prices[i]);
			Text price_text{ price.c_str(), color::Gold, "2" };
			Rect price_text_rect = first_button;
			price_text_rect.position += V2_int{ 0, -unit_frame_size.y - 48 };
			price_text.Draw(price_text_rect);
		}

		Texture exit{ "resources/ui/exit_menu.png" };
		Texture exit_hover{ "resources/ui/exit_menu_hover.png" };
		const Rect exit_button{ { window_size.x - 60 - 4, 30 + 2 }, tile_size };
		bool hovering_over_exit = mouse_pos.Overlaps(exit_button);
		if (hovering_over_exit) {
			if (game.input.MouseDown(Mouse::Left)) {
				game.sound.Get("click").Play(3, 0);
				game.scene.RemoveActive("instructions");
				game.scene.RemoveActive("buy_menu");
			}
			game.draw.Texture(exit_hover, exit_button);
		} else {
			game.draw.Texture(exit, exit_button);
		}

		V2_float first_unit_top_left{ window_size / 2 - V2_float{ 404, 138 } };
		float offset{ window_size.x * 0.06875f };

		if (rotate.Elapsed<milliseconds>() >= delay) {
			rotate.Start();
			direction = ModFloor(direction + 1, directions);
		}

		for (auto i = 0; i < 4; ++i) {
			V2_float pos = { first_unit_top_left.x + (unit_frame_size.x + offset) * i,
							 first_unit_top_left.y };
			Rect unit{ pos, unit_frame_size };
			Rect source_rect{ V2_float{ (float)direction, (float)i } * tile_size,
										  tile_size };
			game.draw.Texture(enemies, unit, { source_rect.position, source_rect.size });
		}

		std::string money_str = "Money: " + std::to_string(game_scene.money);
		Text money_text{ money_str.c_str(), color::Gold, "2" };
		V2_int money_text_size{ 130, 25 };
		Rect money_text_box{
			{ (float)game.window.GetSize().x / 2.0f - (float)money_text_size.x / 2.0f, 0.0f },
			money_text_size
		};
		Rect money_text_frame = money_text_box;
		money_text_frame.position += V2_int{ -10, -4 };
		money_text_frame.size += V2_int{ 20, 8 };
		game.draw.Rect(money_text_frame, color::Black, -1.0f);
		game.draw.Rect(money_text_frame, color::DarkBrown, 6.0f);
		game.draw.Rect(money_text_frame, color::Black, 3.0f);
		money_text.Draw(money_text_box);

		V2_float queue_frame_size{ 28, 32 };
		const Rect queue_frame{
			{ grid_size.x * tile_size.x / 2 - queue_frame_size.x * game_scene.max_queue_size / 2,
			  grid_size.y * tile_size.y - queue_frame_size.y },
			queue_frame_size
		};

		// Draw queue.
		for (int i = 0; i < game_scene.max_queue_size; i++) {
			Rect frame = queue_frame;
			frame.position += V2_int{ queue_frame.size.x * i, 0 };
			game.draw.Texture(game.texture.Get(3000), frame);
		}

		// Draw hover.
		for (int i = 0; i < game_scene.max_queue_size; i++) {
			Rect frame = queue_frame;
			frame.position += V2_int{ queue_frame.size.x * i, 0 };
			if (mouse_pos.Overlaps(frame)) {
				game.draw.Rect(frame, color::Gold, 3.0f);
				break;
			}
		}

		for (int i = 0; i < game_scene.max_queue_size; i++) {
			Rect frame = queue_frame;
			frame.position += V2_int{ queue_frame.size.x * i, 0 };
			if (mouse_pos.Overlaps(frame) &&
				game.input.MouseDown(Mouse::Left) && i < game_scene.enemy_queue.size()) {
				game.sound.Get("click").Play(3, 0);
				game_scene.money += game_scene.prices[static_cast<int>(game_scene.enemy_queue[i])];
				game_scene.enemy_queue.erase(game_scene.enemy_queue.begin() + i);
				break;
			}
		}

		V2_float first_stat_top_left_frac{ 143.0f / 1920.0f, 643.0f / 960.0f };
		V2_float first_stat_size_frac{ 296 / 1920.0f, 45.0f / 960.0f };
		V2_float first_stat_top_left{ first_stat_top_left_frac * window_size };
		V2_float first_stat_size{ first_stat_size_frac * window_size };
		V2_float stat_offsets_frac{ 149.0f / 1920.0f, 15.0f / 960.0f };
		V2_float stat_offsets{ stat_offsets_frac * window_size };

		int stat_count = 4;
		for (int i = 0; i < game_scene.values.size(); ++i) {
			for (int j = 0; j < stat_count; j++) {
				Color stat_color  = color::Black;
				std::string label = "";
				if (j == 0) { // names
					label	   = "Name: " + std::get<0>(game_scene.values[i]);
					stat_color = color::Gold;
				} else if (j == 1) { // damage
					label	   = "Damage: " + std::to_string(std::get<1>(game_scene.values[i]));
					stat_color = color::Red;
				} else if (j == 2) { // health
					label	   = "Health: " + std::to_string(std::get<2>(game_scene.values[i]));
					stat_color = color::Green;
				} else if (j == 3) { // speed
					std::string speed_str = std::to_string(std::get<3>(game_scene.values[i]));
					speed_str.erase(speed_str.find_last_not_of('0') + 1, std::string::npos);
					speed_str.erase(speed_str.find_last_not_of('.') + 1, std::string::npos);
					label	   = "Speed: " + speed_str;
					stat_color = color::Blue;
				}
				V2_float pos = {
					first_stat_top_left.x + (first_stat_size.x + stat_offsets.x) * (float)i,
					first_stat_top_left.y + (first_stat_size.y + stat_offsets.y) * (float)j
				};
				Rect stat_box = { pos, first_stat_size };

				Text stat_text{ label.c_str(), stat_color, "2" };
				stat_text.Draw(stat_box);

				// stat_box.DrawSolid(color::Cyan);
			}
		}

		// Draw border around queue frame.
		Rect queue_frame_border = queue_frame;
		queue_frame_border.position += V2_int{ -4, -4 };
		queue_frame_border.size += V2_int{ queue_frame.size.x * (game_scene.max_queue_size - 1) + 8, 8 };

		game.draw.Rect(queue_frame_border, color::DarkBrown, 6.0f);
		game.draw.Rect(queue_frame_border, color::Black, 3.0f);

		// Draw UI displaying enemies in queue.
		int facing_direction = 7; // characters point to the bottom left.
		for (int i = 0; i < game_scene.enemy_queue.size(); i++) {
			Enemy type = game_scene.enemy_queue[i];
			Rect text_rect{ queue_frame };
			text_rect.position += V2_int{ queue_frame.size.x * i, 0 };
			game.draw.Texture(
				game.texture.Get(2000), text_rect,
				{ V2_float{ static_cast<float>(facing_direction),
													static_cast<float>(type) } *
											  tile_size,
										  tile_size }
			);
		}
		// Draw arrow over first enemy in queue.
		if (game_scene.enemy_queue.size() > 0) {
			V2_float arrow_size{ 15, 21 };
			Rect arrow = queue_frame;
			arrow.position += V2_int{ 0.0f, -arrow_size.y };
			game.draw.Texture(game.texture.Get(3001), arrow);
		}
		Rect sell_hint_box{ { queue_frame_border.position.x + queue_frame_border.size.x + 10,
										  queue_frame_border.position.y + 3 },
										{ 160, queue_frame_border.size.y - 6 } };
		sell_hint.Draw(sell_hint_box);
	}
};

class StartScreen : public Scene {
public:
	Button play;
	Texture play_t{ "resources/ui/play.png" };
	Texture play_t_hover{ "resources/ui/play_hover.png" };

	StartScreen() {
		play.Set<ButtonProperty::Texture>(play_t);
		play.Set<ButtonProperty::Texture>(play_t_hover, ButtonState::Hover);
		play.Set<ButtonProperty::Text>(Text{ "Play", color::White, "2" });
		play.Set<ButtonProperty::TextSize>(play_t.GetSize() / 2.0f);
		play.SetRect(Rect{ game.window.GetCenter(), play_t.GetSize(), Origin::Center });
		play.Set<ButtonProperty::OnActivate>([&]() {
			game.sound.Get("click").Play(3, 0);
			game.scene.Load<GameScene>("game");
			game.scene.TransitionActive("menu", "game");
		});
		play.Set<ButtonProperty::TextColor>(color::White);
		play.Set<ButtonProperty::TextColor>(color::Gold, ButtonState::Hover);
		game.music.Mute();
	}

	void Shutdown() override {
		play.Disable();
	}

	void Init() override {
		play.Enable();
	}

	void Update() final {
		game.music.Mute();
		game.draw.Texture(game.texture.Get(2));

		Text t3{ "Tower Offense", color::DarkGreen, "2" };
		t3.Draw(Rect{ game.window.GetCenter() - V2_int{ 0, 160 },
								{} });
		play.Draw();
	}
};

class LevelWinScreen : public Scene {
public:
	Button play;
	Texture play_t{ "resources/ui/play.png" };
	Texture play_t_hover{ "resources/ui/play_hover.png" };

	LevelWinScreen() {
		play.Set<ButtonProperty::Texture>(play_t);
		play.Set<ButtonProperty::Texture>(play_t_hover, ButtonState::Hover);
		play.Set<ButtonProperty::Text>(Text{ "Try Again", color::White, "2" });
		play.SetRect(Rect{ game.window.GetCenter(), {}, Origin::Center });
		play.Set<ButtonProperty::OnActivate>([&]() {
			game.sound.Get("click").Play(3, 0);
			game.scene.Load<GameScene>("game");
			game.scene.AddActive("game");
		});
		play.Set<ButtonProperty::TextColor>(color::White);
		play.Set<ButtonProperty::TextColor>(color::Gold, ButtonState::Hover);
		game.music.Mute();
	}

	void Update() final {
		game.music.Mute();
		game.draw.Texture(game.texture.Get(2));
		play.Draw();

		Text t{ "Thanks for playing!", color::Black, "2" };
		t.Draw(Rect{ game.window.GetCenter() - V2_int{ 250, 160 },
							   { 500, 250 } });
	}
};

class GMTKJam2023 : public Scene {
public:
	GMTKJam2023() {
		// Setup window configuration.
		game.window.SetTitle("Tower Offense");
		game.window.SetSize({ 1280, 720 });
		game.window.SetSetting(WindowSetting::Resizable);

		game.texture.Load(2, "resources/background/menu.png");
		game.font.Load("0", "resources/font/04B_30.ttf", 32);
		game.font.Load("1", "resources/font/retro_gaming.ttf", 32);
		game.font.Load("2", "resources/font/Deutsch.ttf", 32);
		game.sound.Load("click", "resources/sound/click.wav");
	}

	void Init() {
		game.draw.SetClearColor(color::Black);
		game.scene.Load<StartScreen>("menu");
		game.scene.Load<InstructionScreen>("instructions");
		game.scene.Load<LevelWinScreen>("game_win");
		game.scene.Load<BuyScreen>("buy_menu");
		game.scene.AddActive("menu");
	}
};

int main(int c, char** v) {
	game.Start<GMTKJam2023>();
	return 0;
}
