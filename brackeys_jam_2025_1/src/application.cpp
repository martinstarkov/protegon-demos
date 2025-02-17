#include "protegon/protegon.h"

using namespace ptgn;

constexpr V2_int window_size{ 1280, 720 };
constexpr V2_float tile_size{ 128, 128 };
constexpr Color window_color{ color::Transparent };
constexpr const char* window_title{ "Organ Delivery" };
constexpr float zoom{ 3.0f };

struct CarController {
	float acceleration{ 0.0f };
	float drag{ 0.0f };
	float steer_angle{ 0.0f };
	float traction{ 0.0f };
	float drifting_drag{ 0.0f };
	float drifting_steer_angle{ 0.0f };
	float drifting_traction{ 0.0f };

	Key forward_key{ Key::W };
	Key reverse_key{ Key::S };
	Key left_key{ Key::A };
	Key right_key{ Key::D };

	void SetDrifting(bool drifting) {
		drifting_ = drifting;
	}

	[[nodiscard]] bool IsDrifting() const {
		return drifting_;
	}

	// @param dt Unit: seconds.
	void Update(Transform& transform, RigidBody& rb, float dt) {
		V2_float dir;

		bool forward{ game.input.KeyPressed(forward_key) };
		bool reverse{ game.input.KeyPressed(reverse_key) };
		bool left{ game.input.KeyPressed(left_key) };
		bool right{ game.input.KeyPressed(right_key) };

		if (forward && !reverse) {
			dir.x = 1.0f;
		} else if (reverse && !forward) {
			dir.x = -1.0f;
		}
		if (left && !right) {
			dir.y = -1.0f;
		} else if (right && !left) {
			dir.y = 1.0f;
		}

		float forward_input{ dir.x };
		float steer_input{ dir.x * dir.y };

		auto forward_vector = [&](float rotation) {
			return V2_float{ forward_input, 0.0f }.Rotated(rotation);
		};

		rb.AddAcceleration(forward_vector(transform.rotation) * acceleration, dt);

		float steer{ drifting_ ? drifting_steer_angle : steer_angle };

		transform.rotation += steer_input * rb.velocity.Magnitude() * steer * dt;

		transform.rotation = ClampAngle2Pi(transform.rotation);

		/*	PTGN_LOG(
				"transform.rotation: ", transform.rotation,
				", forward vector: ", forward_vector(transform)
			);*/

		rb.drag = drifting_ ? drifting_drag : drag;

		float trac{ drifting_ ? drifting_traction : traction };

		auto norm_vel{ rb.velocity.Normalized() };
		auto forward_vec{ forward_vector(transform.rotation) };

		auto current_drift_angle{ forward_vec.Angle(norm_vel) };

		drifting_ = current_drift_angle >= DegToRad(10.0f);

		rb.velocity = rb.velocity.Magnitude() * Lerp(norm_vel, forward_vec, trac * dt);
	}

private:
	bool drifting_{ false };
};

void to_json(json& j, const CarController& c) {
	j = json{ { "acceleration", c.acceleration },
			  { "drag", c.drag },
			  { "steer_angle", c.steer_angle },
			  { "traction", c.traction },
			  { "drifting_drag", c.drifting_drag },
			  { "drifting_steer_angle", c.drifting_steer_angle },
			  { "drifting_traction", c.drifting_traction } };
}

void from_json(const json& j, CarController& c) {
	if (j.contains("acceleration")) {
		j.at("acceleration").get_to(c.acceleration);
	} else {
		c.acceleration = 0.0f;
	}
	if (j.contains("drag")) {
		j.at("drag").get_to(c.drag);
	} else {
		c.drag = 0.0f;
	}
	if (j.contains("steer_angle")) {
		c.steer_angle = DegToRad(j.at("steer_angle").template get<float>());
	} else {
		c.steer_angle = 0.0f;
	}
	if (j.contains("traction")) {
		j.at("traction").get_to(c.traction);
	} else {
		c.traction = 0.0f;
	}
	if (j.contains("drifting_drag")) {
		j.at("drifting_drag").get_to(c.drifting_drag);
	} else {
		c.drifting_drag = c.drag;
	}
	if (j.contains("drifting_steer_angle")) {
		c.drifting_steer_angle = DegToRad(j.at("drifting_steer_angle").template get<float>());
	} else {
		c.drifting_steer_angle = c.steer_angle;
	}
	if (j.contains("drifting_traction")) {
		j.at("drifting_traction").get_to(c.drifting_traction);
	} else {
		c.drifting_traction = c.traction;
	}
}

ecs::Entity CreateLight(ecs::Manager& manager, const PointLight& light) {
	auto entity{ manager.CreateEntity() };
	entity.Add<PointLight>(light);
	entity.Add<Visible>();
	return entity;
}

