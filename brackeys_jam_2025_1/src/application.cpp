#include "protegon/protegon.h"

using namespace ptgn;

constexpr V2_int window_size{ 1280, 720 };
constexpr Color window_color{ color::Transparent };
constexpr const char* window_title{ "Organ Delivery" };
constexpr float zoom{ 4.0f };

struct CarController {
	float move_speed{ 50.0f * 10.0f };
	float max_speed{ 15.0f * 10.0f };
	float drag{ 6.0f };
	float steer_angle{ DegToRad(1.0f) };
	float traction{ 3.0f };

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

ecs::Entity CreateCar(
	ecs::Manager& manager, std::string_view texture_key, const V2_float& position
) {
	auto entity{ CreateSprite(manager, texture_key) };
	auto& transform{ entity.Add<Transform>(position) };
	auto& controller{ entity.Add<CarController>() };
	return entity;
}

class GameScene : public Scene {
public:
	ecs::Entity car;

	void Enter() override {
		camera.primary.SetZoom(zoom);

		game.texture.Load("resources/json/textures.json");

		car = CreateCar(manager, "car", { 0, 0 });

		auto blob = CreateSprite(manager, "blob");
		blob.Add<Transform>(V2_float{ 50.0f, 50.0f });

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