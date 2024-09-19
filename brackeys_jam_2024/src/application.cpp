#include <set>
#include <unordered_set>

#include "protegon/protegon.h"

using namespace ptgn;

const path level_json = "resources/data/levels.json";

std::ifstream json_file_open(level_json);
json json_level_data = json::parse(json_file_open);

constexpr V2_int resolution{ 1440, 810 };
constexpr V2_int center{ resolution / 2 };
constexpr bool draw_hitboxes{ false };

const auto& volume_sliders = json_level_data.at("volume");

float car_volume_frac{ volume_sliders.at("car") };
float music_volume_frac{ volume_sliders.at("music") };
float tornadoes_max_volume_frac{ volume_sliders.at("tornadoes_max") };
float tornadoes_ambient_volume_frac{ volume_sliders.at("tornadoes_ambient") };

float car_volume_frac_clamped				= std::clamp(car_volume_frac, 0.0f, 1.0f);
float music_volume_frac_clamped				= std::clamp(music_volume_frac, 0.0f, 1.0f);
float tornadoes_max_volume_frac_clamped		= std::clamp(tornadoes_max_volume_frac, 0.0f, 1.0f);
float tornadoes_ambient_volume_frac_clamped = std::clamp(tornadoes_ambient_volume_frac, 0.0f, 1.0f);

int car_volume{ static_cast<int>(128.0f * car_volume_frac_clamped) };
int music_volume{ static_cast<int>(128.0f * music_volume_frac_clamped) };
int min_tornado_volume{ static_cast<int>(128.0f * tornadoes_ambient_volume_frac_clamped) };
int max_tornado_volume{ static_cast<int>(128.0f * tornadoes_max_volume_frac_clamped) };

static void LoadMainMenu();
static int GetCurrentGameLevel();

enum class TileType {
	TallGrass,
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
		case TileType::TallGrass:	   return Hash("tall_grass");
		case TileType::House:		   return Hash("house");
		case TileType::HouseDestroyed: return Hash("house_destroyed");
		case TileType::None:		   PTGN_ERROR("Cannot return tile key for none type tile");
		default:					   PTGN_ERROR("Unrecognized tile type");
	}
}

struct Size : public V2_float {};

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

	Texture texture;
	Texture vehicle_texture;
	Texture wheel_texture;
	Texture vehicle_dirty_texture;

	float wheel_rotation{ 0.0f };
};

struct TintColor : public Color {
	using Color::Color;
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

	Timer final_alive_timer;

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

