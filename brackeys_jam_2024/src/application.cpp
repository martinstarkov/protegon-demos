#include <unordered_set>

#include "protegon/protegon.h"

using namespace ptgn;

constexpr const V2_int resolution{ 1440, 810 };
constexpr const V2_int center{ resolution / 2 };
constexpr const bool draw_hitboxes{ true };

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

	if (noise_value >= 0.0f && noise_value <= 0.5f) {
		return TileType::Corn;
	} else if (noise_value > 0.59f && noise_value < 0.6f) {
		return TileType::House;
	} else {
		return TileType::Grass;
	}
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
	float forward_thrust{ 0.0f };
	float backward_thrust{ 0.0f };
	float turn_speed{ 0.0f };

	float inertia{ 0.0f };

	V2_float
		prev_acceleration; // acceleration in the current frame (cached even after it is cleared).

	Texture vehicle_texture;
	Texture wheel_texture;

	float wheel_rotation{ 0.0f };
};

struct TintColor : public Color {
	using Color::Color;

	TintColor(const Color& color) : Color{ color } {}
};

struct Warning {
	void Init(ecs::Entity player) {
		game.tween.Load(Hash("warning_flash"))
			.During(milliseconds{ 500 })
			.Repeat(-1)
			.OnRepeat([=](Tween& tween) mutable {
				if (tween.GetRepeats() % 2 == 0) {
					player.Add<TintColor>() = color::Red;
				} else {
					player.Remove<TintColor>();
				}
			})
			.OnStop([=]() mutable { player.Remove<TintColor>(); })
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
		float particle_drag{ 0.99f };

		Circle<float> inner_deletion_circle{ tornado_pos, escape_radius * 0.1f };
		Circle<float> outer_deletion_circle{ tornado_pos, gravity_radius };

		auto particles = particle_manager.EntitiesWith<Transform, RigidBody>();
		for (auto [e, transform, rigid_body] : particles) {
			/*if (lifetime.Elapsed()) {
				e.Destroy();
				continue;
			}*/

			V2_float dir{ tornado_pos - transform.position };

			rigid_body.velocity += GetSuction(dir, 200.0f) * dt;
			rigid_body.velocity += GetWind(dir, particle_pull_resistance) * dt;
			rigid_body.velocity *= particle_drag;
			transform.position	+= rigid_body.velocity * dt;
			transform.rotation	+= turn_speed * dt;

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

	std::vector<ecs::Entity> completed_tornados;
	std::vector<ecs::Entity> required_tornados;

	ecs::Entity current_tornado;

	Progress(const path& ui_texture_path, const std::vector<ecs::Entity>& required_tornados) :
		texture{ ui_texture_path }, required_tornados{ required_tornados } {}

	float progress{ 0.0f };

	constexpr static V2_float meter_pos{
		25, 258
	}; // center bottom screen position of the tornado progress indicator.

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
		completed_tornados.push_back(current_tornado);

		// TODO: Remove tornado tint once indicators exist.
		current_tornado.Get<TornadoComponent>().tint = color::Green;

		current_tornado = ecs::null;
		progress		= 0.0f;

		// PTGN_LOG("Completed tornado with EntityID: ", tornado.GetId());

		if (completed_tornados == required_tornados) {
			// PTGN_LOG("All required tornados completed!");
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

		float total_width{ icon_size.x * required_tornados.size() +
						   (required_tornados.size() - 1) * icon_x_offset };

		V2_float start_pos{ center.x - total_width / 2.0f, icon_y_offset };

		for (int i = 0; i < required_tornados.size(); ++i) {
			auto tornado{ required_tornados[i] };
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

		V2_float meter_size{ texture.GetSize() };

		Color color = Lerp(color::Grey, color::Green, progress);

		V2_float border_size{ 4, 4 };

		V2_float fill_size{ meter_size - border_size * 2.0f };

		V2_float fill_pos{ meter_pos.x, meter_pos.y - border_size.y };

		game.renderer.DrawTexture(texture, meter_pos, meter_size, {}, {}, Origin::CenterBottom);

		game.renderer.DrawRectangleFilled(
			fill_pos, { fill_size.x, fill_size.y * progress }, color, Origin::CenterBottom
		);
	}

	void Draw() {
		DrawTornadoProgress();
		DrawTornadoIcons();
	}

	[[nodiscard]] bool CompletedTornado(ecs::Entity tornado) {
		for (const auto& e : completed_tornados) {
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

	float scale{ 4.0f };

	const V2_float tile_size{ 32, 32 };
	const V2_int grid_size{ 300, 300 };

	NoiseProperties noise_properties;
	std::vector<float> noise_map;
	const ValueNoise noise{ 256, 0 };

	std::unordered_set<V2_int> destroyed_tiles;

	std::vector<ecs::Entity> required_tornados;

	GameScene() {}

	void RestartGame() {
		BackToLevelSelect();

		/*destroyed_tiles.clear();
		required_tornados.clear();
		manager.Reset();
		Init();*/
	}

	void Init() final {
		/*noise_properties.octaves	 = 6;
		noise_properties.frequency	 = 0.01f;
		noise_properties.bias		 = 1.2f;
		noise_properties.persistence = 0.75f;*/

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

		// player must be created after tornados.
		player = CreatePlayer();

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

	ecs::Entity CreatePlayer() {
		ecs::Entity entity = manager.CreateEntity();

		Texture vehicle_texture{ "resources/entity/car.png" };
		Texture wheel_texture{ "resources/entity/wheels.png" };

		entity.Add<Size>(vehicle_texture.GetSize());
		PTGN_ASSERT(required_tornados.size() != 0);
		entity.Add<Progress>("resources/ui/tornadometer.png", required_tornados);

		auto& transform	   = entity.Add<Transform>();
		transform.position = center;

		auto& rigid_body		= entity.Add<RigidBody>();
		rigid_body.max_velocity = 125.0f;

		auto& vehicle			= entity.Add<VehicleComponent>();
		vehicle.forward_thrust	= 1000.0f;
		vehicle.backward_thrust = 0.6f * vehicle.forward_thrust;
		vehicle.turn_speed		= 5.0f;
		vehicle.inertia			= 200.0f;
		vehicle.vehicle_texture = vehicle_texture;
		vehicle.wheel_texture	= wheel_texture;

		auto& aero			 = entity.Add<Aerodynamics>();
		aero.pull_resistance = 1.0f;

		return entity;
	}

	ecs::Entity CreateTornado(const V2_float& position, float turn_speed) {
		ecs::Entity entity = manager.CreateEntity();
		auto& texture	   = entity.Add<Texture>(Texture{ "resources/entity/tornado.png" });

		auto& transform	   = entity.Add<Transform>();
		transform.position = position;

		auto& size = entity.Add<Size>(texture.GetSize() * scale);

		auto& tornado = entity.Add<TornadoComponent>();

		tornado.turn_speed		= turn_speed;
		tornado.escape_radius	= size.x / 2.0f;
		tornado.data_radius		= 4.0f * tornado.escape_radius;
		tornado.gravity_radius	= 8.0f * tornado.escape_radius;
		tornado.warning_radius	= 3.0f * tornado.escape_radius;
		tornado.increment_speed = 0.3f;

		auto& rigid_body		= entity.Add<RigidBody>();
		rigid_body.max_velocity = 137.0f;

		PTGN_ASSERT(tornado.warning_radius > tornado.escape_radius);
		PTGN_ASSERT(tornado.data_radius > tornado.escape_radius);
		PTGN_ASSERT(tornado.gravity_radius >= tornado.data_radius);

		required_tornados.push_back(entity);

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

		if (up) {
			transform.rotation = direction;
			thrust			   = unit_direction * vehicle.forward_thrust;
		} else if (down) {
			transform.rotation =
				transform.rotation - vehicle.turn_speed * vehicle.wheel_rotation * dt;
			thrust = unit_direction * -vehicle.backward_thrust;
		}

		rigid_body.acceleration += thrust;
	}

	void PlayerPhysics(float dt) {
		PTGN_ASSERT(player.Has<RigidBody>());
		PTGN_ASSERT(player.Has<Transform>());
		PTGN_ASSERT(player.Has<VehicleComponent>());

		auto& rigid_body = player.Get<RigidBody>();
		auto& transform	 = player.Get<Transform>();

		const float drag{ 0.8f };

		rigid_body.velocity += rigid_body.acceleration * dt;

		rigid_body.velocity *= drag;

		rigid_body.velocity =
			Clamp(rigid_body.velocity, -rigid_body.max_velocity, rigid_body.max_velocity);

		// Zeros velocity when below a certain magnitude.
		/*float vel_mag2{ rigid_body.velocity.MagnitudeSquared() };

		constexpr float velocity_zeroing_threshold{ 1.0f };

		if (vel_mag2 < velocity_zeroing_threshold) {
			rigid_body.velocity = {};
		}*/

		transform.position += rigid_body.velocity * dt;

		// Center camera on player.
		auto& primary{ camera.GetPrimary() };
		primary.SetPosition(transform.position);

		V2_int player_tile = transform.position / tile_size;

		TileType tile_type = GetTileType(GetNoiseValue(player_tile));

		if (tile_type == TileType::Corn) {
			destroyed_tiles.insert(player_tile);
		}

		player.Get<VehicleComponent>().prev_acceleration = rigid_body.acceleration;

		rigid_body.acceleration = {};
	}

	void UpdateTornadoGravity(float dt) {
		auto tornados = manager.EntitiesWith<TornadoComponent, Transform, RigidBody>();

		PTGN_ASSERT(player.Has<Transform>());
		PTGN_ASSERT(player.Has<RigidBody>());
		PTGN_ASSERT(player.Has<Aerodynamics>());
		PTGN_ASSERT(player.Has<VehicleComponent>());
		PTGN_ASSERT(player.Has<Progress>());

		auto& player_transform{ player.Get<Transform>() };
		VehicleComponent player_vehicle{ player.Get<VehicleComponent>() };

		float player_max_thrust{
			std::max(player_vehicle.backward_thrust, player_vehicle.forward_thrust)
		};

		auto& player_rigid_body{ player.Get<RigidBody>() };
		const auto& player_aero{ player.Get<Aerodynamics>() };

		for (auto [e, tornado, transform, rigid_body] : tornados) {
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
				player.Get<Progress>().Stop(e);
				continue;
			}

			player.Get<Progress>().Update(
				e, player_transform.position, transform.position, tornado.data_radius,
				tornado.escape_radius, dt
			);

			if (game.collision.overlap.PointCircle(
					player_transform.position, { transform.position, tornado.warning_radius }
				)) {
				if (!player.Has<Warning>()) {
					player.Add<Warning>().Init(player);
				}
			} else {
				if (player.Has<Warning>()) {
					player.Get<Warning>().Shutdown();
					player.Remove<Warning>();
				}
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
					player.Get<RigidBody>().acceleration.x = player_vehicle.forward_thrust;
					player.Get<Transform>().rotation += 10.0f * player_vehicle.turn_speed * dt;
				})
				.Start();
		}
	}

	void UpdateTornados(float dt) {
		TornadoMotion(dt);
		UpdateTornadoGravity(dt);
	}

	void TornadoMotion(float dt) {
		auto tornados = manager.EntitiesWith<TornadoComponent, Transform, RigidBody>();

		const float tornado_move_speed{ 1000.0f };

		for (auto [e, tornado, transform, rigid_body] : tornados) {
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
		auto size{ player.Get<Size>() };

		Color tint{ player.Has<TintColor>() ? player.Get<TintColor>() : color::White };

		V2_float relative_wheel_pos =
			V2_float{ (25 - 15), 0.0f }.Rotated(player_transform.rotation);

		game.renderer.DrawTexture(
			vehicle.wheel_texture, player_transform.position + relative_wheel_pos,
			vehicle.wheel_texture.GetSize(), {}, {}, Origin::Center, Flip::None,
			player_transform.rotation + vehicle.wheel_rotation, { 0.5f, 0.5f }, 0.0f, tint
		);

		game.renderer.DrawTexture(
			vehicle.vehicle_texture, player_transform.position, size, {}, {}, Origin::Center,
			Flip::None, player_transform.rotation, { 0.5f, 0.5f }, 0.0f, tint

		);
	}

	void DrawTornados() {
		auto tornados = manager.EntitiesWith<TornadoComponent, Texture, Transform, Size>();

		for (auto [e, tornado, texture, transform, size] : tornados) {
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
		auto& primary{ camera.GetPrimary() };
		Rectangle<float> camera_rect{ primary.GetRectangle() };

		// game.renderer.DrawRectangleHollow(camera_rect, color::Blue, 3.0f);

		Rectangle<float> tile_rect{ {}, tile_size, Origin::TopLeft };

		for (int i{ 0 }; i < grid_size.x; i++) {
			for (int j{ 0 }; j < grid_size.y; j++) {
				V2_int tile{ i, j };

				tile_rect.pos = tile * tile_size;

				// Expand size of each tile to include neighbors to prevent edges from flashing
				// when camera moves. Skip grid tiles not within camera view.
				if (!game.collision.overlap.RectangleRectangle(tile_rect, camera_rect)) {
					continue;
				}

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

		V2_float meter_pos{ resolution - V2_float{ 10, 10 } };
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

		float fraction{ forward_velocity / rigid_body.max_velocity };
		// float fraction{ forward_accel / vehicle.forward_thrust };

		// TODO: Consider not clamping this to show negative velocity.
		fraction = std::clamp(fraction, 0.0f, 1.0f);

		Color fill_color = Lerp(color::Red, color::Green, fraction);

		V2_float border_size{ 4, 4 };

		V2_float fill_size{ meter_size - border_size * 2.0f };

		V2_float fill_pos{ meter_pos.x - border_size.x - fill_size.x,
						   meter_pos.y - border_size.y - fill_size.y };

		Color meter_background = color::White;

		meter_background.a = 128;
		fill_color.a	   = 200;

		game.renderer.DrawRectangleFilled(
			fill_pos, { fill_size.x, fill_size.y }, meter_background, Origin::TopLeft
		);

		game.renderer.DrawRectangleFilled(
			fill_pos, { fill_size.x * fraction, fill_size.y }, fill_color, Origin::TopLeft
		);

		game.renderer.DrawTexture(texture, meter_pos, meter_size, {}, {}, Origin::BottomRight);
	}

	void DrawUI() {
		auto prev_primary = game.camera.GetPrimary();

		game.renderer.Flush();

		OrthographicCamera c;
		c.SetPosition(game.window.GetCenter());
		c.SetSizeToWindow();
		c.SetClampBounds({});
		game.camera.SetPrimary(c);

		// Draw UI here...

		PTGN_ASSERT(player.Has<Progress>());
		player.Get<Progress>().Draw();
		DrawSpeedometer();

		game.renderer.Flush();

		if (game.camera.GetPrimary() == c) {
			game.camera.SetPrimary(prev_primary);
		}
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

class LevelSelect : public Scene {
public:
	std::vector<TextButton> buttons;

	LevelSelect() {
		if (!game.font.Has(Hash("menu_font"))) {
			game.font.Load(Hash("menu_font"), "resources/font/retro_gaming.ttf", button_size.y);
		}
		if (!game.texture.Has(Hash("level_select_background"))) {
			game.texture.Load(Hash("level_select_background"), "resources/ui/laptop.png");
		}
	}

	void StartGame() {
		game.scene.RemoveActive(Hash("level_select"));
		game.scene.Load<GameScene>(Hash("game"));
		game.scene.SetActive(Hash("game"));
	}

	OrthographicCamera camera;

	void Init() final {
		camera.SetSizeToWindow();
		camera.SetPosition(game.window.GetCenter());
		game.camera.SetPrimary(camera);

		buttons.clear();
		buttons.push_back(CreateMenuButton(
			"Confirm", color::White, [&]() { StartGame(); }, color::Blue, color::Black
		));
		buttons.push_back(CreateMenuButton(
			"Info", color::White, [&]() { StartGame(); }, color::Gold, color::Black
		));
		buttons.push_back(CreateMenuButton(
			"Back", color::White,
			[&]() {
				game.scene.RemoveActive(Hash("level_select"));
				if (!game.scene.Has(Hash("main_menu"))) {
					LoadMainMenu();
				}
				game.scene.SetActive(Hash("main_menu"));
			},
			color::LightGrey, color::Black
		));

		buttons[0].button->SetRectangle({ V2_int{ 536, 505 }, button_size, Origin::TopLeft });
		buttons[1].button->SetRectangle({ V2_int{ 790, 505 }, button_size, Origin::TopLeft });
		buttons[2].button->SetRectangle({ V2_int{ 820, 636 }, button_size, Origin::TopLeft });

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
		game.scene.Get(Hash("level_select"))->camera.SetPrimary(camera);

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
	}

private:
	void LoadMainMenu();
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
	}

	void Init() final {
		buttons.clear();
		buttons.push_back(CreateMenuButton(
			"Play", color::White,
			[&]() {
				game.scene.RemoveActive(Hash("main_menu"));
				game.scene.SetActive(Hash("level_select"));
			},
			color::Blue, color::Black
		));
		/*buttons.push_back(CreateMenuButton(
			"Settings", color::White,
			[&]() {
				game.scene.RemoveActive(Hash("main_menu"));
				game.scene.SetActive(Hash("level_select"));
			},
			color::Gold, color::Black
		));*/

		/*	buttons[0].button->SetRectangle({ V2_int{ 550, 505 }, button_size, Origin::TopLeft });
			buttons[1].button->SetRectangle({ V2_int{ 780, 505 }, button_size, Origin::TopLeft });*/
		buttons[0].button->SetRectangle({ V2_int{ 820, 636 }, button_size, Origin::TopLeft });

		for (int i = 0; i < (int)buttons.size(); i++) {
			buttons[i].button->SubscribeToMouseEvents();
		}
		// buttons.push_back(CreateMenuButton(
		//	"Settings", color::White,
		//	[]() {
		//		/*game.scene.RemoveActive(Hash("main_menu"));
		//		game.scene.SetActive(Hash("game"));*/
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
	game.scene.Load<LevelSelect>(Hash("level_select"));
	game.scene.RemoveActive(Hash("game"));
	game.scene.SetActive(Hash("level_select"));
	game.scene.Unload(Hash("game"));
}

void LevelSelect::LoadMainMenu() {
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
		game.scene.SetActive(initial_scene);
	}
};

int main() {
	game.Start<SetupScene>();
	return 0;
}
