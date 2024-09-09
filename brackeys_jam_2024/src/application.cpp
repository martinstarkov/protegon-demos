#include <unordered_set>

#include "protegon/protegon.h"

using namespace ptgn;

constexpr const V2_int resolution{ 960, 540 };
constexpr const V2_int center{ resolution / 2 };
constexpr const bool draw_hitboxes{ true };

enum class TileType {
	Grass,
	Dirt,
	Corn,
	None,
};

TileType GetTileType(float noise_value) {
	if (noise_value < 0.0f) {
		return TileType::None;
	}

	if (noise_value >= 0.0f && noise_value <= 0.5f) {
		return TileType::Corn;
	} else {
		return TileType::Grass;
	}
	PTGN_ERROR("Unrecognized tile type");
}

std::size_t GetTileKey(TileType tile_type) {
	switch (tile_type) {
		case TileType::Grass: return Hash("grass");
		case TileType::Corn:  return Hash("corn");
		case TileType::Dirt:  return Hash("dirt");
		case TileType::None:  PTGN_ERROR("Cannot return tile key for none type tile");
		default:			  PTGN_ERROR("Unrecognized tile type");
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
};

struct TornadoComponent {
	float turn_speed{ 0.0f };
	float gravity_radius{ 0.0f };
	float escape_radius{ 0.0f };
	float data_radius{ 0.0f };

	float outermost_increment_ratio{ 0.0f };
	float innermost_increment_ratio{ 1.0f };
	float increment_speed{ 1.0f };

	Color tint{ color::White };

	// Abritrary units related to frame rate.
	constexpr static float wind_constant{ 10.0f };

	// direction is a vector pointing from the target torward the tornado's center.
	// pull_resistance determines how much the target resists the inward pull of the tornado
	V2_float GetSuction(const V2_float& direction, float max_thrust) const {
		float dist{ direction.Magnitude() };

		// haha.. very funny...
		float suction_force = escape_radius * escape_radius / (dist * dist) * max_thrust;

		V2_float suction{ direction.Normalized() * suction_force };

		return suction;
	}

	V2_float GetWind(const V2_float& direction, float pull_resistance) const {
		float dist{ direction.Magnitude() };

		float wind_speed = escape_radius / dist * wind_constant * turn_speed / pull_resistance;

		V2_float wind{ direction.Rotated(-half_pi<float>).Normalized() * wind_speed };

		return wind;
	}
};

struct Progress {
	Texture texture;

	std::vector<ecs::Entity> completed_tornados;

	ecs::Entity current_tornado;

	Progress(const path& ui_texture_path) : texture{ ui_texture_path } {}

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
		PTGN_LOG("Completed tornado: ", current_tornado.GetId());
		// TODO: Some kind of particle effects? Change tornado indicator to completed?
	}

	void Draw() const {
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
			tornado_properties.outermost_increment_ratio,
			tornado_properties.innermost_increment_ratio, normalized_dist
		) };

		progress += tornado_properties.increment_speed * increment_ratio * dt;

		progress = std::clamp(progress, 0.0f, 1.0f);

		if (progress >= 1.0f) {
			AddTornado(tornado);
		}
	}
};

struct TintColor : public Color {};

class GameScene : public Scene {
public:
	ecs::Manager manager;

	ecs::Entity player;

	const V2_float tile_size{ 32, 32 };
	const V2_int grid_size{ 300, 300 };

	NoiseProperties noise_properties;
	std::vector<float> noise_map;
	const ValueNoise noise{ 256, 0 };

	std::unordered_set<V2_int> destroyed_tiles;

	GameScene() {}

	void RestartGame() {
		destroyed_tiles.clear();
		manager.Reset();
		Init();
	}

	void Init() final {
		noise_properties.octaves	 = 6;
		noise_properties.frequency	 = 0.01f;
		noise_properties.bias		 = 1.2f;
		noise_properties.persistence = 0.75f;

		game.texture.Load(Hash("grass"), "resources/entity/grass.png");
		game.texture.Load(Hash("dirt"), "resources/entity/dirt.png");
		game.texture.Load(Hash("corn"), "resources/entity/corn.png");

		player = CreatePlayer();

		CreateTornado(center + V2_float{ 200, 200 }, 50.0f);

		CreateBackground();

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

		auto& texture = entity.Add<Texture>(Texture{ "resources/entity/car.png" });

		entity.Add<Size>(texture.GetSize());
		entity.Add<Progress>("resources/ui/tornadometer.png");

		auto& transform	   = entity.Add<Transform>();
		transform.position = center;

		auto& rigid_body		= entity.Add<RigidBody>();
		rigid_body.max_velocity = 120.0f;

		auto& vehicle			= entity.Add<VehicleComponent>();
		vehicle.forward_thrust	= 1000.0f;
		vehicle.backward_thrust = 0.3f * vehicle.forward_thrust;
		vehicle.turn_speed		= 1.5f;
		vehicle.inertia			= 200.0f;

		auto& aero			 = entity.Add<Aerodynamics>();
		aero.pull_resistance = 1.0f;

		return entity;
	}

