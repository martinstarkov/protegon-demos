#include <set>
#include <unordered_set>

#include "protegon/protegon.h"

using namespace ptgn;

const path level_json = "resources/data/levels.json";

[[nodiscard]] static json GetLevelData() {
	std::ifstream f(level_json);
	return json::parse(f);
}

constexpr V2_int resolution{ 1440, 810 };
constexpr V2_int center{ resolution / 2 };
constexpr bool draw_hitboxes{ true };

static void LoadMainMenu();

enum class TileType {
	Grass,
	Dirt,
	Corn,
	House,
	HouseDestroyed,
	None,
};

TileType GetTileType(float noise_value) {
	if (noise_value < 0.0f) {
		return TileType::None;
	}

	if (noise_value >= 0.0f && noise_value <= 0.6f) {
		return TileType::Grass;
	} else if (noise_value > 0.6f && noise_value <= 1.0f) {
		return TileType::Corn;
	}
	/*
	 else {
		return TileType::Grass;
	}
	*/
	PTGN_ERROR("Unrecognized tile type");
}

std::size_t GetTileKey(TileType tile_type) {
	switch (tile_type) {
		case TileType::Grass:		   return Hash("grass");
		case TileType::Corn:		   return Hash("corn");
		case TileType::Dirt:		   return Hash("dirt");
		case TileType::House:		   return Hash("house");
		case TileType::HouseDestroyed: return Hash("house_destroyed");
		case TileType::None:		   PTGN_ERROR("Cannot return tile key for none type tile");
		default:					   PTGN_ERROR("Unrecognized tile type");
	}
}

struct Transform {
	V2_float position;
	float rotation{ 0.0f };
};

struct Size : public V2_float {};

struct RigidBody {
	V2_float velocity;
	V2_float acceleration;
	float max_velocity{ 0.0f };
};

struct Aerodynamics {
	float pull_resistance{ 0.0f };
};

struct VehicleComponent {
	float throttle{ 0.0f };
	milliseconds throttle_time{ 0 };

	float thrust{ 0.0f };
	float backward_thrust_frac{ 0.0f };
	float turn_speed{ 0.0f };

	float inertia{ 0.0f };

	V2_float
		prev_acceleration; // acceleration in the current frame (cached even after it is cleared).

	Texture texture;
	Texture vehicle_texture;
	Texture wheel_texture;
	Texture vehicle_dirty_texture;

	float wheel_rotation{ 0.0f };
};

struct TintColor : public Color {
	using Color::Color;

	TintColor(const Color& color) : Color{ color } {}
};

struct CameraShake {};

struct Warning {
	void Init(ecs::Entity player) {
		game.tween.Load(Hash("warning_flash"))
			.During(milliseconds{ 500 })
			.Repeat(-1)
			.OnRepeat([=](Tween& tween) mutable {
				if (tween.GetRepeats() % 2 == 0) {
					if (player.Has<VehicleComponent>()) {
						auto& v	  = player.Get<VehicleComponent>();
						v.texture = v.vehicle_dirty_texture;
					}
					player.Add<CameraShake>();
				} else {
					if (player.Has<VehicleComponent>()) {
						auto& v	  = player.Get<VehicleComponent>();
						v.texture = v.vehicle_texture;
					}
					// player.Remove<TintColor>();
					player.Remove<CameraShake>();
				}
			})
			.OnStop([=]() mutable {
				if (player.Has<VehicleComponent>()) {
					auto& v	  = player.Get<VehicleComponent>();
					v.texture = v.vehicle_texture;
				}
				// player.Remove<TintColor>();
				player.Remove<CameraShake>();
			})
			.Start();
	}

	void Shutdown() {
		PTGN_ASSERT(game.tween.Has(Hash("warning_flash")));
		game.tween.Unload(Hash("warning_flash"));
	}
};

struct Lifetime {
	Timer timer;

	milliseconds lifetime{ 0 };

	void Start() {
		timer.Start();
	}

	bool Elapsed() const {
		PTGN_ASSERT(timer.IsRunning());
		PTGN_ASSERT(lifetime != milliseconds{ 0 });
		return timer.ElapsedPercentage(lifetime) >= 1.0f;
	}
};

static void ApplyBounds(ecs::Entity e, const Rectangle<float>& bounds) {
	if (!e.Has<Transform>()) {
		return;
	}
	auto& pos = e.Get<Transform>().position;
	V2_float min{ pos };
	V2_float max{ pos };
	V2_float bounds_max{ bounds.Max() };
	V2_float bounds_min{ bounds.Min() };
	if (min.x < bounds_min.x) {
		pos.x += bounds_min.x - min.x;
	} else if (max.x > bounds_max.x) {
		pos.x += bounds_max.x - max.x;
	}
	if (min.y < bounds_min.y) {
		pos.y += bounds_min.y - min.y;
	} else if (max.y > bounds_max.y) {
		pos.y += bounds_max.y - max.y;
	}
}

struct TornadoComponent {
	float turn_speed{ 0.0f };
	float gravity_radius{ 0.0f };
	float escape_radius{ 0.0f };
	float warning_radius{ 0.0f };
	float data_radius{ 0.0f };

	float outermost_increment_ratio{ 0.0f };
	float innermost_increment_ratio{ 1.0f };
	float increment_speed{ 1.0f };

	Color tint{ color::White };

	// Abritrary units related to frame rate.
	constexpr static float wind_constant{ 3.0f };

	// direction is a vector pointing from the target torward the tornado's center.
	// pull_resistance determines how much the target resists the inward pull of the tornado
	V2_float GetSuction(const V2_float& direction, float max_thrust) const {
		float dist2{ direction.MagnitudeSquared() };

		// haha.. very funny...
		float suction_force = escape_radius * max_thrust;

		V2_float suction{ direction / dist2 * suction_force };

		return suction;
	}

	V2_float GetWind(const V2_float& direction, float pull_resistance) const {
		float dist2{ direction.MagnitudeSquared() };

		float wind_speed = escape_radius * wind_constant * turn_speed / pull_resistance;

		V2_float tangent{ direction.Skewed() };

		V2_float wind{ tangent / dist2 * wind_speed };

		return wind;
	}

	ecs::Manager particle_manager;

	std::vector<ecs::Entity> available_entities;

	std::size_t max_particles{ 300 };

	// TODO: Make this a vector and choose randomly from that vector.
	Texture particle_texture{ "resources/entity/tornado_particle_1.png" };

	Timer particle_spawn_timer;

	constexpr static float particle_launch_speed{ 0.0f };

	milliseconds particle_spawn_cycle{ 100 };

