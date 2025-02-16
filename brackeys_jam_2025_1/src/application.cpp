#include "protegon/protegon.h"

using namespace ptgn;

constexpr V2_int window_size{ 1280, 720 };
constexpr V2_float tile_size{ 128, 128 };
constexpr Color window_color{ color::Transparent };
constexpr const char* window_title{ "Organ Delivery" };
constexpr float zoom{ 3.0f };

struct CarController {
	float move_speed{ 0.0f };
	float max_speed{ 0.0f };
	float drag{ 0.0f };
	float steer_angle{ 0.0f };
	float traction{ 0.0f };

	Key forward_key{ Key::W };
	Key reverse_key{ Key::S };
	Key left_key{ Key::A };
	Key right_key{ Key::D };

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
		float forard_input{ dir.x };

		auto forward_vector = [](const Transform& t) {
			return V2_float{ 1.0f, 0.0f }.Rotated(t.rotation);
		};

		move_force		   += forward_vector(transform) * move_speed * forard_input * game.dt();
		transform.position += move_force * game.dt();

		transform.rotation += steer_input * move_force.Magnitude() * steer_angle * game.dt();

		transform.rotation = ClampAngle2Pi(transform.rotation);

		PTGN_LOG(
			"transform.rotation: ", transform.rotation,
			", forward vector: ", forward_vector(transform)
		);

		move_force *= 1.0f / (1.0f + drag * game.dt());
		move_force	= Clamp(move_force, -max_speed, max_speed);

		move_force = move_force.Magnitude() *
					 Lerp(move_force.Normalized(), forward_vector(transform), traction * game.dt());
	}

private:
	V2_float move_force;
};

void to_json(json& j, const CarController& c) {
	j = json{ { "move_speed", c.move_speed },
			  { "max_speed", c.max_speed },
			  { "drag", c.drag },
			  { "steer_angle", c.steer_angle },
			  { "traction", c.traction } };
}

void from_json(const json& j, CarController& c) {
	if (j.contains("move_speed")) {
		j.at("move_speed").get_to(c.move_speed);
	} else {
		c.move_speed = 0.0f;
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
}

ecs::Entity CreateCar(
	ecs::Manager& manager, std::string_view texture_key, const path& car_json_filepath
) {
	auto entity{ CreateSprite(manager, texture_key) };
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

		car = CreateCar(manager, "car", "resources/json/car.json");

		camera.primary.StartFollow(car);
	}

	void Update() override {
		for (auto [e, transform, controller] : manager.EntitiesWith<Transform, CarController>()) {
			controller.Update(transform);
		}
	}
};

int main() {
	game.Init(window_title, window_size, window_color);
	game.scene.Enter<GameScene>("game");
	return 0;
}