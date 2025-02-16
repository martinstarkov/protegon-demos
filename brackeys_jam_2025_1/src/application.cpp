#include "protegon/protegon.h"

using namespace ptgn;

constexpr V2_int window_size{ 1280, 720 };
constexpr V2_float tile_size{ 128, 128 };
constexpr Color window_color{ color::Transparent };
constexpr const char* window_title{ "Organ Delivery" };
constexpr float zoom{ 3.0f };

struct CarController {
	float acceleration{ 0.0f };
	float max_speed{ 0.0f };
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

	void Update(Transform& transform) {
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

		float steer_input{ dir.y };
		float forward_input{ dir.x };

		auto forward_vector = [&](const Transform& t) {
			return V2_float{ forward_input, 0.0f }.Rotated(t.rotation);
		};

		velocity_		   += forward_vector(transform) * acceleration * game.dt();
		transform.position += velocity_ * game.dt();

		float steer{ drifting_ ? drifting_steer_angle : steer_angle };

		transform.rotation += steer_input * velocity_.Magnitude() * steer * game.dt();

		transform.rotation = ClampAngle2Pi(transform.rotation);

		/*	PTGN_LOG(
				"transform.rotation: ", transform.rotation,
				", forward vector: ", forward_vector(transform)
			);*/

		float d{ drifting_ ? drifting_drag : drag };

		velocity_ *= 1.0f / (1.0f + d * game.dt());
		velocity_  = Clamp(velocity_, -max_speed, max_speed);

		float trac{ drifting_ ? drifting_traction : traction };

		auto norm_vel{ velocity_.Normalized() };
		auto forward_vec{ forward_vector(transform) };

		auto current_drift_angle{ forward_vec.Angle(norm_vel) };

		drifting_ = current_drift_angle >= DegToRad(10.0f);

		velocity_ = velocity_.Magnitude() * Lerp(norm_vel, forward_vec, trac * game.dt());

		// PTGN_LOG("velocity: ", velocity_);
	}

private:
	bool drifting_{ false };
	V2_float velocity_;
};

void to_json(json& j, const CarController& c) {
	j = json{ { "acceleration", c.acceleration },
			  { "max_speed", c.max_speed },
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
	if (j.contains("max_speed")) {
		j.at("max_speed").get_to(c.max_speed);
	} else {
		c.max_speed = 0.0f;
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

ecs::Entity CreateCar(ecs::Manager& manager, const path& car_json_filepath) {
	auto entity{ CreateSprite(manager, "car") };
	auto j{ LoadJson(car_json_filepath) };
	auto& transform{ entity.Add<Transform>(j.at("Transform")) };
	auto& controller{ entity.Add<CarController>(j.at("CarController")) };
	return entity;
}

void CreateRoad(ecs::Manager& manager, const V2_int& top_left) {
	auto entity = CreateSprite(manager, "road");
	entity.Add<Transform>(top_left);
	entity.Add<Origin>(Origin::TopLeft);
}

void CreateBuilding(ecs::Manager& manager, const V2_int& top_left) {
	auto entity = CreateSprite(manager, "building");
	entity.Add<Transform>(top_left);
	entity.Add<Origin>(Origin::TopLeft);
}

void CreateSkidmark(ecs::Manager& manager, const Transform& car_transform) {
	auto entity = CreateSprite(manager, "skidmark");
	entity.Add<Transform>(car_transform);
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

	void Enter() override {
		camera.primary.SetZoom(zoom);

		game.texture.Load("resources/json/textures.json");

		CreateLevel(manager, "resources/level/map.png");

		manager.Refresh();

		auto zombie = CreateSprite(manager, "zombie");
		zombie.Add<Transform>(V2_float{ 50.0f, 50.0f });

		car = CreateCar(manager, "resources/json/car.json");

		camera.primary.StartFollow(car);
	}

	void Update() override {
		auto [texture_key, transform, controller] = car.Get<TextureKey, Transform, CarController>();
		/*if (game.input.KeyPressed(Key::SPACE)) {
			controller.SetDrifting(true);
		} else {
			controller.SetDrifting(false);
		}*/
		controller.Update(transform);
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