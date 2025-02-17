#include "protegon/protegon.h"

using namespace ptgn;

constexpr V2_int window_size{ 1280, 720 };
constexpr V2_float tile_size{ 128, 128 };
constexpr Color window_color{ color::Transparent };
constexpr const char* window_title{ "Organ Delivery" };
constexpr float zoom{ 4.0f };

constexpr CollisionCategory buildings{ 1 };
constexpr CollisionCategory roads{ 2 };
constexpr CollisionCategory zombies{ 3 };
constexpr CollisionCategory player_category{ 4 };

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

		rb.velocity = rb.velocity.Magnitude() * Lerp(norm_vel, forward_vec, trac * dt);

		drifting_ = current_drift_angle >= DegToRad(10.0f);
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

void CreateZombies(
	ecs::Manager& manager, ecs::Entity player, const Circle& spawn_range,
	const V2_float& spawn_frac, float dt
) {
	auto create_zombie = [&](V2_float spawn_point) {
		V2_float dir{ player.Get<Transform>().position - spawn_point };
		std::string_view key{ "zombie" };
		auto entity{ CreateSprite(manager, key) };
		entity.Add<Transform>(spawn_point, dir.Angle());
		entity.Add<Depth>(-1);
		entity.Add<Origin>(Origin::Center);
		auto& box = entity.Add<CircleCollider>(entity, game.texture.GetSize(key).x / 2.0f);
		box.SetCollisionCategory(zombies);
		/*box.AddCollidesWith(player_category);
		box.AddCollidesWith(buildings);*/
		auto& rigid_body = entity.Add<RigidBody>();
		rigid_body.drag	 = 4.0f;
		// TODO: Make this a follow target component.
		float zombie_accel{ 50.0f };
		entity.Add<Tween>()
			.During(milliseconds{ 0 })
			.Repeat(-1)
			.OnUpdate([=]() mutable {
				auto [t, rb]  = entity.Get<Transform, RigidBody>();
				auto& playert = player.Get<Transform>();
				V2_float dir{ playert.position - t.position };
				t.rotation	   = dir.Angle();
				V2_float accel = dir.Normalized() * zombie_accel;
				rb.AddAcceleration(accel, dt);
			})
			.Start();
	};
	// Search for a road in range of the player to spawn the zombie.
	for (auto [e, transform, texture_key, box] :
		 manager.EntitiesWith<Transform, TextureKey, BoxCollider>()) {
		if (box.GetCollisionCategory() != roads) {
			continue;
		}
		Rect r{ box.GetAbsoluteRect() };
		if (!r.Overlaps(spawn_range)) {
			continue;
		}
		V2_float texture_size{ game.texture.GetSize(texture_key) };
		V2_float local_spawn{ texture_size * spawn_frac };
		V2_float spawn_point{ r.Min() + local_spawn };
		std::invoke(create_zombie, spawn_point);
	}
}

ecs::Entity CreateCar(ecs::Manager& manager, const path& car_json_filepath) {
	std::string_view key{ "car" };
	auto entity{ CreateSprite(manager, key) };
	auto j{ LoadJson(car_json_filepath) };
	auto& transform{ entity.Add<Transform>(j.at("Transform")) };
	auto rb_json{ j.at("RigidBody") };
	entity.Add<RigidBody>(rb_json);
	auto& controller{ entity.Add<CarController>(j.at("CarController")) };
	auto& box{ entity.Add<BoxCollider>(entity, game.texture.GetSize(key) * 0.75f, Origin::Center) };
	float kill_speed_threshold{ 40.0f };
	float kill_speed_threshold2{ kill_speed_threshold * kill_speed_threshold };
	box.before_collision = [=](ecs::Entity e1, ecs::Entity e2) {
		if (e2.Has<CircleCollider>() &&
			e2.Get<CircleCollider>().GetCollisionCategory() == zombies) {
			auto& rb = e1.Get<RigidBody>();
			if (rb.velocity.MagnitudeSquared() > kill_speed_threshold2) {
				// TODO: Add tween to destroy the zombie.
				e2.Destroy();
				return false;
			}
		}
		return true;
	};
	return entity;
}

void CreateRoad(ecs::Manager& manager, const V2_int& top_left) {
	std::string_view key{ "road" };
	auto entity = CreateSprite(manager, key);
	entity.Add<Transform>(top_left);
	entity.Add<Origin>(Origin::TopLeft);
	auto& box		 = entity.Add<BoxCollider>(entity, game.texture.GetSize(key), Origin::TopLeft);
	box.enabled		 = false;
	box.overlap_only = true;
	box.SetCollisionCategory(roads);
	entity.Add<Depth>(-2);
}

