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
constexpr bool draw_hitboxes{ false };

static void LoadMainMenu();
static int GetCurrentGameLevel();

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
		game.tween.Get(Hash("warning_flash")).Stop();
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

void BackToLevelSelect(int level, bool won);

struct Progress {
	Texture texture;

	std::vector<ecs::Entity> completed_tornadoes;
	std::vector<ecs::Entity> required_tornadoes;

	ecs::Entity current_tornado;

	Progress(const path& ui_texture_path, const std::vector<ecs::Entity>& required_tornadoes) :
		texture{ ui_texture_path }, required_tornadoes{ required_tornadoes } {}

	float progress{ 0.0f };

	bool CompletedAllRequired() const {
		std::vector<ecs::Entity> v1 = completed_tornadoes;
		std::vector<ecs::Entity> v2 = required_tornadoes;

		auto comp = [&](const ecs::Entity& a, const ecs::Entity& b) {
			return a.GetId() < b.GetId();
		};

		// Required in case completion order differs from required order.
		std::sort(v1.begin(), v1.end(), comp);
		std::sort(v2.begin(), v2.end(), comp);
		return v1 == v2;
	}

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

	void FinishCurrentTornado() {
		PTGN_ASSERT(current_tornado.Has<TornadoComponent>());
		completed_tornadoes.push_back(current_tornado);

		// TODO: Remove tornado tint once indicators exist.
		current_tornado.Get<TornadoComponent>().tint = color::DarkGreen;

		current_tornado = ecs::null;
		progress		= 0.0f;

		// PTGN_LOG("Completed tornado with EntityID: ", tornado.GetId());

		if (CompletedAllRequired()) {
			// PTGN_LOG("All required tornadoes completed!");
			BackToLevelSelect(GetCurrentGameLevel(), true);
			return;
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

		const V2_float icon_size{ tornado_icon.GetSize() * 1.5f };

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
			fill_pos, V2_float{ fill_size.x, fill_size.y * progress }, color, Origin::BottomLeft
		);
	}

	void DrawTornadoArrow(const V2_float& player_pos) {
		if (progress <= 0.0f || current_tornado == ecs::null) {
			return;
		}

		PTGN_ASSERT(game.texture.Has(Hash("tornado_arrow")));
		PTGN_ASSERT(game.texture.Has(Hash("tornado_arrow")));

		const Texture tornado_arrow_texture = game.texture.Get(Hash("tornado_arrow"));

		const float scale{ 1.0f };
		V2_float arrow_size{ tornado_arrow_texture.GetSize() * scale };

		PTGN_ASSERT(current_tornado.Has<Transform>());
		PTGN_ASSERT(current_tornado.Has<TornadoComponent>());

		V2_float tornado_center{ current_tornado.Get<Transform>().position };
		TornadoComponent tornado_properties{ current_tornado.Get<TornadoComponent>() };

		V2_float dir{ tornado_center - player_pos };

		float dist{ dir.Magnitude() };

		if (dist >= tornado_properties.data_radius) {
			return;
		}

		PTGN_ASSERT(tornado_properties.data_radius > tornado_properties.escape_radius);
		float range{ tornado_properties.data_radius -
					 (tornado_properties.escape_radius + arrow_size.x) };

		float dist_from_escape{ dist - (tornado_properties.escape_radius + arrow_size.x) };

		float normalized_dist{ dist_from_escape / range };

		if (normalized_dist <= 0.0f) {
			return;
		}

		Color color = Lerp(color::Red, color::Green, normalized_dist);

		const float arrow_pixels_from_player{ 25 };

		V2_float arrow_pos{ player_pos + dir / dist * arrow_pixels_from_player };

		float arrow_rotation{ dir.Angle() };

		game.renderer.DrawTexture(
			tornado_arrow_texture, arrow_pos, arrow_size, {}, {}, Origin::Center, Flip::None,
			arrow_rotation, { 0.5f, 0.5f }, 10.0f, color
		);
	}

	void Draw(const V2_float& player_pos) {
		DrawTornadoProgress();
		DrawTornadoIcons();

		game.renderer.Flush();
		game.camera.SetCameraPrimary();
		DrawTornadoArrow(player_pos);
		game.renderer.Flush();
		game.camera.SetCameraWindow();
	}