	void CreateParticles(float dt, ecs::Entity tornado) {
		PTGN_ASSERT(tornado.Has<Transform>());
		PTGN_ASSERT(tornado.Has<RigidBody>());
		V2_float tornado_pos{ tornado.Get<Transform>().position };
		V2_float tornado_vel{ tornado.Get<RigidBody>().velocity };
		RNG<float> rng_pos{ -escape_radius, escape_radius };

		if (!particle_spawn_timer.IsRunning()) {
			particle_spawn_timer.Start();
			available_entities.reserve(max_particles);
			particle_manager.Reserve(max_particles);

			// milliseconds particle_lifetime{ 3000 };

			for (std::size_t i = 0; i < max_particles; i++) {
				auto particle = particle_manager.CreateEntity();
				available_entities.push_back(particle);
				auto& transform = particle.Add<Transform>();
				V2_float heading{ V2_float::RandomHeading() };
				transform.position	= tornado_pos + V2_float{ rng_pos(), rng_pos() };
				auto& rigid_body	= particle.Add<RigidBody>();
				rigid_body.velocity = tornado_vel;
			}

			particle_manager.Refresh();
		}

		if (particle_spawn_timer.ElapsedPercentage(particle_spawn_cycle) >= 1.0f) {
			particle_spawn_timer.Start();
		}

		if (particle_spawn_timer.ElapsedPercentage(particle_spawn_cycle) >= 0.5f) {
			return;
		}

		for (std::size_t i = 0; i < available_entities.size(); i++) {
			ecs::Entity particle = available_entities.back();
			available_entities.pop_back();
			auto& transform		= particle.Get<Transform>();
			transform.position	= tornado_pos + V2_float{ rng_pos(), rng_pos() };
			auto& rigid_body	= particle.Get<RigidBody>();
			rigid_body.velocity = tornado_vel;
		}
	}

	void UpdateParticles(float dt, ecs::Entity tornado) {
		PTGN_ASSERT(tornado.Has<Transform>());

		V2_float tornado_pos{ tornado.Get<Transform>().position };

		float particle_pull_resistance{ 0.1f };

		Circle<float> inner_deletion_circle{ tornado_pos, escape_radius * 0.1f };
		Circle<float> outer_deletion_circle{ tornado_pos, gravity_radius };

		const float particle_drag_force{ 0.01f };

		auto particles = particle_manager.EntitiesWith<Transform, RigidBody>();
		for (auto [e, transform, rigid_body] : particles) {
			/*if (lifetime.Elapsed()) {
				e.Destroy();
				continue;
			}*/

			V2_float dir{ tornado_pos - transform.position };

			rigid_body.acceleration += -rigid_body.velocity * particle_drag_force;
			rigid_body.velocity		+= GetSuction(dir, 200.0f) * dt;
			rigid_body.velocity		+= GetWind(dir, particle_pull_resistance) * dt;
			transform.position		+= rigid_body.velocity * dt;
			transform.rotation		+= turn_speed * dt;

			rigid_body.acceleration = {};

			if (game.collision.overlap.PointCircle(transform.position, inner_deletion_circle) ||
				!game.collision.overlap.PointCircle(transform.position, outer_deletion_circle)) {
				available_entities.push_back(e);
			}
		}

		CreateParticles(dt, tornado);
	}

	void DrawParticles() {
		auto particles = particle_manager.EntitiesWith<Transform>();
		for (auto [e, transform] : particles) {
			game.renderer.DrawTexture(
				particle_texture, transform.position, particle_texture.GetSize(), {}, {},
				Origin::Center, Flip::None, transform.rotation, { 0.5f, 0.5f }, 4.0f
			);
		}
	}
};

void BackToLevelSelect();

struct Progress {
	Texture texture;

	std::vector<ecs::Entity> completed_tornadoes;
	std::vector<ecs::Entity> required_tornadoes;

	ecs::Entity current_tornado;

	Progress(const path& ui_texture_path, const std::vector<ecs::Entity>& required_tornadoes) :
		texture{ ui_texture_path }, required_tornadoes{ required_tornadoes } {}

	float progress{ 0.0f };

	void Stop(ecs::Entity tornado) {
		if (tornado != current_tornado || tornado == ecs::null) {
			return;
		}

		progress = 0.0f;
	}

	void Start(ecs::Entity tornado) {
		PTGN_ASSERT(tornado != current_tornado);
		PTGN_ASSERT(!CompletedTornado(tornado));

		current_tornado = tornado;
		progress		= 0.0f;
	}

	void AddTornado(ecs::Entity tornado) {
		PTGN_ASSERT(tornado == current_tornado);
		PTGN_ASSERT(current_tornado.Has<TornadoComponent>());
		completed_tornadoes.push_back(current_tornado);

		// TODO: Remove tornado tint once indicators exist.
		current_tornado.Get<TornadoComponent>().tint = color::Green;

		current_tornado = ecs::null;
		progress		= 0.0f;

		// PTGN_LOG("Completed tornado with EntityID: ", tornado.GetId());

		if (completed_tornadoes == required_tornadoes) {
			// PTGN_LOG("All required tornadoes completed!");
			BackToLevelSelect();
		}
		// TODO: Some kind of particle effects? Change tornado indicator to completed?
	}

	void DrawTornadoIcons() {
		if (game.tween.Has(Hash("pulled_in_tween"))) {
			return;
		}

		const int icon_x_offset{ 10 };
		const int icon_y_offset{ 10 };

		PTGN_ASSERT(game.texture.Has(Hash("tornado_icon")));
		PTGN_ASSERT(game.texture.Has(Hash("tornado_icon_green")));

		const Texture tornado_icon = game.texture.Get(Hash("tornado_icon"));

		const V2_float icon_size{ tornado_icon.GetSize() };

		float total_width{ icon_size.x * required_tornadoes.size() +
						   (required_tornadoes.size() - 1) * icon_x_offset };

		V2_float start_pos{ center.x - total_width / 2.0f, icon_y_offset };

		for (int i = 0; i < required_tornadoes.size(); ++i) {
			auto tornado{ required_tornadoes[i] };
			Texture t = tornado_icon;
			if (CompletedTornado(tornado)) {
				t = game.texture.Get(Hash("tornado_icon_green"));
			}
			V2_float pos = start_pos + V2_float{ i * (icon_size.x + icon_x_offset), 0 };

			game.renderer.DrawTexture(t, pos, icon_size, {}, {}, Origin::TopLeft);
		}
	}

	void DrawTornadoProgress() {
		if (progress <= 0.0f || current_tornado == ecs::null) {
			return;
		}

		V2_float meter_pos{
			4.0f, resolution.y / 2.0f
		}; // center bottom screen position of the tornado progress indicator.

		const float scale{ 2.0f };

		V2_float meter_size{ texture.GetSize() * scale };

		Color color = Lerp(color::Grey, color::Green, progress);

		V2_float border_size{ V2_float{ 4, 4 } * scale };

		V2_float fill_size{ meter_size - border_size * 2.0f };

		V2_float fill_pos{ meter_pos.x + border_size.x, meter_pos.y + fill_size.y / 2.0f };

		game.renderer.DrawTexture(texture, meter_pos, meter_size, {}, {}, Origin::CenterLeft);

		game.renderer.DrawRectangleFilled(
			fill_pos, { fill_size.x, fill_size.y * progress }, color, Origin::BottomLeft
		);
	}

	void Draw() {
		DrawTornadoProgress();
		DrawTornadoIcons();
	}

	[[nodiscard]] bool CompletedTornado(ecs::Entity tornado) {
		for (const auto& e : completed_tornadoes) {
			if (e == tornado) {
				return true;
			}
		}
		return false;
	}