	ecs::Entity CreateTornado(const V2_float& position, float turn_speed) {
		ecs::Entity entity = manager.CreateEntity();
		auto& texture	   = entity.Add<Texture>(Texture{ "resources/entity/tornado.png" });

		auto& transform	   = entity.Add<Transform>();
		transform.position = position;

		entity.Add<Size>(texture.GetSize());

		auto& tornado = entity.Add<TornadoComponent>();

		tornado.turn_speed	   = turn_speed;
		tornado.escape_radius  = texture.GetSize().x / 2.0f;
		tornado.data_radius	   = 3.0f * tornado.escape_radius;
		tornado.gravity_radius = 8.0f * tornado.escape_radius;

		auto& rigid_body		= entity.Add<RigidBody>();
		rigid_body.max_velocity = 110.0f;

		PTGN_ASSERT(tornado.data_radius > tornado.escape_radius);
		PTGN_ASSERT(tornado.gravity_radius >= tornado.data_radius);

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

		V2_float unit_direction{ V2_float{ 1.0f, 0.0f }.Rotated(transform.rotation) };

		V2_float thrust;

		bool up{ game.input.KeyPressed(Key::W) };
		bool left{ game.input.KeyPressed(Key::A) };
		bool down{ game.input.KeyPressed(Key::S) };
		bool right{ game.input.KeyPressed(Key::D) };

		if (up) {
			thrust = unit_direction * vehicle.forward_thrust;
		} else if (down) {
			thrust = unit_direction * -vehicle.backward_thrust;
		}

		if (right) {
			transform.rotation += vehicle.turn_speed * dt;
		}
		if (left) {
			transform.rotation -= vehicle.turn_speed * dt;
		}

		rigid_body.acceleration += thrust;
	}

	void PlayerPhysics(float dt) {
		PTGN_ASSERT(player.Has<RigidBody>());
		PTGN_ASSERT(player.Has<Transform>());

		auto& rigid_body = player.Get<RigidBody>();
		auto& transform	 = player.Get<Transform>();

		const float drag{ 0.85f };

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
					if (game.collision.overlap.CircleRectangle(tornado_destruction, tile_rect)) {
						game.renderer.DrawRectangleFilled(tile_rect, color::Purple);
						destroyed_tiles.insert(tile);
					}
				}
			}

			transform.rotation += tornado.turn_speed * dt;
		}
	}

	// Draw functions.

	void DrawPlayer() {
		PTGN_ASSERT(player.Has<Transform>());
		PTGN_ASSERT(player.Has<Texture>());
		PTGN_ASSERT(player.Has<Size>());

		auto& player_transform{ player.Get<Transform>() };

		game.renderer.DrawTexture(
			player.Get<Texture>(), player_transform.position, player.Get<Size>(), {}, {},
			Origin::Center, Flip::None, player_transform.rotation
		);
	}

	void DrawTornados() {
		auto tornados = manager.EntitiesWith<TornadoComponent, Texture, Transform, Size>();

		for (auto [e, tornado, texture, transform, size] : tornados) {
			game.renderer.DrawTexture(
				texture, transform.position, size, {}, {}, Origin::Center, Flip::None,
				transform.rotation, { 0.5f, 0.5f }, 0.0f, tornado.tint
			);

			if (draw_hitboxes) {
				game.renderer.DrawCircleHollow(
					transform.position, tornado.gravity_radius, color::Blue
				);
				game.renderer.DrawCircleHollow(
					transform.position, tornado.escape_radius, color::Red
				);
				game.renderer.DrawCircleHollow(
					transform.position, tornado.data_radius, color::DarkGreen
				);
			}
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

		game.renderer.DrawRectangleHollow(camera_rect, color::Blue, 3.0f);

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

				if (destroyed_tile) {
					tile_type = TileType::Dirt;
				}

				Texture t = game.texture.Get(GetTileKey(tile_type));

				game.renderer.DrawTexture(
					t, tile_rect.pos, tile_rect.size, {}, {}, Origin::TopLeft
				);
			}
		}
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