	[[nodiscard]] bool CompletedTornado(ecs::Entity tornado) {
		for (const auto& e : completed_tornadoes) {
			if (e == tornado) {
				return true;
			}
		}
		return false;
	}

	void DecrementTornadoProgress(float dt) {
		if (current_tornado == ecs::null) {
			return;
		}
		PTGN_ASSERT(current_tornado.Has<TornadoComponent>());
		TornadoComponent tornado_properties{ current_tornado.Get<TornadoComponent>() };

		// Rate relative to tornado max increment speed that it decrements when outside of
		// radius.
		const float decrement_rate{ 0.5f };

		progress -= tornado_properties.increment_speed * decrement_rate * dt;
		progress  = std::clamp(progress, 0.0f, 1.0f);
	}

	void Update(ecs::Entity tornado, const V2_float& player_pos, float dt) {
		if (tornado == ecs::null) {
			DecrementTornadoProgress(dt);
			return;
		}
		PTGN_ASSERT(!CompletedTornado(tornado));

		if (tornado != current_tornado) {
			Start(tornado);
		}

		PTGN_ASSERT(current_tornado.Has<Transform>());
		PTGN_ASSERT(current_tornado.Has<TornadoComponent>());

		const auto& tornado_center = current_tornado.Get<Transform>().position;

		TornadoComponent tornado_properties{ current_tornado.Get<TornadoComponent>() };

		PTGN_ASSERT(tornado_properties.data_radius != 0.0f);
		PTGN_ASSERT(tornado_properties.escape_radius != 0.0f);

		V2_float dir{ tornado_center - player_pos };
		float dist{ dir.Magnitude() };
		PTGN_ASSERT(dist <= tornado_properties.data_radius);
		PTGN_ASSERT(tornado_properties.data_radius > tornado_properties.escape_radius);
		float range{ tornado_properties.data_radius - tornado_properties.escape_radius };

		if (dist <= tornado_properties.escape_radius) {
			progress = 0.0f;
			return;
		}

		float dist_from_escape{ dist - tornado_properties.escape_radius };

		float normalized_dist{ dist_from_escape / range };
		PTGN_ASSERT(normalized_dist >= 0.0f);
		PTGN_ASSERT(normalized_dist <= 1.0f);

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
			FinishCurrentTornado();
		}
	}
};

class GameScene : public Scene {
public:
	ecs::Manager manager;

	ecs::Entity player;

	const V2_int tile_size{ 16, 16 };
	V2_int grid_size{ resolution / tile_size };

	NoiseProperties noise_properties;
	std::vector<float> noise_map;
	ValueNoise noise;

	std::unordered_set<V2_int> destroyed_tiles;

	std::vector<ecs::Entity> required_tornadoes;

	json level_data;

	int level{ 0 };

	GameScene(int level) : level{ level } {
		PTGN_INFO("Starting level: ", level);

		if (level == 0 && !game.texture.Has(Hash("tutorial_text"))) {
			game.texture.Load(Hash("tutorial_text"), "resources/ui/instructions.png");
		}

		game.texture.Load(Hash("grass"), "resources/entity/grass.png");
		game.texture.Load(Hash("dirt"), "resources/entity/dirt.png");
		game.texture.Load(Hash("corn"), "resources/entity/corn.png");
		game.texture.Load(Hash("house"), "resources/entity/house.png");
		game.texture.Load(Hash("house_destroyed"), "resources/entity/house_destroyed.png");
		game.texture.Load(Hash("tornado_icon"), "resources/ui/tornado_icon.png");
		game.texture.Load(Hash("tornado_icon_green"), "resources/ui/tornado_icon_green.png");
		game.texture.Load(Hash("tornado_arrow"), "resources/ui/arrow.png");
		game.texture.Load(Hash("speedometer"), "resources/ui/speedometer.png");
	}