	void Update(
		ecs::Entity tornado, const V2_float& player_pos, const V2_float& tornado_center,
		float data_radius, float escape_radius, float dt
	) {
		PTGN_ASSERT(data_radius != 0.0f);
		PTGN_ASSERT(escape_radius != 0.0f);

		if (CompletedTornado(tornado)) {
			return;
		}

		if (tornado != current_tornado) {
			bool start_over{ false };

			if (current_tornado != ecs::null) {
				PTGN_ASSERT(current_tornado.Has<Transform>());
				float dist2old{
					(player_pos - current_tornado.Get<Transform>().position).MagnitudeSquared()
				};
				// If the new tornado is closer than the previously imaged one, start imaging the
				// new one and drop the old one.
				float dist2new{ (player_pos - tornado_center).MagnitudeSquared() };
				if (dist2new < dist2old) {
					start_over = true;
				}
			} else {
				start_over = true;
			}

			if (start_over) {
				Start(tornado);
			}
		}

		V2_float dir{ tornado_center - player_pos };
		float dist{ dir.Magnitude() };
		PTGN_ASSERT(dist <= data_radius);
		PTGN_ASSERT(data_radius > escape_radius);
		float range{ data_radius - escape_radius };

		if (dist <= escape_radius) {
			progress = 0.0f;
			return;
		}

		float dist_from_escape{ dist - escape_radius };

		float normalized_dist{ dist_from_escape / range };
		PTGN_ASSERT(normalized_dist >= 0.0f);
		PTGN_ASSERT(normalized_dist <= 1.0f);

		TornadoComponent tornado_properties{ tornado.Get<TornadoComponent>() };

		PTGN_ASSERT(
			tornado_properties.outermost_increment_ratio <=
			tornado_properties.innermost_increment_ratio
		);
		float increment_ratio{ Lerp(
			tornado_properties.innermost_increment_ratio,
			tornado_properties.outermost_increment_ratio, normalized_dist
		) };

		progress += tornado_properties.increment_speed * increment_ratio * dt;

		progress = std::clamp(progress, 0.0f, 1.0f);

		if (progress >= 1.0f) {
			AddTornado(tornado);
		}
	}
};

class GameScene : public Scene {
public:
	ecs::Manager manager;

	ecs::Entity player;

	const V2_int tile_size{ 16, 16 };
	const V2_int grid_size{ static_cast<int>(1 * resolution.x / tile_size.x),
							static_cast<int>(2 * resolution.y / tile_size.y) };

	NoiseProperties noise_properties;
	std::vector<float> noise_map;
	const ValueNoise noise{ 256, 0 };

	std::unordered_set<V2_int> destroyed_tiles;

	std::vector<ecs::Entity> required_tornadoes;

	json level_data;

	GameScene(int level) {
		PTGN_LOG("Playing level: ", level);
	}

	void RestartGame() {
		BackToLevelSelect();

		/*destroyed_tiles.clear();
		required_tornadoes.clear();
		manager.Reset();
		Init();*/
	}

	Rectangle<float> bounds;

	void Shutdown() final {
		game.tween.Clear();
	}

	void Init() final {
		level_data = GetLevelData();

		auto& primary{ camera.GetCurrent() };

		bounds.pos	  = {};
		bounds.size	  = grid_size * tile_size;
		bounds.origin = Origin::TopLeft;

		primary.SetBounds(bounds);
		primary.SetZoom(2.0f);

		noise_properties.octaves	 = 2;
		noise_properties.frequency	 = 0.045f;
		noise_properties.bias		 = 1.21f;
		noise_properties.persistence = 0.65f;

		game.texture.Load(Hash("grass"), "resources/entity/grass.png");
		game.texture.Load(Hash("dirt"), "resources/entity/dirt.png");
		game.texture.Load(Hash("corn"), "resources/entity/corn.png");
		game.texture.Load(Hash("house"), "resources/entity/house.png");
		game.texture.Load(Hash("house_destroyed"), "resources/entity/house_destroyed.png");
		game.texture.Load(Hash("tornado_icon"), "resources/ui/tornado_icon.png");
		game.texture.Load(Hash("tornado_icon_green"), "resources/ui/tornado_icon_green.png");
		game.texture.Load(Hash("speedometer"), "resources/ui/speedometer.png");

		CreateTornado(center + V2_float{ 200, 200 }, 50.0f);

		CreateTornado(center - V2_float{ 200, 200 }, 50.0f);

		CreateBackground();

		// player must be created after tornadoes.
		player = CreatePlayer(
			V2_float{ grid_size.x / 2.0f, static_cast<float>(grid_size.y) } * tile_size -
			V2_float{ 0.0f, resolution.y / 2.0f }
		);

		manager.Refresh();
	}

	void Update(float dt) final {
		PlayerInput(dt);

		UpdateTornados(dt);

		PlayerPhysics(dt);

		UpdateBackground();

		if (game.input.KeyDown(Key::R)) {
			RestartGame();
		}

		Draw();
	}

	void Draw() {
		DrawBackground();

		DrawPlayer();

		DrawTornados();

		DrawUI();
	}

	// Init functions.

	ecs::Entity CreatePlayer(const V2_float& pos) {
		ecs::Entity entity = manager.CreateEntity();
		manager.Refresh();

		Texture vehicle_texture{ "resources/entity/car.png" };
		Texture vehicle_dirty_texture{ "resources/entity/car_dirty.png" };
		Texture wheel_texture{ "resources/entity/wheels.png" };

		entity.Add<Size>(vehicle_texture.GetSize());
		PTGN_ASSERT(required_tornadoes.size() != 0);
		entity.Add<Progress>("resources/ui/tornadometer.png", required_tornadoes);

		auto& transform	   = entity.Add<Transform>();
		transform.position = pos;
		transform.rotation = -half_pi<float>;

		auto& rigid_body		= entity.Add<RigidBody>();
		rigid_body.max_velocity = 225.0f;

		auto& vehicle				  = entity.Add<VehicleComponent>();
		vehicle.throttle_time		  = milliseconds{ 500 };
		vehicle.thrust				  = 3000.0f;
		vehicle.backward_thrust_frac  = 0.6f;
		vehicle.turn_speed			  = 5.0f;
		vehicle.inertia				  = 200.0f;
		vehicle.vehicle_texture		  = vehicle_texture;
		vehicle.vehicle_dirty_texture = vehicle_dirty_texture;
		vehicle.wheel_texture		  = wheel_texture;
		vehicle.texture				  = vehicle_texture;

		auto& aero			 = entity.Add<Aerodynamics>();
		aero.pull_resistance = 1.0f;

		return entity;
	}

	ecs::Entity CreateTornado(const V2_float& position, float turn_speed) {
		ecs::Entity entity = manager.CreateEntity();
		manager.Refresh();

		auto& texture = entity.Add<Texture>(Texture{ "resources/entity/tornado.png" });

		auto& transform	   = entity.Add<Transform>();
		transform.position = position;

		auto& size = entity.Add<Size>(texture.GetSize());

		auto& tornado = entity.Add<TornadoComponent>();

		tornado.turn_speed		= turn_speed;
		tornado.escape_radius	= size.x / 2.0f;
		tornado.data_radius		= 4.0f * tornado.escape_radius;
		tornado.gravity_radius	= 8.0f * tornado.escape_radius;
		tornado.warning_radius	= 3.0f * tornado.escape_radius;
		tornado.increment_speed = 0.5f;

		auto& rigid_body		= entity.Add<RigidBody>();
		rigid_body.max_velocity = 137.0f;

		PTGN_ASSERT(tornado.warning_radius > tornado.escape_radius);
		PTGN_ASSERT(tornado.data_radius > tornado.escape_radius);
		PTGN_ASSERT(tornado.gravity_radius >= tornado.data_radius);

		required_tornadoes.push_back(entity);

		return entity;
	}