		if (!final_alive_timer.IsRunning() && CompletedAllRequired()) {
			final_alive_timer.Start();
			return;
		}
		// TODO: Some kind of particle effects? Change tornado indicator to completed?
	}

	milliseconds required_time_after_final_completion{ 1000 };

	void CheckWinCondition(int& win) {
		if (final_alive_timer.IsRunning() && !game.tween.Has(Hash("pulled_in_tween")) &&
			final_alive_timer.ElapsedPercentage(required_time_after_final_completion) >= 1.0f) {
			// PTGN_LOG("All required tornadoes completed!");
			win = 1;
			final_alive_timer.Stop();
		}
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
		if (progress <= 0.0f || current_tornado == ecs::null ||
			game.tween.Has(Hash("pulled_in_tween"))) {
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
		if (progress <= 0.0f || current_tornado == ecs::null ||
			game.tween.Has(Hash("pulled_in_tween"))) {
			return;
		}

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
		float range{ FastAbs(
			tornado_properties.data_radius - (tornado_properties.escape_radius + arrow_size.x)
		) };

		float dist_from_escape{ dist - (tornado_properties.escape_radius + arrow_size.x) };

		PTGN_ASSERT(range != 0.0f);

		float normalized_dist{ dist_from_escape / range };

		if (normalized_dist <= 0.0f) {
			return;
		}

		Color color = Lerp(color::Red, color::Green, normalized_dist);

		const float z_index{ 10.0f };

		DrawTornadoArrowStatic(tornado_arrow_texture, player_pos, dir, color, scale, z_index);
	}

	static void DrawTornadoArrowStatic(
		const Texture& tornado_arrow_texture, const V2_float& player_pos, const V2_float& dir,
		const Color& color, float scale, float z_index
	) {
		const float arrow_pixels_from_player{ 25.0f };

		V2_float arrow_pos{ player_pos + dir.Normalized() * arrow_pixels_from_player };

		float arrow_rotation{ dir.Angle() };

		V2_float arrow_size{ tornado_arrow_texture.GetSize() * scale };

		game.renderer.DrawTexture(
			tornado_arrow_texture, arrow_pos, arrow_size, {}, {}, Origin::Center, Flip::None,
			arrow_rotation, { 0.5f, 0.5f }, z_index, color
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
		if (game.tween.Has(Hash("pulled_in_tween"))) {
			progress = 0.0f;
			return;
		}

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

		float volume_range{ tornado_properties.gravity_radius - tornado_properties.escape_radius };

		float dist_from_escape{ std::max(0.0f, dist - tornado_properties.escape_radius) };

		float normalized_sound_dist{ dist_from_escape / range };

		normalized_sound_dist = std::clamp(normalized_sound_dist, 0.0f, 1.0f);

		int volume =
			(int)Lerp(min_tornado_volume, max_tornado_volume, 1.0f - normalized_sound_dist);

		PTGN_ASSERT(game.sound.Has(Hash("tornado_sound")));
		PTGN_ASSERT(game.sound.Has(Hash("tornado_wind_sound")));
		auto& sound		 = game.sound.Get(Hash("tornado_sound"));
		auto& sound_wind = game.sound.Get(Hash("tornado_wind_sound"));
		sound.SetVolume(volume);
		sound_wind.SetVolume(volume);
		// PTGN_LOG("Volume: ", volume);

		if (dist <= tornado_properties.escape_radius) {
			progress = 0.0f;
			return;
		}

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

	std::unordered_set<V2_int> animated_tiles;

	NoiseProperties noise_properties;
	std::vector<float> noise_map;
	ValueNoise noise;

	NoiseProperties grass_noise_properties;
	std::vector<float> grass_noise_map;

	std::unordered_set<V2_int> destroyed_tiles;

	std::vector<ecs::Entity> required_tornadoes;

	json level_data;

	int level{ 0 };

	GameScene(int level) : level{ level } {
		PTGN_INFO("Starting level: ", level);
	}

	~GameScene() {
		game.sound.HaltChannel(1);
		game.sound.HaltChannel(2);
		game.sound.HaltChannel(3);

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
		game.sound.HaltChannel(1);
		game.sound.HaltChannel(2);
		game.tween.Clear();
	}

	float min_zoom{ 1.0f };
	float max_zoom{ 2.0f };
	float zoom_speed{ 0.38f };
	float zoom{ 1.5f };

	void Init() final {
		animated_tiles.reserve(100);

		auto& sound = game.sound.Get(Hash("tornado_sound"));
		sound.Stop(1);
		sound.SetVolume(min_tornado_volume);
		sound.Play(1, -1);
		auto& sound_wind = game.sound.Get(Hash("tornado_wind_sound"));
		sound_wind.Stop(2);
		sound_wind.SetVolume(min_tornado_volume);
		sound_wind.Play(2, -1);

		level_data = json_level_data.at("levels").at(level);

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

		grass_noise_properties.octaves	   = 6;
		grass_noise_properties.frequency   = 0.57f;
		grass_noise_properties.bias		   = 4.4f;
		grass_noise_properties.persistence = 1.7f;

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
		PTGN_ASSERT(player.Has<Progress>());

		player.Get<Progress>().CheckWinCondition(won);

		if (!won) {
			PlayerInput(dt);

			UpdateTornadoes(dt);

			PlayerPhysics(dt);

			UpdateBackground();

			if (game.input.KeyDown(Key::R)) {
				RestartGame();
			}
		}

		Draw();
	}

	void Draw() {
		DrawBackground();

		DrawPlayer();

		DrawTornadoes();

		if (!won) {
			DrawUI();
		} else {
			if (!game.tween.Has(Hash("winning_tween"))) {
				game.tween.Clear();

				std::string icon_path = level_data.at("ui_icon");
				std::size_t key{ Hash(icon_path) };
				PTGN_ASSERT(game.texture.Has(key));
				Texture t = game.texture.Get(key);
				constexpr float scale{ 3.0f };
				PTGN_ASSERT(game.font.Has(Hash("menu_font")));
				std::string win_text = level_data.at("win_text");
				Font font			 = game.font.Get(Hash("menu_font"));
				Text text{ font, win_text, color::Silver };
				V2_float text_size = text.GetSize();

				constexpr float text_offset_y{ 220.0f };
				constexpr float text_scale{ 0.5f };

				auto& win_tween = game.tween.Load(Hash("winning_tween"));
				win_tween.During(milliseconds{ 2000 })
					.OnUpdate([=](float f) {
						Color color	 = color::Black;
						color.a		 = static_cast<std::uint8_t>(Lerp(0.0f, 255.0f, f));
						auto& camera = game.camera.GetCurrent();
						game.renderer.DrawRectangleFilled(
							camera.GetTopLeftPosition(), game.window.GetSize(), color,
							Origin::TopLeft, 0.0f, {}, 20.0f
						);
					})
					.During(milliseconds{ 1000 })
					.OnUpdate([=](float f) mutable {
						auto& camera = game.camera.GetCurrent();

						game.renderer.DrawRectangleFilled(
							camera.GetTopLeftPosition(), camera.GetSize(), color::Black,
							Origin::TopLeft, 0.0f, {}, 20.0f
						);

						Color tint_color = color::White;
						auto alpha		 = static_cast<std::uint8_t>(Lerp(0.0f, 255.0f, f));

						tint_color.a = alpha;

						V2_float center_pos = camera.GetPosition();

						game.renderer.DrawTexture(
							t, center_pos, t.GetSize() * scale * 1.5f / zoom, {}, {},
							Origin::Center, Flip::None, 0.0f, {}, 21.0f, tint_color
						);

						Rectangle<float> text_rect{ center_pos + V2_float{ 0.0f, text_offset_y *
																					 1.5f / zoom },
													text_size, Origin::Center };

						game.renderer.DrawTexture(
							text.GetTexture(), text_rect.pos,
							text_rect.size * 1.5f / zoom * text_scale, {}, {}, Origin::Center,
							Flip::None, 0.0f, {}, 22.0f, tint_color
						);
					})
					.During(milliseconds{ 2000 })
					.OnStart([=]() mutable {})
					.OnUpdate([=]() mutable {
						auto& camera = game.camera.GetCurrent();

						game.renderer.DrawRectangleFilled(
							camera.GetTopLeftPosition(), camera.GetSize(), color::Black,
							Origin::TopLeft, 0.0f, {}, 20.0f
						);

						V2_float center_pos = camera.GetPosition();

						game.renderer.DrawTexture(
							t, center_pos, t.GetSize() * scale * 1.5f / zoom, {}, {},
							Origin::Center, Flip::None, 0.0f, {}, 21.0f, color::White
						);

						Rectangle<float> text_rect{ center_pos + V2_float{ 0.0f, text_offset_y *
																					 1.5f / zoom },
													text_size, Origin::Center };

						game.renderer.DrawTexture(
							text.GetTexture(), text_rect.pos,
							text_rect.size * 1.5f / zoom * text_scale, {}, {}, Origin::Center,
							Flip::None, 0.0f, {}, 22.0f, color::White
						);
					})
					.OnComplete([=]() { BackToLevelSelect(level, true); })
					.Start();
			}
		}
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

		if (tornado_data.contains("custom1")) {
			std::string sequence_name = "tornado_sequence_" + std::to_string(tornado_id);
			Tween& sequence			  = game.tween.Load(Hash(sequence_name));

			const auto& custom_sequence = tornado_data.at("custom1");
			V2_float rotation_point;
			V2_float end_rotation_point;
			V2_float end_pos;

			PTGN_ASSERT(entity.Has<Transform>());
			V2_float start_pos = entity.Get<Transform>().position;

			rotation_point.x = custom_sequence.at("rotation_pos").at(0);
			rotation_point.y = custom_sequence.at("rotation_pos").at(1);
			end_pos.x		 = custom_sequence.at("end_pos").at(0);
			end_pos.y		 = custom_sequence.at("end_pos").at(1);

			end_rotation_point.x = rotation_point.x;
			end_rotation_point.y = end_pos.y;

			V2_float rotation_dir{ rotation_point - start_pos };

			const float starting_angle{ rotation_dir.Angle() };

			float rotation_distance{ rotation_dir.Magnitude() };

			PTGN_ASSERT(rotation_distance > 0.0f);

			int rotation_time_ms = custom_sequence.at("rotation_time");
			milliseconds rotation_time{ rotation_time_ms };
			int linear_time_ms = custom_sequence.at("linear_time");
			milliseconds linear_time{ linear_time_ms };

			const float rotation_time_factor =
				static_cast<float>(rotation_time_ms) / static_cast<float>(linear_time_ms);
			PTGN_ASSERT(rotation_time_factor > 0.0f);

			sequence.During(linear_time).OnUpdate([=](float progress) mutable {
				V2_float point = Lerp(rotation_point, end_rotation_point, progress);
				float angle =
					Lerp(0.0f, two_pi<float>, std::fmod(progress / rotation_time_factor, 1.0f));
				float x = point.x + std::cos(angle + starting_angle) * rotation_distance;
				float y = point.y + std::sin(angle + starting_angle) * rotation_distance;
				PTGN_ASSERT(entity.Has<Transform>());
				auto& transform	   = entity.Get<Transform>();
				transform.position = V2_float{ x, y };
			});

			sequence.Start();
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
		noise			= { 256, seed };
		noise_map		= FractalNoise::Generate(noise, {}, grid_size, noise_properties);
		grass_noise_map = FractalNoise::Generate(noise, {}, grid_size, grass_noise_properties);
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

		auto play_car_sound = [](std::string_view name) {
			if (!game.sound.IsPlayingChannel(3)) {
				PTGN_ASSERT(game.sound.Has(Hash(name)));
				auto& sound = game.sound.Get(Hash(name));
				sound.SetVolume(car_volume);
				sound.Play(3);
			}
		};

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
				play_car_sound("car_start");
			} else {
				auto& throttle = game.tween.Get(Hash("throttle_tween"));
				if (!throttle.IsStarted() && !throttle.IsCompleted()) {
					play_car_sound("car_start");
					throttle.Start();
				} else {
					play_car_sound("engine_sound");
				}
			}
		} else {
			if (game.sound.IsPlayingChannel(3)) {
				game.sound.HaltChannel(3);
			}
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

	ecs::Entity nearest_uncompleted_tornado_entity = ecs::null;

	int won = 0;

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

		float nearest_uncompleted_tornado{ std::numeric_limits<float>::max() };
		nearest_uncompleted_tornado_entity = ecs::null;

		std::vector<ecs::Entity> data_tornadoes;

		for (auto [e, tornado, transform, rigid_body] : tornadoes) {
			V2_float dir{ transform.position - player_transform.position };

			if (!player.Get<Progress>().CompletedTornado(e)) {
				float dist2 = dir.MagnitudeSquared();
				if (dist2 < nearest_uncompleted_tornado) {
					nearest_uncompleted_tornado		   = dist2;
					nearest_uncompleted_tornado_entity = e;
				}
			}

			if (!game.collision.overlap.PointCircle(
					player_transform.position, { transform.position, tornado.gravity_radius }
				)) {
				continue;
			}

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
			auto& sound = game.sound.Get(Hash("tornado_sound"));
			sound.SetVolume(min_tornado_volume);
			auto& sound_wind = game.sound.Get(Hash("tornado_wind_sound"));
			sound_wind.SetVolume(min_tornado_volume);
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

	RNG<float> animation_rng{ 0.0f, 1.0f };
	float tall_grass_animation_probability{ 0.1f };

	void TornadoMotion(float dt) {
		auto tornadoes = manager.EntitiesWith<TornadoComponent, Transform, RigidBody>();

		const float tornado_move_speed{ 1000.0f };

		animated_tiles.clear();

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

			V2_int min_gravity{ (transform.position -
								 V2_float{ tornado.gravity_radius, tornado.gravity_radius }) /
								tile_size };
			V2_int max_gravity{ (transform.position +
								 V2_float{ tornado.gravity_radius, tornado.gravity_radius }) /
								tile_size };

			V2_int min_escape{ (transform.position -
								V2_float{ tornado.escape_radius, tornado.escape_radius }) /
							   tile_size };
			V2_int max_escape{ (transform.position +
								V2_float{ tornado.escape_radius, tornado.escape_radius }) /
							   tile_size };

			Circle<float> tornado_destruction{ transform.position, tornado.escape_radius };
			Circle<float> tornado_gravity{ transform.position, tornado.gravity_radius };

			PTGN_ASSERT(min_gravity.x <= max_gravity.x);
			PTGN_ASSERT(min_gravity.y <= max_gravity.y);
			PTGN_ASSERT(min_escape.x <= max_escape.x);
			PTGN_ASSERT(min_escape.y <= max_escape.y);

			// Destroy all tiles within escape radius of tornado
			for (int i = min_escape.x; i <= min_escape.x; i++) {
				for (int j = min_escape.y; j <= min_escape.y; j++) {
					V2_int tile{ i, j };
					Rectangle<float> tile_rect{ tile * tile_size, tile_size, Origin::TopLeft };
					// auto tile_type = GetTileType(GetNoiseValue(tile));
					if (game.collision.overlap.CircleRectangle(tornado_destruction, tile_rect)) {
						if (draw_hitboxes) {
							game.renderer.DrawRectangleFilled(tile_rect, color::Purple);
						}
						destroyed_tiles.insert(tile);
					}
				}
			}

			// Animate random tiles within tornado radius.
			for (int i = min_gravity.x; i <= max_gravity.x; i++) {
				for (int j = min_gravity.y; j <= max_gravity.y; j++) {
					V2_int tile{ i, j };
					Rectangle<float> tile_rect{ tile * tile_size, tile_size, Origin::TopLeft };
					// auto tile_type = GetTileType(GetNoiseValue(tile));
					if (game.collision.overlap.CircleRectangle(tornado_gravity, tile_rect)) {
						// game.renderer.DrawRectangleFilled(tile_rect, color::Purple, 0.0f,
						// {}, 40.0f);
						float p{ animation_rng() };
						if (p <= tall_grass_animation_probability) {
							animated_tiles.insert(tile);
						}
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
			player_transform.rotation + vehicle.wheel_rotation, { 0.5f, 0.5f }, 1.0f, tint
		);

		game.renderer.DrawTexture(
			vehicle.texture, player_transform.position, size, {}, {}, Origin::Center, Flip::None,
			player_transform.rotation, { 0.5f, 0.5f }, 2.0f, tint

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

	float GetGrassNoiseValue(const V2_int& tile) {
		int index{ tile.x + grid_size.x * tile.y };
		if (index >= grass_noise_map.size() || index < 0) {
			return -1.0f;
		}
		float noise_value{ grass_noise_map[index] };
		PTGN_ASSERT(noise_value >= 0.0f);
		PTGN_ASSERT(noise_value <= 1.0f);
		return noise_value;
	}

	milliseconds tall_grass_animation_duration{ 300 };
	const int tall_grass_animation_columns{ 4 };

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

				if (tile_type == TileType::Grass) {
					float grass_noise_value{ GetGrassNoiseValue(tile) };
					if (grass_noise_value >= 0.65f) {
						tile_type = TileType::TallGrass;
					}
				}

				Texture t = game.texture.Get(GetTileKey(tile_type));

				if (tile_type == TileType::TallGrass) {
					bool animated_tile{ animated_tiles.count(tile) > 0 };
					if (animated_tile) {
						game.tween.Load(Hash(tile))
							.During(tall_grass_animation_duration)
							.Yoyo()
							.OnUpdate([=](float f) {
								auto column = static_cast<int>(
									f * static_cast<float>(tall_grass_animation_columns - 1)
								);
								game.renderer.DrawTexture(
									t, tile_rect.pos, size, V2_int{ column * tile_size.x, 0 },
									tile_size, Origin::TopLeft, Flip::None, 0.0f, { 0.5f, 0.5f },
									1.0f
								);
							})
							.Start();
					}
					int column = 0;
					game.renderer.DrawTexture(
						t, tile_rect.pos, size, V2_int{ column, 0 }, tile_size, Origin::TopLeft,
						Flip::None, 0.0f, { 0.5f, 0.5f }, z_index
					);
				} else {
					game.renderer.DrawTexture(
						t, tile_rect.pos, size, {}, {}, Origin::TopLeft, Flip::None, 0.0f,
						{ 0.5f, 0.5f }, z_index
					);
				}
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

	void DrawTornadoArrowGlobal(const V2_float& player_pos) {
		if (nearest_uncompleted_tornado_entity == ecs::null ||
			game.tween.Has(Hash("pulled_in_tween"))) {
			return;
		}

		PTGN_ASSERT(game.texture.Has(Hash("tornado_arrow")));
		const Texture tornado_arrow_texture = game.texture.Get(Hash("tornado_arrow"));

		const float arrow_scale{ 1.0f };
		const float z_index{ 10.0f };
		const Color arrow_tint = color::White;

		V2_float arrow_size{ tornado_arrow_texture.GetSize() * arrow_scale };

		PTGN_ASSERT(nearest_uncompleted_tornado_entity.Has<TornadoComponent>());
		PTGN_ASSERT(nearest_uncompleted_tornado_entity.Has<Transform>());

		TornadoComponent tornado_properties =
			nearest_uncompleted_tornado_entity.Get<TornadoComponent>();
		V2_float tornado_center{ nearest_uncompleted_tornado_entity.Get<Transform>().position };

		V2_float dir{ tornado_center - player_pos };

		float dist{ dir.Magnitude() };

		float dist_from_escape{ dist - (tornado_properties.escape_radius + arrow_size.x) };

		if (dist_from_escape <= 0) {
			return;
		}

		Progress::DrawTornadoArrowStatic(
			tornado_arrow_texture, player_pos, dir, arrow_tint, arrow_scale, z_index
		);
	}

	void DrawUI() {
		PTGN_ASSERT(player.Has<Progress>());
		PTGN_ASSERT(player.Has<Transform>());
		auto& player_pos = player.Get<Transform>().position;

		DrawTornadoArrowGlobal(player_pos);

		game.renderer.Flush();
		game.camera.SetCameraWindow();

		// Draw UI here...

		player.Get<Progress>().Draw(player_pos);
		DrawSpeedometer();
		if (level == 0 && !game.tween.Has(Hash("pulled_in_tween"))) {
			const Texture tutorial_text{ game.texture.Get(Hash("tutorial_text")) };
			const V2_float text_size{ tutorial_text.GetSize() };
			const V2_float text_pos{ static_cast<float>(resolution.x), 0.0f };
			game.renderer.DrawTexture(tutorial_text, text_pos, text_size, {}, {}, Origin::TopRight);
		}

		game.renderer.Flush();
		game.camera.SetCameraPrimary();
	}
};

const int text_x_offset{ 14 };
const int button_y_offset{ 14 };
const V2_int button_size{ 150, 50 };
const V2_int first_button_coordinate{ 40, 180 };

TextButton CreateMenuButton(
	const std::string& content_enabled, Color text_color, const ButtonActivateFunction& f,
	const Color& color, Color hover_color, const std::string& content_disabled = ""
) {
	TextButton b;
	Text text{ Hash("menu_font"), content_enabled, text_color };
	b.SetText(text);
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
	return b;
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

		buttons[0].SetRectangle({ V2_int{ 820, 636 }, button_size, Origin::TopLeft });

		for (int i = 0; i < (int)buttons.size(); i++) {
			buttons[i].SubscribeToMouseEvents();
		}
	}

	void Shutdown() final {
		for (int i = 0; i < (int)buttons.size(); i++) {
			buttons[i].UnsubscribeFromMouseEvents();
		}
	}

	void Update() final {
		game.renderer.DrawTexture(
			game.texture.Get(Hash("text_screen_background")), game.window.GetCenter(), resolution,
			{}, {}, Origin::Center, Flip::None, 0.0f, {}, -1.0f
		);

		for (std::size_t i = 0; i < buttons.size(); i++) {
			auto rect	= buttons[i].GetRectangle();
			rect.pos.x += text_x_offset;
			rect.size.x =
				buttons[i]
					.GetText()
					.GetSize(Hash("menu_font"), std::string(buttons[i].GetText().GetContent()))
					.x *
				0.5f;
			buttons[i].GetText().Draw(rect);
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

		if (!game.texture.Has(Hash("win_background"))) {
			game.texture.Load(Hash("win_background"), "resources/ui/win_screen.png");
		}

		std::size_t bg_count = 5;

		for (std::size_t i = 0; i < bg_count; i++) {
			std::string name = "level_select_bg" + std::to_string(i);
			if (!game.texture.Has(Hash(name))) {
				game.texture.Load(
					Hash(name),
					std::string("resources/ui/bg") + std::to_string(i) + std::string(".png")
				);
			}
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

	void PlayMusic(std::size_t music_key) const {
		auto& music = game.music.Get(music_key);
		game.music.SetVolume(music_volume);
		music.Play(-1);
	}

	bool won = false;

	Rectangle<float> text_rect;

	Text mirror_text{ Hash("menu_font"), "Final Boss", color::Silver };

	int final_level_number{ 9 };

	bool final_level{ false };

	Texture select_bg;

	void Init() final {
		text_rect = { V2_int{ 1223, 98 }, button_size, Origin::Center };

		level_data = json_level_data;

		std::size_t furthest_branch = 0;

		for (const auto& b : level_data.at("branches")) {
			for (std::size_t i = 0; i < b.size(); i++) {
				furthest_branch = std::max(i, furthest_branch);
			}
		}

		auto& difficulties = level_data.at("difficulty_layers");

		PTGN_ASSERT(difficulties.size() >= furthest_branch + 1);

		PTGN_ASSERT(!difficulties.empty());

		if (!game.music.IsPlaying()) {
			std::string music_path = difficulties.at(0).at("music");
			std::size_t music_key  = Hash(music_path);
			playing_music_key	   = music_key;
			if (!game.music.Has(music_key)) {
				game.music.Load(music_key, music_path);
			}
			PlayMusic(music_key);
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

		std::string bg_name = "level_select_bg" + std::to_string(difficulty_layer);
		std::size_t bg_key	= Hash(bg_name);

		if (game.texture.Has(bg_key)) {
			select_bg = game.texture.Get(bg_key);
		} else {
			PTGN_ASSERT(game.texture.Has(Hash("level_select_bg0")));
			select_bg = game.texture.Get(Hash("level_select_bg0"));
		}

		std::string music_path = difficulties.at(difficulty_layer).at("music");
		std::size_t music_key  = Hash(music_path);

		if (playing_music_key != music_key) {
			game.music.Stop();
			playing_music_key = music_key;
			if (!game.music.Has(music_key)) {
				game.music.Load(music_key, music_path);
			}
			PlayMusic(music_key);
		}

		if (!level_buttons.empty()) {
			won = false;
		}

		final_level = false;

		if (level_buttons.empty()) {
			PTGN_INFO("You won! No levels available");
			won = true;
		} else if (level_buttons.size() == 1) {
			if (std::get<0>(level_buttons.at(0)) == final_level_number) {
				final_level = true;
			}

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
		if (!won) {
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

			buttons[0].SetRectangle({ V2_int{ 596, 505 }, button_size, Origin::CenterTop });
			buttons[1].SetRectangle({ V2_int{ 830, 505 }, button_size, Origin::CenterTop });
			buttons[2].SetRectangle({ V2_int{ 820, 636 }, button_size, Origin::TopLeft });
		} else {
			buttons.push_back(CreateMenuButton(
				"Restart", color::Blue,
				[&]() {
					won = false;
					game.music.Stop();
					game.scene.Unload(Hash("level_select"));
					if (!game.scene.Has(Hash("main_menu"))) {
						LoadMainMenu();
					}
					game.scene.AddActive(Hash("main_menu"));
				},
				color::Transparent, color::Black
			));
			buttons[0].SetRectangle({ V2_int{ 1223, 98 }, button_size, Origin::Center });
		}

		for (int i = 0; i < (int)buttons.size(); i++) {
			buttons[i].SubscribeToMouseEvents();
		}
	}

	void Shutdown() final {
		for (int i = 0; i < (int)buttons.size(); i++) {
			buttons[i].UnsubscribeFromMouseEvents();
		}
		for (int i = 0; i < (int)level_buttons.size(); i++) {
			std::get<1>(level_buttons[i])->UnsubscribeFromMouseEvents();
		}
	}

	void Update() final {
		if (!level_buttons.empty()) {
			if (selected_level == -1 ||
				(level_buttons.size() == 1 &&
				 std::get<1>(level_buttons[0])->GetTintColor() == color::White) ||
				(level_buttons.size() == 2 &&
				 std::get<1>(level_buttons[0])->GetTintColor() == color::White &&
				 std::get<1>(level_buttons[1])->GetTintColor() == color::White)) {
				if (buttons.size() > 0) {
					buttons[0].SetInteractable(false);
				}
				if (buttons.size() > 1) {
					buttons[1].SetInteractable(false);
				}
			} else {
				if (buttons.size() > 0) {
					buttons[0].SetInteractable(true);
				}
				if (buttons.size() > 1) {
					buttons[1].SetInteractable(true);
				}
			}
		}

		std::size_t background = Hash("level_select_background");

		if (won) {
			background = Hash("win_background");
		}

		// Top left corner of the laptop screen.
		game.renderer.DrawTexture(
			select_bg, { 548, 161 }, { 371, 265 }, {}, {}, Origin::TopLeft, Flip::None, 0.0f, {},
			-2.0f
		);

		game.renderer.DrawTexture(
			game.texture.Get(background), game.window.GetCenter(), resolution, {}, {},
			Origin::Center, Flip::None, 0.0f, {}, -1.0f
		);

		for (std::size_t i = 0; i < buttons.size(); i++) {
			auto rect	= buttons[i].GetRectangle();
			rect.pos.x += text_x_offset;
			rect.size.x =
				buttons[i]
					.GetText()
					.GetSize(Hash("menu_font"), std::string(buttons[i].GetText().GetContent()))
					.x *
				0.5f;
			buttons[i].GetText().Draw(rect);
		}

		for (std::size_t i = 0; i < level_buttons.size(); i++) {
			std::get<1>(level_buttons[i])->Draw();
		}

		if (final_level) {
			// Intentionally copying.
			Rectangle<float> rect  = text_rect;
			rect.pos.x			  += text_x_offset;
			rect.size.x =
				mirror_text.GetSize(Hash("menu_font"), std::string(mirror_text.GetContent())).x *
				0.5f;
			mirror_text.Draw(rect);
		}
	}
};

class MainMenu : public Scene {
public:
	MainMenu() {
		game.texture.Load(Hash("tutorial_text"), "resources/ui/instructions.png");
		game.texture.Load(Hash("grass"), "resources/entity/grass.png");
		game.texture.Load(Hash("tall_grass"), "resources/entity/tall_grass.png");
		game.texture.Load(Hash("dirt"), "resources/entity/dirt.png");
		game.texture.Load(Hash("corn"), "resources/entity/corn.png");
		game.texture.Load(Hash("house"), "resources/entity/house.png");
		game.texture.Load(Hash("house_destroyed"), "resources/entity/house_destroyed.png");
		game.texture.Load(Hash("tornado_icon"), "resources/ui/tornado_icon.png");
		game.texture.Load(Hash("tornado_icon_green"), "resources/ui/tornado_icon_green.png");
		game.texture.Load(Hash("tornado_arrow"), "resources/ui/arrow.png");
		game.texture.Load(Hash("speedometer"), "resources/ui/speedometer.png");
		if (!game.sound.Has(Hash("tornado_sound"))) {
			game.sound.Load(Hash("tornado_sound"), "resources/audio/tornado.ogg");
		}
		if (!game.sound.Has(Hash("tornado_wind_sound"))) {
			game.sound.Load(Hash("tornado_wind_sound"), "resources/audio/wind.ogg");
		}
		if (!game.sound.Has(Hash("engine_sound"))) {
			game.sound.Load(Hash("engine_sound"), "resources/audio/car_1.ogg");
		}
		if (!game.sound.Has(Hash("car_start"))) {
			game.sound.Load(Hash("car_start"), "resources/audio/car_start.ogg");
		}

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
		game.ui.button
			.Load(
				Hash("play"),
				CreateMenuButton(
					"Play", color::Cyan,
					[&]() {
						game.scene.RemoveActive(Hash("main_menu"));
						if (game.scene.Has(Hash("level_select"))) {
							game.scene.Get<LevelSelect>(Hash("level_select"))->ClearChoices();
						} else {
							game.scene.Load<LevelSelect>(Hash("level_select"));
						}
						game.scene.AddActive(Hash("level_select"));
					},
					color::Transparent, color::Black
				)
			)
			->SetRectangle({ V2_int{ 560, 505 }, button_size, Origin::TopLeft });
		game.ui.button
			.Load(
				Hash("tutorial"),
				CreateMenuButton(
					"Tutorial", color::Gold,
					[&]() {
						game.scene.RemoveActive(Hash("main_menu"));
						auto screen		  = game.scene.Get<TextScreen>(Hash("text_screen"));
						screen->back_name = "main_menu";
						screen->text.SetContent(
							"When in level select, click on the storm you wish to chase. "
							"Then click details for "
							"info about the chosen storm, then start the chase!"
						);
						game.scene.AddActive(Hash("text_screen"));
					},
					color::Transparent, color::Black
				)
			)
			->SetRectangle({ V2_int{ 770, 505 }, button_size, Origin::TopLeft });

		/*	buttons[0].button->SetRectangle({ V2_int{ 550, 505 }, button_size, Origin::TopLeft });
			buttons[1].button->SetRectangle({ V2_int{ 780, 505 }, button_size, Origin::TopLeft });*/

		// buttons.push_back(CreateMenuButton(
		//	"Settings", color::White,
		//	[]() {
		//		/*game.scene.RemoveActive(Hash("main_menu"));
		//		game.scene.AddActive(Hash("game"));*/
		//	},
		//	color::Red, color::Black
		//));
	}

	void Shutdown() final {
		// TODO: Fix button click crash.
		game.ui.button.Clear();
	}

	void Update() final {
		game.renderer.DrawTexture(
			game.texture.Get(Hash("menu_background")), game.window.GetCenter(), resolution, {}, {},
			Origin::Center, Flip::None, 0.0f, {}, -1.0f
		);
		game.ui.button.DrawAllHollow(6.0f);
		/*rect.pos.x += text_x_offset;
		rect.size.x =
			buttons[i]
				.GetText()
				.GetSize(Hash("menu_font"), std::string(buttons[i].GetText().GetContent()))
				.x *
			0.5f;
		buttons[i].GetText().Draw(rect);*/
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