	~GameScene() {
		if (level == 0) {
			game.texture.Unload(Hash("tutorial_text"));
		}

		game.texture.Unload(Hash("grass"));
		game.texture.Unload(Hash("dirt"));
		game.texture.Unload(Hash("corn"));
		game.texture.Unload(Hash("house"));
		game.texture.Unload(Hash("house_destroyed"));
		game.texture.Unload(Hash("tornado_icon"));
		game.texture.Unload(Hash("tornado_icon_green"));
		game.texture.Unload(Hash("speedometer"));

		// TODO: Unload tornado textures.
	}

	void RestartGame() {
		BackToLevelSelect(level, false);

		/*destroyed_tiles.clear();
		required_tornadoes.clear();
		manager.Reset();
		Init();*/
	}

	Rectangle<float> bounds;

	void Shutdown() final {
		game.tween.Clear();
	}

	float min_zoom{ 1.0f };
	float max_zoom{ 2.0f };
	float zoom_speed{ 0.38f };
	float zoom{ 1.5f };

	void Init() final {
		level_data = GetLevelData().at("levels").at(level);

		V2_int screen_size{ level_data.at("screen_size").at(0),
							level_data.at("screen_size").at(1) };

		grid_size = { screen_size * resolution / tile_size };

		PTGN_INFO("Level size: ", grid_size);

		// PTGN_LOG(level_data.dump(4));

		auto& primary{ camera.GetCurrent() };

		bounds.pos	  = {};
		bounds.size	  = grid_size * tile_size;
		bounds.origin = Origin::TopLeft;

		primary.SetBounds(bounds);
		primary.SetZoom(zoom);

		noise_properties.octaves	 = 2;
		noise_properties.frequency	 = 0.045f;
		noise_properties.bias		 = 1.21f;
		noise_properties.persistence = 0.65f;

		auto& tornadoes = level_data.at("tornadoes");

		PTGN_ASSERT(
			!tornadoes.empty(), "Each level must have at least one tornado specified in the JSON"
		);

		std::size_t tornado_id{ 0 };
		for (const auto& t : tornadoes) {
			CreateTornado(tornado_id, t);
			tornado_id++;
		}

		std::uint32_t seed = level_data.at("seed");

		CreateBackground(seed);

		// player must be created after tornadoes.
		player = CreatePlayer(
			V2_float{ grid_size.x / 2.0f, static_cast<float>(grid_size.y) } * tile_size -
			V2_float{ 0.0f, resolution.y / 2.0f }
		);

		manager.Refresh();
	}