	void CreateBackground() {
		noise_map = FractalNoise::Generate(noise, {}, grid_size, noise_properties);
	}

	// Update functions.

	void UpdateBackground() {}

	void PlayerInput(float dt) {
		PTGN_ASSERT(player.Has<RigidBody>());
		PTGN_ASSERT(player.Has<VehicleComponent>());
		PTGN_ASSERT(player.Has<Transform>());

		auto& rigid_body = player.Get<RigidBody>();
		auto& vehicle	 = player.Get<VehicleComponent>();
		auto& transform	 = player.Get<Transform>();

		bool up{ game.input.KeyPressed(Key::W) };
		bool left{ game.input.KeyPressed(Key::A) };
		bool down{ game.input.KeyPressed(Key::S) };
		bool right{ game.input.KeyPressed(Key::D) };

		const float wheel_rotation_angle{ pi<float> / 8.0f };

		if (right) {
			vehicle.wheel_rotation = wheel_rotation_angle;
		}
		if (left) {
			vehicle.wheel_rotation = -wheel_rotation_angle;
		}

		if (!left && !right) {
			vehicle.wheel_rotation = 0.0f;
		}

		float direction{ transform.rotation + vehicle.turn_speed * vehicle.wheel_rotation * dt };

		V2_float unit_direction{ V2_float{ 1.0f, 0.0f }.Rotated(direction) };

		V2_float thrust;

		bool throttling{ game.tween.Has(Hash("throttle_tween")) };

		if ((up || down) && !(up && down)) {
			if (!throttling) {
				const auto reset_throttle = [=]() {
					player.Get<VehicleComponent>().throttle = 0.0f;
				};

				game.tween.Load(Hash("throttle_tween"))
					.During(vehicle.throttle_time)
					.OnUpdate([=](Tween& tween, float f) {
						PTGN_ASSERT(player.Has<VehicleComponent>());
						auto& throttle{ player.Get<VehicleComponent>().throttle };
						// PTGN_LOG("Updating throttle: ", f);
						bool u{ game.input.KeyPressed(Key::W) };
						bool d{ game.input.KeyPressed(Key::S) };
						if (d) {
							throttle = -f;
						}
						if (u) {
							throttle = f;
						}
						if (!u && !d || u && d) {
							throttle = 0.0f;
							tween.Reset();
						}
					})
					.OnStop(reset_throttle)
					.Start();
				game.tween.KeepAlive(Hash("throttle_tween"));
			} else {
				auto& throttle = game.tween.Get(Hash("throttle_tween"));
				if (!throttle.IsStarted() && !throttle.IsCompleted()) {
					throttle.Start();
				}
			}
		} else {
			if (throttling) {
				auto& throttle = game.tween.Get(Hash("throttle_tween"));
				throttle.Reset();
			}
		}

		if (up) {
			transform.rotation = direction;
			thrust			   = unit_direction * vehicle.thrust * vehicle.throttle;
		} else if (down) {
			transform.rotation =
				transform.rotation - vehicle.turn_speed * vehicle.wheel_rotation * dt;
			// Negative contained in vehicle.throttle.
			thrust =
				unit_direction * vehicle.thrust * vehicle.backward_thrust_frac * vehicle.throttle;
		}

		rigid_body.acceleration += thrust;
	}

	void PlayerPhysics(float dt) {
		PTGN_ASSERT(player.Has<RigidBody>());
		PTGN_ASSERT(player.Has<Transform>());
		PTGN_ASSERT(player.Has<VehicleComponent>());

		auto& rigid_body = player.Get<RigidBody>();
		auto& transform	 = player.Get<Transform>();
		auto& vehicle	 = player.Get<VehicleComponent>();

		const float drag{ 5.0f };

		rigid_body.acceleration += -rigid_body.velocity * drag;

		rigid_body.velocity += rigid_body.acceleration * dt;

		// TODO: Fix this.
		if (game.input.KeyPressed(Key::S)) {
			rigid_body.velocity = Clamp(
				rigid_body.velocity, -rigid_body.max_velocity * vehicle.backward_thrust_frac,
				rigid_body.max_velocity * vehicle.backward_thrust_frac
			);
		} else {
			rigid_body.velocity =
				Clamp(rigid_body.velocity, -rigid_body.max_velocity, rigid_body.max_velocity);
		}

		// Zeros velocity when below a certain magnitude.
		/*float vel_mag2{ rigid_body.velocity.MagnitudeSquared() };

		constexpr float velocity_zeroing_threshold{ 1.0f };

		if (vel_mag2 < velocity_zeroing_threshold) {
			rigid_body.velocity = {};
		}*/

		transform.position += rigid_body.velocity * dt;

		ApplyBounds(player, bounds);

		// Center camera on player.
		auto& primary{ camera.GetCurrent() };

		V2_float shake;

		if (player.Has<CameraShake>()) {
			float camera_shake_amplitude{ 1.0f };
			shake = V2_float::RandomHeading() * camera_shake_amplitude;
		}

		primary.SetPosition(transform.position + shake);

		V2_int player_tile = transform.position / tile_size;

		TileType tile_type = GetTileType(GetNoiseValue(player_tile));

		if (tile_type == TileType::Corn) {
			destroyed_tiles.insert(player_tile);
		}

		vehicle.prev_acceleration = rigid_body.acceleration;

		rigid_body.acceleration = {};
	}

	void UpdateTornadoGravity(float dt) {
		auto tornadoes = manager.EntitiesWith<TornadoComponent, Transform, RigidBody>();

		PTGN_ASSERT(player.Has<Transform>());
		PTGN_ASSERT(player.Has<RigidBody>());
		PTGN_ASSERT(player.Has<Aerodynamics>());
		PTGN_ASSERT(player.Has<VehicleComponent>());
		PTGN_ASSERT(player.Has<Progress>());

		auto& player_transform{ player.Get<Transform>() };
		VehicleComponent player_vehicle{ player.Get<VehicleComponent>() };

		float player_max_thrust{ player_vehicle.thrust };

		auto& player_rigid_body{ player.Get<RigidBody>() };
		const auto& player_aero{ player.Get<Aerodynamics>() };

		bool within_danger{ false };

		for (auto [e, tornado, transform, rigid_body] : tornadoes) {
			if (!game.collision.overlap.PointCircle(
					player_transform.position, { transform.position, tornado.gravity_radius }
				)) {
				continue;
			}

			V2_float dir{ transform.position - player_transform.position };
			V2_float wind_speed{ tornado.GetWind(dir, player_aero.pull_resistance) * dt };
			player_rigid_body.velocity	   += wind_speed;
			player_transform.rotation	   += wind_speed.Magnitude() / player_vehicle.inertia;
			player_rigid_body.acceleration += tornado.GetSuction(dir, player_max_thrust);
			player_rigid_body.velocity	   += rigid_body.velocity * dt;

			if (!game.collision.overlap.PointCircle(
					player_transform.position, { transform.position, tornado.data_radius }
				)) {
				continue;
			}

			player.Get<Progress>().Update(
				e, player_transform.position, transform.position, tornado.data_radius,
				tornado.escape_radius, dt
			);

			if (game.collision.overlap.PointCircle(
					player_transform.position, { transform.position, tornado.warning_radius }
				)) {
				within_danger = true;
			} else {
				continue;
			}

			if (!game.collision.overlap.PointCircle(
					player_transform.position, { transform.position, tornado.escape_radius }
				)) {
				continue;
			}

			std::size_t tween_key{ Hash("pulled_in_tween") };

			if (game.tween.Has(tween_key)) {
				continue;
			}

			game.tween.Load(tween_key)
				.During(milliseconds{ 3000 })
				.OnStart([=]() { player.Add<TintColor>(); })
				.OnComplete([=]() {
					player.Remove<TintColor>();
					RestartGame();
				})
				.OnUpdate([=]() {
					PTGN_ASSERT(player.IsAlive());
					PTGN_ASSERT(player.Has<RigidBody>());
					PTGN_ASSERT(player.Has<Transform>());
					player.Get<RigidBody>().acceleration.x = player_vehicle.thrust;
					player.Get<Transform>().rotation += 10.0f * player_vehicle.turn_speed * dt;
				})
				.Start();
		}

		if (within_danger) {
			if (!player.Has<Warning>()) {
				player.Add<Warning>().Init(player);
			}
		} else {
			if (player.Has<Warning>()) {
				player.Get<Warning>().Shutdown();
				player.Remove<Warning>();
			}
		}
	}