const int button_y_offset{ 14 };
const V2_int button_size{ 250, 50 };
const V2_int first_button_coordinate{ 250, 220 };

TextButton CreateMenuButton(
	const std::string& content, const Color& text_color, const ButtonActivateFunction& f,
	const Color& color, const Color& hover_color
) {
	ColorButton b;
	b.SetOnActivate(f);
	b.SetColor(color);
	b.SetHoverColor(hover_color);
	Text text{ Hash("menu_font"), content, color };
	return TextButton{ std::make_shared<ColorButton>(b), text };
}

class LevelSelect : public Scene {
public:
	std::vector<TextButton> buttons;

	LevelSelect() {}

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
			"Easy", color::Blue, [&]() { StartGame(); }, color::Blue, color::Black
		));
		buttons.push_back(CreateMenuButton(
			"Medium", color::Green, [&]() { StartGame(); }, color::Gold, color::Black
		));
		buttons.push_back(CreateMenuButton(
			"Hard", color::Red, [&]() { StartGame(); }, color::Red, color::Black
		));
		buttons.push_back(CreateMenuButton(
			"Back", color::Black,
			[]() {
				game.scene.RemoveActive(Hash("level_select"));
				game.scene.SetActive(Hash("main_menu"));
			},
			color::LightGrey, color::Black
		));

		for (int i = 0; i < (int)buttons.size(); i++) {
			buttons[i].button->SetRectangle({ V2_int{ first_button_coordinate.x,
													  first_button_coordinate.y +
														  i * (button_size.y + button_y_offset) },
											  button_size, Origin::CenterTop });
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
			game.texture.Get(Hash("menu_background")), game.window.GetCenter(), resolution, {}, {},
			Origin::Center, Flip::None, 0.0f, {}, -1.0f
		);
		for (std::size_t i = 0; i < buttons.size(); i++) {
			buttons[i].button->DrawHollow(6.0f);
			auto rect = buttons[i].button->GetRectangle();
			rect.size.x =
				buttons[i]
					.text.GetSize(Hash("menu_font"), std::string(buttons[i].text.GetContent()))
					.x *
				0.5f;
			buttons[i].text.Draw(rect);
		}
	}
};

class MainMenu : public Scene {
public:
	std::vector<TextButton> buttons;

	MainMenu() {
		game.font.Load(Hash("menu_font"), "resources/font/retro_gaming.ttf", button_size.y);
		game.texture.Load(Hash("menu_background"), "resources/ui/background.png");

		// TODO: Readd.
		// game.music.Load(Hash("background_music"),
		// "resources/sound/background_music.ogg").Play(-1);

		game.scene.Load<LevelSelect>(Hash("level_select"));
	}

	void Init() final {
		buttons.clear();
		buttons.push_back(CreateMenuButton(
			"Play", color::Blue,
			[]() {
				game.scene.RemoveActive(Hash("main_menu"));
				game.scene.SetActive(Hash("level_select"));
			},
			color::Blue, color::Black
		));
		// buttons.push_back(CreateMenuButton(
		//	"Settings", color::Red,
		//	[]() {
		//		/*game.scene.RemoveActive(Hash("main_menu"));
		//		game.scene.SetActive(Hash("game"));*/
		//	},
		//	color::Red, color::Black
		//));

		for (int i = 0; i < (int)buttons.size(); i++) {
			buttons[i].button->SetRectangle({ V2_int{ first_button_coordinate.x,
													  first_button_coordinate.y +
														  i * (button_size.y + button_y_offset) },
											  button_size, Origin::CenterTop });
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
			buttons[i].button->DrawHollow(7.0f);
			auto rect = buttons[i].button->GetRectangle();
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

class SetupScene : public Scene {
public:
	SetupScene() {}

	void Init() final {
		game.renderer.SetClearColor(color::Silver);
		game.window.SetSize(resolution);

		/*std::size_t initial_scene{ Hash("main_menu") };
		game.scene.Load<MainMenu>(initial_scene);*/

		std::size_t initial_scene{ Hash("game") };
		game.scene.Load<GameScene>(initial_scene);
		game.scene.SetActive(initial_scene);
	}
};

int main() {
	game.Start<SetupScene>();
	return 0;
}