void CreateBuilding(ecs::Manager& manager, const V2_int& top_left) {
	std::string_view key{ "building" };
	auto entity = CreateSprite(manager, key);
	entity.Add<Transform>(top_left);
	entity.Add<Origin>(Origin::TopLeft);
	auto& box = entity.Add<BoxCollider>(entity, game.texture.GetSize(key), Origin::TopLeft);
	box.SetCollisionCategory(buildings);
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

	RNG<float> spawn_rng;
	Timer spawn_timer;

	void Enter() override {
		camera.primary.SetZoom(zoom);

		game.texture.Load("resources/json/textures.json");

		CreateLevel(manager, "resources/level/map.png");

		manager.Refresh();

		car = CreateCar(manager, "resources/json/car.json");

		float building_padding{ 0.1f };
		PTGN_ASSERT(building_padding >= 0.0f && building_padding < 0.5f);
		spawn_rng = { building_padding, 1.0f - building_padding };

		PointLight light1;

		float light_radius{ 250.0f };

		light1.SetRadius(light_radius).SetIntensity(1.0f).SetFalloff(3.0f).SetColor(color::Red);
		//.SetAmbientColor(color::LightRed);
		PointLight light2{ light1 };
		light2.SetColor(color::Blue); //.SetAmbientColor(color::LightBlue);

		red_light = CreateLight(manager, light1);
		red_light.Add<Transform>();

		milliseconds fade_time{ 250 };
		// float max_ambient_intensity{ 0.3f };

		red_light.Add<Tween>()
			.During(fade_time)
			.Yoyo()
			.Repeat(-1)
			.OnUpdate([=](float f) mutable {
				red_light.Get<PointLight>().SetRadius(light_radius * f);
				// red_light.Get<PointLight>().SetAmbientIntensity(max_ambient_intensity * f);
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
				// blue_light.Get<PointLight>().SetAmbientIntensity(max_ambient_intensity * f);
			})
			.Start();

		camera.primary.SetPosition(car.Get<Transform>().position);
		camera.primary.StartFollow(car);
		camera.primary.SetLerp(V2_float{ 0.2f });
	}

	void Update() override {
		auto [texture_key, transform, rb, controller, box] =
			car.Get<TextureKey, Transform, RigidBody, CarController, BoxCollider>();

		/*camera.primary.SetRotation(Lerp(
			ClampAngle2Pi(camera.primary.GetRotation()),
			ClampAngle2Pi(-half_pi<float> - transform.rotation), 0.1f
		));*/

		float collision_check_dist{ 40.0f };
		float collision_check_dist2{ collision_check_dist * collision_check_dist };
		float zombie_stop_distance{ box.size.x / 2.0f };
		float zombie_stop_distance2{ zombie_stop_distance * zombie_stop_distance };

		for (auto [e, t, zrb, c] : manager.EntitiesWith<Transform, RigidBody, CircleCollider>()) {
			if (c.GetCollisionCategory() != zombies) {
				continue;
			}
			V2_float dir{ transform.position - t.position };
			float dist2{ dir.MagnitudeSquared() };
			if (dist2 < collision_check_dist2) {
				c.enabled = true;
				e.Add<Tint>(color::Green);
				if (dist2 < zombie_stop_distance2) {
					zrb.velocity = {};
				}
			} else {
				c.enabled = false;
				e.Add<Tint>(color::Red);
			}
		}

		V2_float light_offset{ V2_float{ 1.0f, 0.0f }.Rotated(transform.rotation) *
							   game.texture.GetSize(texture_key).y / 2.0f };
		blue_light.Get<Transform>().position =
			camera.primary.TransformToScreen(transform.position + light_offset);
		red_light.Get<Transform>().position =
			camera.primary.TransformToScreen(transform.position + light_offset);
		if (game.input.KeyPressed(Key::SPACE)) {
			controller.SetDrifting(true);
		} /* else {
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

		float spawn_radius{ 128 };

		if (spawn_timer.Completed(milliseconds{ 500 }) || !spawn_timer.IsRunning()) {
			spawn_timer.Start();
			Circle spawn_range{ transform.position, spawn_radius };
			CreateZombies(
				manager, car, spawn_range, { std::invoke(spawn_rng), std::invoke(spawn_rng) },
				physics.dt()
			);
		}
	}
};

int main() {
	game.Init(window_title, window_size, window_color);
	game.scene.Enter<GameScene>("game");
	return 0;
}