	void UpdateTornados(float dt) {
		TornadoMotion(dt);
		UpdateTornadoGravity(dt);
	}

	void TornadoMotion(float dt) {
		auto tornadoes = manager.EntitiesWith<TornadoComponent, Transform, RigidBody>();

		const float tornado_move_speed{ 1000.0f };

		for (auto [e, tornado, transform, rigid_body] : tornadoes) {
			// TODO: Remove
			if (game.input.KeyDown(Key::LEFT)) {
				rigid_body.velocity.x -= tornado_move_speed * dt;
			} else if (game.input.KeyDown(Key::RIGHT)) {
				rigid_body.velocity.x += tornado_move_speed * dt;
			}
			if (game.input.KeyDown(Key::UP)) {
				rigid_body.velocity.y -= tornado_move_speed * dt;
			} else if (game.input.KeyDown(Key::DOWN)) {
				rigid_body.velocity.y += tornado_move_speed * dt;
			}

			rigid_body.velocity =
				Clamp(rigid_body.velocity, -rigid_body.max_velocity, rigid_body.max_velocity);

			transform.position += rigid_body.velocity * dt;

			V2_int min{ (transform.position -
						 V2_float{ tornado.escape_radius, tornado.escape_radius }) /
						tile_size };
			V2_int max{ (transform.position +
						 V2_float{ tornado.escape_radius, tornado.escape_radius }) /
						tile_size };

			Circle<float> tornado_destruction{ transform.position, tornado.escape_radius };

			PTGN_ASSERT(min.x <= max.x);
			PTGN_ASSERT(min.y <= max.y);

			// Destroy all tiles within escape radius of tornado
			for (int i = min.x; i <= max.x; i++) {
				for (int j = min.y; j <= max.y; j++) {
					V2_int tile{ i, j };
					Rectangle<float> tile_rect{ tile * tile_size, tile_size, Origin::TopLeft };
					auto tile_type = GetTileType(GetNoiseValue(tile));
					if (game.collision.overlap.CircleRectangle(tornado_destruction, tile_rect)) {
						game.renderer.DrawRectangleFilled(tile_rect, color::Purple);
						destroyed_tiles.insert(tile);
					}
				}
			}

			transform.rotation += tornado.turn_speed * dt;

			tornado.UpdateParticles(dt, e);
		}
	}

	// Draw functions.

	void DrawPlayer() {
		PTGN_ASSERT(player.Has<Transform>());
		PTGN_ASSERT(player.Has<Size>());
		PTGN_ASSERT(player.Has<VehicleComponent>());

		auto& player_transform{ player.Get<Transform>() };
		auto& vehicle{ player.Get<VehicleComponent>() };
		Size size{ player.Get<Size>() };

		Color tint{ player.Has<TintColor>() ? player.Get<TintColor>() : color::White };

		V2_float relative_wheel_pos =
			V2_float{ (25 - 15), 0.0f }.Rotated(player_transform.rotation);

		game.renderer.DrawTexture(
			vehicle.wheel_texture, player_transform.position + relative_wheel_pos,
			vehicle.wheel_texture.GetSize(), {}, {}, Origin::Center, Flip::None,
			player_transform.rotation + vehicle.wheel_rotation, { 0.5f, 0.5f }, 0.0f, tint
		);

		game.renderer.DrawTexture(
			vehicle.texture, player_transform.position, size, {}, {}, Origin::Center, Flip::None,
			player_transform.rotation, { 0.5f, 0.5f }, 0.0f, tint

		);
	}

	void DrawTornados() {
		auto tornadoes = manager.EntitiesWith<TornadoComponent, Texture, Transform, Size>();

		for (auto [e, tornado, texture, transform, size] : tornadoes) {
			game.renderer.DrawTexture(
				texture, transform.position, size, {}, {}, Origin::Center, Flip::None,
				transform.rotation, { 0.5f, 0.5f }, 2.0f, tornado.tint
			);

			if (draw_hitboxes) {
				game.renderer.DrawCircleHollow(
					transform.position, tornado.gravity_radius, color::Blue, 1.0f, 0.005f, 3.0f
				);
				game.renderer.DrawCircleHollow(
					transform.position, tornado.escape_radius, color::Red, 1.0f, 0.005f, 3.0f
				);
				game.renderer.DrawCircleHollow(
					transform.position, tornado.warning_radius, color::Orange, 1.0f, 0.005f, 3.0f
				);
				game.renderer.DrawCircleHollow(
					transform.position, tornado.data_radius, color::DarkGreen, 1.0f, 0.005f, 3.0f
				);
			}

			tornado.DrawParticles();
		}
	}

	float GetNoiseValue(const V2_int& tile) {
		int index{ tile.x + grid_size.x * tile.y };
		if (index >= noise_map.size() || index < 0) {
			return -1.0f;
		}
		float noise_value{ noise_map[index] };
		PTGN_ASSERT(noise_value >= 0.0f);
		PTGN_ASSERT(noise_value <= 1.0f);
		return noise_value;
	}