ecs::Entity CreateCar(ecs::Manager& manager, const path& car_json_filepath) {
	std::string_view key{ "car" };
	auto entity{ CreateSprite(manager, key) };
	auto j{ LoadJson(car_json_filepath) };
	auto& transform{ entity.Add<Transform>(j.at("Transform")) };
	auto rb_json{ j.at("RigidBody") };
	auto& rb{ entity.Add<RigidBody>(rb_json) };
	auto& controller{ entity.Add<CarController>(j.at("CarController")) };
	auto& box{ entity.Add<BoxCollider>(entity, game.texture.GetSize(key) * 0.75f, Origin::Center) };
	return entity;
}

void CreateRoad(ecs::Manager& manager, const V2_int& top_left) {
	auto entity = CreateSprite(manager, "road");
	entity.Add<Transform>(top_left);
	entity.Add<Origin>(Origin::TopLeft);
	entity.Add<Depth>(-2);
}

void CreateBuilding(ecs::Manager& manager, const V2_int& top_left) {
	std::string_view key{ "building" };
	auto entity = CreateSprite(manager, key);
	entity.Add<Transform>(top_left);
	entity.Add<Origin>(Origin::TopLeft);
	auto& box = entity.Add<BoxCollider>(entity, game.texture.GetSize(key), Origin::TopLeft);
}

void CreateSkidmark(ecs::Manager& manager, const Transform& car_transform) {
	auto entity = CreateSprite(manager, "skidmark");
	entity.Add<Transform>(car_transform);
	entity.Add<Depth>(-1);
	entity.Add<Tint>();
	// entity.Add<Lifetime>(seconds{ 7 }, true);
	entity.Add<Tween>()
		.During(seconds{ 1 })
		.Reverse()
		.OnUpdate([=](float f) mutable {
			auto& c{ entity.Get<Tint>() };
			c = c.WithAlpha(f);
		})
		.OnComplete([=]() mutable { entity.Destroy(); })
		.Start();
}

void CreateLevel(ecs::Manager& manager, const path& filepath) {
	ForEachPixel(filepath, [&](const V2_int& pixel, auto color) {
		auto pos{ pixel * tile_size };
		if (color == color::White) {
			CreateRoad(manager, pos);
		} else if (color == color::Black) {
			CreateBuilding(manager, pos);
		}
	});
}

class GameScene : public Scene {
public:
	ecs::Entity car;
	ecs::Entity red_light;
	ecs::Entity blue_light;

	void Enter() override {
		camera.primary.SetZoom(zoom);

		game.texture.Load("resources/json/textures.json");

		CreateLevel(manager, "resources/level/map.png");

		manager.Refresh();

		auto zombie = CreateSprite(manager, "zombie");
		zombie.Add<Transform>(V2_float{ 50.0f, 50.0f });

		car = CreateCar(manager, "resources/json/car.json");

		PointLight light1;

		float light_radius{ 150.0f };

		light1.SetRadius(light_radius)
			.SetIntensity(1.0f)
			.SetFalloff(1.0f)
			.SetColor(color::Red)
			.SetAmbientColor(color::LightRed);
		PointLight light2{ light1 };
		light2.SetColor(color::Blue).SetAmbientColor(color::LightBlue);

		red_light = CreateLight(manager, light1);
		red_light.Add<Transform>();

		milliseconds fade_time{ 250 };
		float max_ambient_intensity{ 0.3f };

		red_light.Add<Tween>()
			.During(fade_time)
			.Yoyo()
			.Repeat(-1)
			.OnUpdate([=](float f) mutable {
				red_light.Get<PointLight>().SetRadius(light_radius * f);
				red_light.Get<PointLight>().SetAmbientIntensity(max_ambient_intensity * f);
			})
			.Start();
		blue_light = CreateLight(manager, light2);
		blue_light.Add<Transform>();
		blue_light.Add<Tween>()
			.During(fade_time)
			.Reverse()
			.Repeat(-1)
			.Yoyo()
			.OnUpdate([=](float f) mutable {
				blue_light.Get<PointLight>().SetRadius(light_radius * f);
				blue_light.Get<PointLight>().SetAmbientIntensity(max_ambient_intensity * f);
			})
			.Start();

		camera.primary.StartFollow(car);
	}

	void Update() override {
		auto [texture_key, transform, rb, controller, box] =
			car.Get<TextureKey, Transform, RigidBody, CarController, BoxCollider>();
		V2_float light_offset{ V2_float{ 1.0f, 0.0f }.Rotated(transform.rotation) *
							   game.texture.GetSize(texture_key).y / 2.0f };
		blue_light.Get<Transform>().position =
			camera.primary.TransformToScreen(transform.position + light_offset);
		red_light.Get<Transform>().position =
			camera.primary.TransformToScreen(transform.position + light_offset);
		/*if (game.input.KeyPressed(Key::SPACE)) {
			controller.SetDrifting(true);
		} else {
			controller.SetDrifting(false);
		}*/
		controller.Update(transform, rb, physics.dt());
		// box.rotation = transform.rotation;
		if (controller.IsDrifting()) {
			CreateSkidmark(manager, transform);
			texture_key = "car_drift";
		} else {
			texture_key = "car";
		}
	}
};

int main() {
	game.Init(window_title, window_size, window_color);
	game.scene.Enter<GameScene>("game");
	return 0;
}