	void Update(float dt) final {
		PlayerInput(dt);

		UpdateTornadoes(dt);

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

		DrawTornadoes();

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

	ecs::Entity CreateTornado(std::size_t tornado_id, const json& tornado_data) {
		ecs::Entity entity = manager.CreateEntity();
		manager.Refresh();

		std::string tornado_path = tornado_data.at("texture");

		std::size_t key{ Hash(tornado_path) };

		if (!game.texture.Has(key)) {
			PTGN_ASSERT(
				FileExists(tornado_path), "Tornado texture: ", tornado_path, " could not be found"
			);
			game.texture.Load(key, tornado_path);
		}

		auto& texture = entity.Add<Texture>(game.texture.Get(key));

		if (tornado_data.contains("static")) {
			auto& static_tornado = tornado_data.at("static");
			auto& tornado_pos	 = static_tornado.at("pos");
			auto& transform		 = entity.Add<Transform>();
			transform.position.x = tornado_pos.at(0);
			transform.position.y = tornado_pos.at(1);
		} else if (tornado_data.contains("sequence")) {
			auto& sequence_tornado = tornado_data.at("sequence");
			PTGN_ASSERT(
				sequence_tornado.size() >= 2,
				"JSON tornado sequence must contain at least two entries"
			);
			auto& tornado_pos		  = sequence_tornado.at(0).at("pos");
			auto& transform			  = entity.Add<Transform>();
			transform.position.x	  = tornado_pos.at(0);
			transform.position.y	  = tornado_pos.at(1);
			std::string sequence_name = "tornado_sequence_" + std::to_string(tornado_id);
			Tween& sequence			  = game.tween.Load(Hash(sequence_name));

			for (std::size_t current{ 0 }; current < sequence_tornado.size(); current++) {
				std::size_t next{ current + 1 };
				if (next >= sequence_tornado.size()) {
					break;
				}

				const auto& data_current = sequence_tornado.at(current);
				const auto& data_next	 = sequence_tornado.at(next);

				V2_float start_pos;
				V2_float end_pos;

				start_pos.x = data_current.at("pos").at(0);
				start_pos.y = data_current.at("pos").at(1);

				end_pos.x = data_next.at("pos").at(0);
				end_pos.y = data_next.at("pos").at(1);

				int time_to_next_ms = data_current.at("time_to_next");
				milliseconds time_to_next{ time_to_next_ms };

				sequence.During(time_to_next).OnUpdate([=](float progress) mutable {
					auto& transform	   = entity.Get<Transform>();
					transform.position = Lerp(start_pos, end_pos, progress);
				});
			}
			sequence.Start();
		}

		if (tornado_data.contains("custom") && tornado_data.at("custom")) {
			/* TODO: custom stuff with tornado */
		}

		PTGN_ASSERT(
			entity.Has<Transform>(), "Failed to create tornado position from given JSON data"
		);

		float turn_speed	  = tornado_data.at("turn_speed");
		float increment_speed = tornado_data.at("increment_speed");
		float escape_radius	  = tornado_data.at("escape_radius");
		float data_radius	  = tornado_data.at("data_radius");
		float gravity_radius  = tornado_data.at("gravity_radius");
		float warning_radius  = tornado_data.at("warning_radius");

		V2_float texture_size{ texture.GetSize() };

		auto& size = entity.Add<Size>(escape_radius * texture_size);

		auto& tornado = entity.Add<TornadoComponent>();

		float width{ texture_size.x / 2.0f };

		tornado.turn_speed		= turn_speed;
		tornado.increment_speed = increment_speed;
		tornado.escape_radius	= escape_radius * width;
		tornado.data_radius		= data_radius * width;
		tornado.gravity_radius	= gravity_radius * width;
		tornado.warning_radius	= warning_radius * width;

		auto& rigid_body = entity.Add<RigidBody>();
		// rigid_body.max_velocity = 137.0f;

		PTGN_ASSERT(tornado.warning_radius > tornado.escape_radius);
		PTGN_ASSERT(tornado.data_radius > tornado.escape_radius);
		PTGN_ASSERT(tornado.gravity_radius >= tornado.data_radius);

		required_tornadoes.push_back(entity);

		return entity;
	}

	void CreateBackground(std::uint32_t seed) {
		noise	  = { 256, seed };
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
		bool q{ game.input.KeyPressed(Key::Q) };
		bool e{ game.input.KeyPressed(Key::E) };

		auto& primary{ camera.GetCurrent() };

		if (q) {
			zoom += zoom_speed * dt;
			zoom  = std::clamp(zoom, min_zoom, max_zoom);
			primary.SetZoom(zoom);
		}
		if (e) {
			zoom -= zoom_speed * dt;
			zoom  = std::clamp(zoom, min_zoom, max_zoom);
			primary.SetZoom(zoom);
		}

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

	// May return ecs::null.
	ecs::Entity GetClosestTornado(const std::vector<ecs::Entity>& data_tornadoes) {
		PTGN_ASSERT(!data_tornadoes.empty());
		PTGN_ASSERT(player.Has<Progress>());
		PTGN_ASSERT(player.Has<Transform>());
		float closest_tornado2{ std::numeric_limits<float>::max() };
		ecs::Entity closest_tornado{ ecs::null };
		const auto& player_center = player.Get<Transform>().position;
		for (const ecs::Entity& t : data_tornadoes) {
			// Ignore completed tornadoes.
			if (player.Get<Progress>().CompletedTornado(t) || !t.Has<Transform>()) {
				continue;
			}
			const auto& tornado_center = t.Get<Transform>().position;
			float dist2new{ (player_center - tornado_center).MagnitudeSquared() };
			if (dist2new < closest_tornado2) {
				closest_tornado2 = dist2new;
				closest_tornado	 = t;
			}
		}
		return closest_tornado;
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

		std::vector<ecs::Entity> data_tornadoes;

		for (auto [e, tornado, transform, rigid_body] : tornadoes) {
			if (!game.collision.overlap.PointCircle(
					player_transform.position, { transform.position, tornado.gravity_radius }
				)) {
				continue;
			}

			V2_float dir{ transform.position - player_transform.position };
			V2_float wind_speed{ tornado.GetWind(dir, player_aero.pull_resistance) * dt };

			// Apply tornado effects to player.

			player_rigid_body.velocity	   += wind_speed;
			player_transform.rotation	   += wind_speed.Magnitude() / player_vehicle.inertia;
			player_rigid_body.acceleration += tornado.GetSuction(dir, player_max_thrust);
			player_rigid_body.velocity	   += rigid_body.velocity * dt;

			if (!game.collision.overlap.PointCircle(
					player_transform.position, { transform.position, tornado.data_radius }
				)) {
				// Not collecting data.
				continue;
			}

			data_tornadoes.push_back(e);

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

		if (data_tornadoes.size() > 0) {
			auto closest_tornado = GetClosestTornado(data_tornadoes);
			player.Get<Progress>().Update(closest_tornado, player_transform.position, dt);
		} else {
			player.Get<Progress>().DecrementTornadoProgress(dt);
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

	void UpdateTornadoes(float dt) {
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

	void DrawTornadoes() {
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

		V2_float margin{ 4.0f, 0.0f };
		V2_float meter_size{ texture.GetSize() };
		V2_float meter_pos{ resolution - meter_size / 2.0f - margin };

		PTGN_ASSERT(player.Has<VehicleComponent>());

		VehicleComponent vehicle{ player.Get<VehicleComponent>() };

		float fraction{ vehicle.throttle };

		// TODO: Consider not clamping this to show negative velocity.
		fraction = std::clamp(fraction, 0.0f, 1.0f);

		const float speedometer_radius{ 58.0f };
		const float start_angle{ DegToRad(63.0f + 51.0f) };
		const float angle_range{ DegToRad(360.0f) - start_angle + DegToRad(63.0f) };
		const float end_angle{ start_angle + DegToRad(1.0f) };

		float red_amount = Lerp(0.0f, angle_range, fraction);
		float green_amount =
			Lerp(0.0f, angle_range * (3.0f / 7.0f), std::min(1.0f, fraction / (3.0f / 7.0f)));
		float yellow_amount =
			Lerp(0.0f, angle_range * (5.2f / 7.0f), std::min(1.0f, fraction / (5.2f / 7.0f)));

		game.renderer.DrawArcFilled(
			meter_pos, speedometer_radius, start_angle, end_angle + red_amount, false, color::Red
		);

		game.renderer.DrawArcFilled(
			meter_pos, speedometer_radius, start_angle, end_angle + yellow_amount, false,
			color::Gold
		);

		game.renderer.DrawArcFilled(
			meter_pos, speedometer_radius, start_angle, end_angle + green_amount, false, color::Lime
		);

		game.renderer.DrawTexture(
			texture, meter_pos, meter_size, {}, {}, Origin::Center, Flip::None, 0.0f, {}, 1.0f
		);
	}

	void DrawUI() {
		game.renderer.Flush();
		game.camera.SetCameraWindow();

		// Draw UI here...

		PTGN_ASSERT(player.Has<Progress>());
		PTGN_ASSERT(player.Has<Transform>());
		player.Get<Progress>().Draw(player.Get<Transform>().position);
		DrawSpeedometer();
		if (level == 0) {
			const Texture tutorial_text{ game.texture.Get(Hash("tutorial_text")) };
			const V2_float text_size{ tutorial_text.GetSize() };
			const V2_float text_pos{ static_cast<float>(resolution.x), 0.0f };
			game.renderer.DrawTexture(tutorial_text, text_pos, text_size, {}, {}, Origin::TopRight);
		}

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
	const std::string& content_enabled, Color text_color, const ButtonActivateFunction& f,
	const Color& color, Color hover_color, const std::string& content_disabled = ""
) {
	ColorButton b;
	Text text{ Hash("menu_font"), content_enabled, text_color };
	b.SetOnHover(
		[=]() mutable { text.SetColor(hover_color); }, [=]() mutable { text.SetColor(text_color); }
	);
	b.SetOnEnable([=]() mutable {
		text.SetColor(text_color);
		text.SetContent(content_enabled);
	});
	b.SetOnDisable([=]() mutable {
		text.SetColor(color::Black);
		if (content_disabled != "") {
			text.SetContent(content_disabled);
		}
	});
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
			"Back", color::Silver,
			[&]() {
				game.scene.RemoveActive(Hash("text_screen"));
				if (!game.scene.Has(Hash(back_name))) {
					LoadMainMenu();
				}
				PTGN_ASSERT(game.scene.Has(Hash(back_name)));
				game.scene.AddActive(Hash(back_name));
			},
			color::Transparent, color::Black
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

	bool CompletedLevel(int level) const {
		return completed_levels.count(level) > 0;
	}

	json GetLevel(int level) const {
		for (const auto& l : level_data.at("levels")) {
			if (l.at("id") == level) {
				return l;
			}
		}
		PTGN_ERROR("Failed to find level in json");
	}

	std::string GetDetails(int level) const {
		PTGN_ASSERT(level != -1);
		auto l = GetLevel(level);
		return l.at("details");
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

		auto l				  = GetLevel(level);
		std::string icon_name = l.at("ui_icon");
		std::size_t key		  = Hash(icon_name);

		PTGN_ASSERT(game.texture.Has(key));

		Texture texture = game.texture.Get(key);
		rect.pos		= game.window.GetCenter();
		rect.size		= texture.GetSize();
		rect.origin		= Origin::Center;

		auto button = std::make_shared<TexturedToggleButton>(
			rect, std::vector<TextureOrKey>{ texture, texture }
		);

		Color tornado_select_color = color::Black;
		Color tornado_hover_color  = color::Grey;

		button->SetOnActivate([this, button, level, tornado_select_color]() {
			selected_level = level;
			PTGN_INFO("Selected level: ", level);
			button->SetTintColor(tornado_select_color);
			ToggleOtherLevel();
		});
		button->SetOnHover(
			[=]() {
				if (button->GetTintColor() == color::White) {
					button->SetTintColor(tornado_hover_color);
					// PTGN_INFO("Hover started on button for level: ", level);
				}
			},
			[=]() {
				if (button->GetTintColor() == tornado_hover_color) {
					button->SetTintColor(color::White);
					// PTGN_INFO("Hover stopped on button for level: ", level);
				}
			}
		);
		level_buttons.emplace_back(level, button);
	}

	int selected_level{ -1 };

	V2_float level_button_offset0{ 0, -100 };
	V2_float level_button_offset1{ -100, -170 };
	V2_float level_button_offset2{ +120, -50 };

	void AddCompletedLevel(int level) {
		completed_levels.emplace(level);
	}

	void ClearChoices() {
		for (int i = 0; i < (int)level_buttons.size(); i++) {
			std::get<1>(level_buttons[i])->UnsubscribeFromMouseEvents();
		}

		level_buttons.clear();
		selected_level = -1;
	}

	std::set<int> GetPotentialLevels() {
		std::set<int> potential_levels;

		for (auto& b : level_data.at("branches")) {
			if (difficulty_layer >= b.size()) {
				continue;
			}
			int potential_level = b[difficulty_layer];
			if (CompletedLevel(potential_level)) {
				continue;
			}
			potential_levels.emplace(potential_level);
		}
		while (potential_levels.size() > 2) {
			RNG<std::size_t> removal{ 0, potential_levels.size() - 1 };
			auto it = potential_levels.begin();
			std::advance(it, removal());
			potential_levels.erase(it);
		}

		return potential_levels;
	}

	std::size_t playing_music_key{ 0 };

	void Init() final {
		level_data = GetLevelData();

		std::size_t furthest_branch = 0;

		for (const auto& b : level_data.at("branches")) {
			for (std::size_t i = 0; i < b.size(); i++) {
				furthest_branch = std::max(i, furthest_branch);
			}
		}

		auto& difficulties = level_data.at("difficulty_layers");

		PTGN_ASSERT(difficulties.size() == furthest_branch + 1);

		PTGN_ASSERT(!difficulties.empty());

		if (!game.music.IsPlaying()) {
			std::string music_path = difficulties.at(0).at("music");
			std::size_t music_key  = Hash(music_path);
			playing_music_key	   = music_key;
			if (!game.music.Has(music_key)) {
				game.music.Load(music_key, music_path);
			}
			game.music.Get(music_key).Play(-1);
		}

		for (int l : level_data.at("completed_levels")) {
			completed_levels.emplace(l);
		}

		for (const auto& l : level_data.at("levels")) {
			std::string icon_path = l.at("ui_icon");
			std::size_t key{ Hash(icon_path) };
			if (!game.texture.Has(key)) {
				PTGN_ASSERT(FileExists(icon_path), "Could not find icon for level: ", l.at("id"));
				game.texture.Load(key, icon_path);
			}
		}

		bool was_cleared{ level_buttons.empty() };

		if (was_cleared) {
			std::set<int> potential_levels;

			while (difficulty_layer <= furthest_branch) {
				potential_levels = GetPotentialLevels();
				if (!potential_levels.empty()) {
					break;
				}
				difficulty_layer++;
			}

			for (int l : potential_levels) {
				CreateLevelButton(l);
			}
		}

		PTGN_ASSERT(
			difficulty_layer < difficulties.size(),
			"Difficulty layer exceeded those specified in JSON"
		);

		std::string music_path = difficulties.at(difficulty_layer).at("music");
		std::size_t music_key  = Hash(music_path);

		if (playing_music_key != music_key) {
			game.music.Stop();
			playing_music_key = music_key;
			if (!game.music.Has(music_key)) {
				game.music.Load(music_key, music_path);
			}
			game.music.Get(music_key).Play(-1);
		}

		if (level_buttons.empty()) {
			PTGN_INFO("You won! No levels available");
			// TODO: Add restart game button?
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
			"Chase", color::Green,
			[&]() {
				auto level = selected_level;
				ClearChoices();
				StartGame(level);
			},
			color::Transparent, color::Black, "Click"
		));
		buttons.push_back(CreateMenuButton(
			"Details", color::Gold,
			[&]() {
				game.scene.RemoveActive(Hash("level_select"));
				auto screen		  = game.scene.Get<TextScreen>(Hash("text_screen"));
				screen->back_name = "level_select";
				PTGN_ASSERT(selected_level != -1);
				screen->text.SetContent(GetDetails(selected_level));
				game.scene.AddActive(Hash("text_screen"));
			},
			color::Transparent, color::Black, "Tornado"
		));
		buttons.push_back(CreateMenuButton(
			"Back", color::Silver,
			[&]() {
				game.scene.RemoveActive(Hash("level_select"));
				if (!game.scene.Has(Hash("main_menu"))) {
					LoadMainMenu();
				}
				game.scene.AddActive(Hash("main_menu"));
			},
			color::Transparent, color::Black
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
		} else {
			buttons[0].button->SetInteractable(true);
			buttons[1].button->SetInteractable(true);
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
			"Play", color::Cyan,
			[&]() {
				game.scene.RemoveActive(Hash("main_menu"));
				if (game.scene.Has(Hash("level_select"))) {
					game.scene.Get<LevelSelect>(Hash("level_select"))->ClearChoices();
				}
				game.scene.AddActive(Hash("level_select"));
			},
			color::Transparent, color::Black
		));
		buttons.push_back(CreateMenuButton(
			"Tutorial", color::Gold,
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
			color::Transparent, color::Black
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

void BackToLevelSelect(int level, bool won) {
	if (game.scene.Has(Hash("level_select"))) {
		if (won) {
			auto level_select{ game.scene.Get<LevelSelect>(Hash("level_select")) };
			level_select->AddCompletedLevel(level);
			level_select->ClearChoices();
		} else {
			/* player lost, do something on level select to reflect this? */
		}
	} else {
		game.scene.Load<LevelSelect>(Hash("level_select"));
	}
	game.scene.AddActive(Hash("level_select"));
	game.scene.Unload(Hash("game"));
}

void LoadMainMenu() {
	game.scene.Load<MainMenu>(Hash("main_menu"));
}

int GetCurrentGameLevel() {
	PTGN_ASSERT(game.scene.Has(Hash("game")), "Could not find game scene");
	return game.scene.Get<GameScene>(Hash("game"))->level;
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