	void DrawBackground() {
		const auto& primary{ camera.GetCurrent() };
		Rectangle<float> camera_rect{ primary.GetRectangle() };

		// game.renderer.DrawRectangleHollow(camera_rect, color::Blue, 3.0f);

		Rectangle<float> tile_rect{ {}, tile_size, Origin::TopLeft };

		// Expand size of each tile to include neighbors to prevent edges from flashing
		// when camera moves. Skip grid tiles not within camera view.

		V2_int min{ Clamp(
			V2_int{ camera_rect.Min() / tile_size } - V2_int{ 1, 1 }, V2_int{ 0, 0 }, grid_size
		) };
		V2_int max{ Clamp(
			V2_int{ camera_rect.Max() / tile_size } + V2_int{ 1, 1 }, V2_int{ 0, 0 }, grid_size
		) };

		for (int i{ min.x }; i < max.x; i++) {
			for (int j{ min.y }; j < max.y; j++) {
				V2_int tile{ i, j };

				tile_rect.pos = tile * tile_size;

				float noise_value{ GetNoiseValue(tile) };

				PTGN_ASSERT(noise_value >= 0.0f);

				bool destroyed_tile{ destroyed_tiles.count(tile) > 0 };

				TileType tile_type = GetTileType(noise_value);

				auto size = tile_rect.size;
				float z_index{ 0.0f };

				if (tile_type == TileType::House) {
					// size	= tile_rect.size * 3;
					z_index = 1.0f;
				}

				if (destroyed_tile) {
					if (tile_type == TileType::House) {
						tile_type = TileType::HouseDestroyed;
					} else {
						tile_type = TileType::Dirt;
					}
				}

				Texture t = game.texture.Get(GetTileKey(tile_type));

				game.renderer.DrawTexture(
					t, tile_rect.pos, size, {}, {}, Origin::TopLeft, Flip::None, 0.0f,
					{ 0.5f, 0.5f }, z_index
				);
			}
		}
	}

	void DrawSpeedometer() {
		if (game.tween.Has(Hash("pulled_in_tween"))) {
			return;
		}

		PTGN_ASSERT(game.texture.Has(Hash("speedometer")));

		Texture texture{ game.texture.Get(Hash("speedometer")) };

		V2_float meter_pos{ V2_float{ static_cast<float>(resolution.x), 0.0f } +
							V2_float{ -10.0f, 10.0f } };
		V2_float meter_size{ texture.GetSize() };

		PTGN_ASSERT(player.Has<RigidBody>());
		PTGN_ASSERT(player.Has<Transform>());
		PTGN_ASSERT(player.Has<VehicleComponent>());

		RigidBody rigid_body{ player.Get<RigidBody>() };
		Transform transform{ player.Get<Transform>() };
		VehicleComponent vehicle{ player.Get<VehicleComponent>() };

		float forward_velocity =
			rigid_body.velocity.Dot(V2_float{ 1.0f, 0.0f }.Rotated(transform.rotation));

		// float forward_accel = vehicle.prev_acceleration.Dot(V2_float{ 1.0f, 0.0f
		// }.Rotated(transform.rotation));

		float fraction{ vehicle.throttle };
		// float fraction{ forward_velocity / rigid_body.max_velocity };
		//  float fraction{ forward_accel / vehicle.forward_thrust };

		// TODO: Consider not clamping this to show negative velocity.
		fraction = std::clamp(fraction, 0.0f, 1.0f);

		Color fill_color = Lerp(color::Red, color::Green, fraction);

		V2_float border_size{ 4, 4 };

		V2_float fill_size{ meter_size - border_size * 2.0f };

		V2_float fill_pos{ meter_pos.x - border_size.x - fill_size.x,
						   meter_pos.y + border_size.y + fill_size.y };

		Color meter_background = color::White;

		meter_background.a = 128;
		fill_color.a	   = 200;

		game.renderer.DrawRectangleFilled(
			fill_pos, { fill_size.x, fill_size.y }, meter_background, Origin::BottomLeft
		);

		game.renderer.DrawRectangleFilled(
			fill_pos, { fill_size.x * fraction, fill_size.y }, fill_color, Origin::BottomLeft
		);

		game.renderer.DrawTexture(texture, meter_pos, meter_size, {}, {}, Origin::TopRight);
	}

	void DrawUI() {
		game.renderer.Flush();
		game.camera.SetCameraWindow();

		// Draw UI here...

		PTGN_ASSERT(player.Has<Progress>());
		player.Get<Progress>().Draw();
		DrawSpeedometer();

		game.renderer.Flush();
		game.camera.SetCameraPrimary();
	}
};

struct TextButton {
	TextButton(const std::shared_ptr<Button>& button, const Text& text) :
		button{ button }, text{ text } {}

	std::shared_ptr<Button> button;
	Text text;
};

const int text_x_offset{ 14 };
const int button_y_offset{ 14 };
const V2_int button_size{ 150, 50 };
const V2_int first_button_coordinate{ 40, 180 };

TextButton CreateMenuButton(
	const std::string& content, Color text_color, const ButtonActivateFunction& f,
	const Color& color, Color hover_color
) {
	ColorButton b;
	Text text{ Hash("menu_font"), content, text_color };
	b.SetOnHover(
		[=]() mutable { text.SetColor(hover_color); }, [=]() mutable { text.SetColor(text_color); }
	);
	b.SetOnActivate(f);
	b.SetColor(color);
	b.SetHoverColor(hover_color);
	return TextButton{ std::make_shared<ColorButton>(b), text };
}

class TextScreen : public Scene {
public:
	std::vector<TextButton> buttons;

	Font font{ "resources/font/retro_gaming.ttf", 18 };

	Text text{ font, "", color::Black };

	std::string back_name = "main_menu";

	TextScreen() {
		if (!game.font.Has(Hash("menu_font"))) {
			game.font.Load(Hash("menu_font"), "resources/font/retro_gaming.ttf", button_size.y);
		}
		if (!game.texture.Has(Hash("text_screen_background"))) {
			game.texture.Load(Hash("text_screen_background"), "resources/ui/laptop_text.png");
		}
	}

	V2_float max_text_dim{ 362, 253 };

	Rectangle<float> text_rect{ { 554, 174 }, max_text_dim, Origin::TopLeft };

	void Init() final {
		buttons.clear();
		buttons.push_back(CreateMenuButton(
			"Back", color::White,
			[&]() {
				game.scene.RemoveActive(Hash("text_screen"));
				if (!game.scene.Has(Hash(back_name))) {
					LoadMainMenu();
				}
				PTGN_ASSERT(game.scene.Has(Hash(back_name)));
				game.scene.AddActive(Hash(back_name));
			},
			color::LightGrey, color::Black
		));

		buttons[0].button->SetRectangle({ V2_int{ 820, 636 }, button_size, Origin::TopLeft });

		for (int i = 0; i < (int)buttons.size(); i++) {
			buttons[i].button->SubscribeToMouseEvents();
		}
	}

	void Shutdown() final {
		for (int i = 0; i < (int)buttons.size(); i++) {
			buttons[i].button->UnsubscribeFromMouseEvents();
		}
	}

	void Update() final {
		game.renderer.DrawTexture(
			game.texture.Get(Hash("text_screen_background")), game.window.GetCenter(), resolution,
			{}, {}, Origin::Center, Flip::None, 0.0f, {}, -1.0f
		);

		for (std::size_t i = 0; i < buttons.size(); i++) {
			auto rect	= buttons[i].button->GetRectangle();
			rect.pos.x += text_x_offset;
			rect.size.x =
				buttons[i]
					.text.GetSize(Hash("menu_font"), std::string(buttons[i].text.GetContent()))
					.x *
				0.5f;
			buttons[i].text.Draw(rect);
		}
		text_rect.size.x = (float)std::clamp(text.GetSize().x, 0, static_cast<int>(max_text_dim.x));
		text.SetWrapAfter(static_cast<std::uint32_t>(text_rect.size.x));
		text.Draw(text_rect);
	}
};

class LevelSelect : public Scene {
public:
	std::vector<TextButton> buttons;

	std::size_t difficulty_layer{ 0 };

	std::set<int> completed_levels;

	json level_data;

	bool CompletedLevel(int level) {
		return completed_levels.count(level) > 0;
	}

	bool ShownLevel(int level) {
		return std::any_of(shown_levels.begin(), shown_levels.end(), [=](int l) {
			return level == l;
		});
	}

	json GetLevel(int level) const {
		for (auto& l : level_data["levels"]) {
			if (l["id"] == level) {
				return l;
			}
		}
		PTGN_ERROR("Failed to find level in json");
	}

	std::string GetDetails(int level) const {
		PTGN_ASSERT(level != -1);
		auto l = GetLevel(level);
		return l["details"];
	}

	LevelSelect() {
		if (!game.font.Has(Hash("menu_font"))) {
			game.font.Load(Hash("menu_font"), "resources/font/retro_gaming.ttf", button_size.y);
		}
		if (!game.texture.Has(Hash("level_select_background"))) {
			game.texture.Load(Hash("level_select_background"), "resources/ui/laptop.png");
		}
	}

	void StartGame(int level) {
		game.scene.RemoveActive(Hash("level_select"));
		game.scene.Load<GameScene>(Hash("game"), level);
		game.scene.AddActive(Hash("game"));
	}

	// level, button, tint
	std::vector<std::tuple<int, std::shared_ptr<TexturedToggleButton>>> level_buttons;

	std::vector<int> shown_levels;

	void ToggleOtherLevel() {
		for (auto& [l, button] : level_buttons) {
			if (l != selected_level) {
				button->SetTintColor(color::White);
				button->SetToggleState(false);
			}
		}
	}

	void CreateLevelButton(int level) {
		Rectangle rect;

		auto l			= GetLevel(level);
		std::size_t key = Hash(l["ui_icon"]);

		PTGN_ASSERT(game.texture.Has(key));

		Texture texture = game.texture.Get(key);
		rect.pos		= game.window.GetCenter();
		rect.size		= texture.GetSize();
		rect.origin		= Origin::Center;

		auto button = std::make_shared<TexturedToggleButton>(
			rect, std::vector<TextureOrKey>{ texture, texture }
		);

		Color select_color = color::Black;
		Color hover_color  = color::Grey;

		button->SetOnActivate([this, button, level, select_color]() {
			selected_level = level;
			PTGN_INFO("Selected level: ", level);
			button->SetTintColor(select_color);
			ToggleOtherLevel();
		});
		button->SetOnHover(
			[=]() {
				if (button->GetTintColor() == color::White) {
					button->SetTintColor(hover_color);
					PTGN_INFO("Hover started on button for level: ", level);
				}
			},
			[=]() {
				if (button->GetTintColor() == hover_color) {
					button->SetTintColor(color::White);
					PTGN_INFO("Hover stopped on button for level: ", level);
				}
			}
		);
		level_buttons.emplace_back(level, button);
	}

	int selected_level{ -1 };

	int GetValidLevel() {
		int level = -1;
		int i	  = 0;
		while (i < 3000) {
			i++;
			int branch	   = branch_rng();
			auto& branches = level_data["branches"];
			PTGN_ASSERT(
				branch < branches.size(), "Randomly selected branch out of range of branches"
			);
			auto& levels = branches[branch];
			if (difficulty_layer >= levels.size()) {
				continue;
			}
			int potential_level = levels[difficulty_layer];
			if (CompletedLevel(potential_level) || ShownLevel(potential_level)) {
				continue;
			}
			return potential_level;
		}
		return level;
	}

	V2_float level_button_offset0{ 0, -100 };
	V2_float level_button_offset1{ -100, -170 };
	V2_float level_button_offset2{ +120, -50 };

	void ClearChoices() {
		for (int i = 0; i < (int)level_buttons.size(); i++) {
			std::get<1>(level_buttons[i])->UnsubscribeFromMouseEvents();
		}

		level_buttons.clear();
		shown_levels.clear();
		selected_level = -1;
	}

	RNG<int> branch_rng;

	void Init() final {
		level_data = GetLevelData();

		branch_rng = { 0, static_cast<int>(level_data["branches"].size()) - 1 };

		for (const auto& l : level_data["levels"]) {
			std::string icon_path = l["ui_icon"];
			std::size_t key{ Hash(icon_path) };
			if (!game.texture.Has(key)) {
				PTGN_ASSERT(FileExists(icon_path), "Could not find icon for level: ", l["id"]);
				game.texture.Load(key, icon_path);
			}
		}

		if (shown_levels.empty()) {
			int first_level = GetValidLevel();

			int second_level = -1;
			if (first_level == -1) {
				difficulty_layer++;
				first_level = GetValidLevel();
				if (first_level == -1) {
					PTGN_LOG("No more levels available! You won them all");
				} else {
					shown_levels.emplace_back(first_level);
					second_level = GetValidLevel();
					shown_levels.emplace_back(second_level);
				}
			} else {
				shown_levels.emplace_back(first_level);
				second_level = GetValidLevel();
				shown_levels.emplace_back(second_level);
			}
		}

		bool was_cleared{ level_buttons.empty() };

		if (was_cleared && shown_levels.size() > 0 && shown_levels[0] != -1) {
			CreateLevelButton(shown_levels[0]);
		}

		if (was_cleared && shown_levels.size() > 1 && shown_levels[1] != -1) {
			CreateLevelButton(shown_levels[1]);
		}

		if (level_buttons.size() == 0) {
			/* win screen? */
		} else if (level_buttons.size() == 1) {
			auto& button{ std::get<1>(level_buttons[0]) };
			auto rect{ button->GetRectangle() };
			rect.pos = game.window.GetCenter() + level_button_offset0;
			button->SetRectangle(rect);
		} else if (level_buttons.size() == 2) {
			auto& button1{ std::get<1>(level_buttons[0]) };
			auto rect1{ button1->GetRectangle() };
			rect1.pos = game.window.GetCenter() + level_button_offset1;
			button1->SetRectangle(rect1);

			auto& button2{ std::get<1>(level_buttons[1]) };
			auto rect2{ button2->GetRectangle() };
			rect2.pos = game.window.GetCenter() + level_button_offset2;
			button2->SetRectangle(rect2);

		} else {
			PTGN_ERROR("Too many level buttons");
		}
		for (int i = 0; i < (int)level_buttons.size(); i++) {
			std::get<1>(level_buttons[i])->SubscribeToMouseEvents();
		}

		buttons.clear();
		buttons.push_back(CreateMenuButton(
			"Confirm", color::White,
			[&]() {
				auto level = selected_level;
				ClearChoices();
				StartGame(level);
			},
			color::Blue, color::Black
		));
		buttons.push_back(CreateMenuButton(
			"Details", color::White,
			[&]() {
				game.scene.RemoveActive(Hash("level_select"));
				auto screen		  = game.scene.Get<TextScreen>(Hash("text_screen"));
				screen->back_name = "level_select";
				PTGN_ASSERT(selected_level != -1);
				screen->text.SetContent(GetDetails(selected_level));
				game.scene.AddActive(Hash("text_screen"));
			},
			color::Gold, color::Black
		));
		buttons.push_back(CreateMenuButton(
			"Back", color::White,
			[&]() {
				game.scene.RemoveActive(Hash("level_select"));
				if (!game.scene.Has(Hash("main_menu"))) {
					LoadMainMenu();
				}
				game.scene.AddActive(Hash("main_menu"));
			},
			color::LightGrey, color::Black
		));

		buttons[0].button->SetRectangle({ V2_int{ 596, 505 }, button_size, Origin::CenterTop });
		buttons[1].button->SetRectangle({ V2_int{ 830, 505 }, button_size, Origin::CenterTop });
		buttons[2].button->SetRectangle({ V2_int{ 820, 636 }, button_size, Origin::TopLeft });

		for (int i = 0; i < (int)buttons.size(); i++) {
			buttons[i].button->SubscribeToMouseEvents();
		}
	}

	void Shutdown() final {
		for (int i = 0; i < (int)buttons.size(); i++) {
			buttons[i].button->UnsubscribeFromMouseEvents();
		}
		for (int i = 0; i < (int)level_buttons.size(); i++) {
			std::get<1>(level_buttons[i])->UnsubscribeFromMouseEvents();
		}
	}

	void Update() final {
		if (selected_level == -1 ||
			(level_buttons.size() == 1 &&
			 std::get<1>(level_buttons[0])->GetTintColor() == color::White) ||
			(level_buttons.size() == 2 &&
			 std::get<1>(level_buttons[0])->GetTintColor() == color::White &&
			 std::get<1>(level_buttons[1])->GetTintColor() == color::White)) {
			buttons[0].button->SetInteractable(false);
			buttons[1].button->SetInteractable(false);
			buttons[0].text.SetContent("Click");
			buttons[1].text.SetContent("Tornado");
		} else {
			buttons[0].button->SetInteractable(true);
			buttons[1].button->SetInteractable(true);
			buttons[0].text.SetContent("Confirm");
			buttons[1].text.SetContent("Details");
		}

		game.renderer.DrawTexture(
			game.texture.Get(Hash("level_select_background")), game.window.GetCenter(), resolution,
			{}, {}, Origin::Center, Flip::None, 0.0f, {}, -1.0f
		);

		for (std::size_t i = 0; i < buttons.size(); i++) {
			auto rect	= buttons[i].button->GetRectangle();
			rect.pos.x += text_x_offset;
			rect.size.x =
				buttons[i]
					.text.GetSize(Hash("menu_font"), std::string(buttons[i].text.GetContent()))
					.x *
				0.5f;
			buttons[i].text.Draw(rect);
		}

		for (std::size_t i = 0; i < level_buttons.size(); i++) {
			std::get<1>(level_buttons[i])->Draw();
		}
	}
};

class MainMenu : public Scene {
public:
	std::vector<TextButton> buttons;

	MainMenu() {
		if (!game.font.Has(Hash("menu_font"))) {
			game.font.Load(Hash("menu_font"), "resources/font/retro_gaming.ttf", button_size.y);
		}
		if (!game.texture.Has(Hash("menu_background"))) {
			game.texture.Load(Hash("menu_background"), "resources/ui/background.png");
		}

		// TODO: Readd.
		// game.music.Load(Hash("background_music"),
		// "resources/sound/background_music.ogg").Play(-1);

		if (!game.scene.Has(Hash("level_select"))) {
			game.scene.Load<LevelSelect>(Hash("level_select"));
		}
		if (!game.scene.Has(Hash("text_screen"))) {
			game.scene.Load<TextScreen>(Hash("text_screen"));
		}
	}

	void Init() final {
		buttons.clear();
		buttons.push_back(CreateMenuButton(
			"Play", color::White,
			[&]() {
				game.scene.RemoveActive(Hash("main_menu"));
				if (game.scene.Has(Hash("level_select"))) {
					game.scene.Get<LevelSelect>(Hash("level_select"))->ClearChoices();
				}
				game.scene.AddActive(Hash("level_select"));
			},
			color::Blue, color::Black
		));
		buttons.push_back(CreateMenuButton(
			"Tutorial", color::White,
			[&]() {
				game.scene.RemoveActive(Hash("main_menu"));
				auto screen		  = game.scene.Get<TextScreen>(Hash("text_screen"));
				screen->back_name = "main_menu";
				screen->text.SetContent(
					"When in level select, click on the storm you wish to chase. "
					"Then click info for "
					"details about the chosen storm. Confirm to start the chase!"
				);
				game.scene.AddActive(Hash("text_screen"));
			},
			color::Blue, color::Black
		));

		/*	buttons[0].button->SetRectangle({ V2_int{ 550, 505 }, button_size, Origin::TopLeft });
			buttons[1].button->SetRectangle({ V2_int{ 780, 505 }, button_size, Origin::TopLeft });*/
		buttons[0].button->SetRectangle({ V2_int{ 560, 505 }, button_size, Origin::TopLeft });
		buttons[1].button->SetRectangle({ V2_int{ 770, 505 }, button_size, Origin::TopLeft });

		for (int i = 0; i < (int)buttons.size(); i++) {
			buttons[i].button->SubscribeToMouseEvents();
		}
		// buttons.push_back(CreateMenuButton(
		//	"Settings", color::White,
		//	[]() {
		//		/*game.scene.RemoveActive(Hash("main_menu"));
		//		game.scene.AddActive(Hash("game"));*/
		//	},
		//	color::Red, color::Black
		//));

		for (int i = 0; i < (int)buttons.size(); i++) {
			buttons[i].button->SubscribeToMouseEvents();
		}
	}

	void Shutdown() final {
		for (int i = 0; i < (int)buttons.size(); i++) {
			buttons[i].button->UnsubscribeFromMouseEvents();
		}
	}

	void Update() final {
		game.renderer.DrawTexture(
			game.texture.Get(Hash("menu_background")), game.window.GetCenter(), resolution, {}, {},
			Origin::Center, Flip::None, 0.0f, {}, -1.0f
		);
		for (std::size_t i = 0; i < buttons.size(); i++) {
			// buttons[i].button->DrawHollow(6.0f);
			auto rect	= buttons[i].button->GetRectangle();
			rect.pos.x += text_x_offset;
			rect.size.x =
				buttons[i]
					.text.GetSize(Hash("menu_font"), std::string(buttons[i].text.GetContent()))
					.x *
				0.5f;
			buttons[i].text.Draw(rect);
		}
		// TODO: Make this a texture and global (perhaps run in the start scene?).
		// Draw Mouse Cursor.
		// game.renderer.DrawCircleFilled(game.input.GetMousePosition(), 5.0f, color::Red);
	}
};

void BackToLevelSelect() {
	if (game.scene.Has(Hash("level_select"))) {
		game.scene.Get<LevelSelect>(Hash("level_select"))->ClearChoices();
	} else {
		game.scene.Load<LevelSelect>(Hash("level_select"));
	}
	game.scene.AddActive(Hash("level_select"));
	game.scene.Unload(Hash("game"));
}

void LoadMainMenu() {
	game.scene.Load<MainMenu>(Hash("main_menu"));
}

class SetupScene : public Scene {
public:
	SetupScene() {}

	void Init() final {
		game.renderer.SetClearColor(color::Silver);
		game.window.SetSize(resolution);

		std::size_t initial_scene{ Hash("main_menu") };
		game.scene.Load<MainMenu>(initial_scene);

		/*std::size_t initial_scene{ Hash("game") };
		game.scene.Load<GameScene>(initial_scene);*/

		game.scene.AddActive(initial_scene);
	}
};

int main() {
	game.Start<SetupScene>();

	return 0;
}
