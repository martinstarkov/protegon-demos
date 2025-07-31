#include <core/entity.h>
#include <core/game.h>
#include <events/input_handler.h>
#include <events/key.h>
#include <math/vector2.h>
#include <physics/collision/collider.h>
#include <physics/rigid_body.h>
#include <rendering/api/color.h>
#include <rendering/api/origin.h>
#include <rendering/graphics/rect.h>
#include <scene/scene.h>
#include <scene/scene_manager.h>

using namespace ptgn;

constexpr V2_int resolution{ 1280, 720 };
constexpr Color window_color{ color::Transparent };
constexpr const char* window_title{ "GMTK Jam 2025" };

constexpr CollisionCategory player_category{ 2 };
constexpr CollisionCategory block_category{ 3 };

// TODO: Fix script inputs being delayed.

V2_float GetRandomEdgePosition(const V2_float& size) {
	static RNG<int> edge_rng{ 0, 3 };
	RNG<float> x_rng{ 0.0f, size.x };
	RNG<float> y_rng{ 0.0f, size.y };
	int edge{ std::invoke(edge_rng) };

	switch (edge) {
		case 0: // Top
			return V2_float{ std::invoke(x_rng), 0.0f };
		case 1: // Right
			return V2_float{ size.x, std::invoke(y_rng) };
		case 2: // Bottom
			return V2_float{ std::invoke(x_rng), size.y };
		case 3: // Left
			return V2_float{ 0.0f, std::invoke(y_rng) };
		default: PTGN_ERROR("Invalid edge");
	}
}

class CollisionScript : public Script<CollisionScript> {
	void OnCollisionStart(Collision c) {
		if (entity.HasParent()) {
			return;
		}
		Entity parent;
		auto category{ c.entity.Get<BoxCollider>().GetCollisionCategory() };
		if (category == block_category) {
			if (c.entity.HasParent()) {
				parent = c.entity.GetParent();
			}
		} else if (category == player_category) {
			parent = c.entity;
		}
		if (!parent) {
			return;
		}
		auto& rb			= entity.Get<RigidBody>();
		rb.velocity			= {};
		rb.angular_velocity = 0.0f;
		auto parent_transform{ parent.GetAbsoluteTransform() };
		auto& transform{ entity.GetTransform() };
		transform = transform.InverseRelativeTo(parent_transform);
		entity.SetParent(parent);
	}
};

class GameScene : public Scene {
public:
	V2_int block_size{ 32, 32 };
	float rotation_speed{ DegToRad(200.0f) };
	int num_directions{ 16 };
	V2_float center{ resolution / 2.0f };

	Entity central_block;

	Timer spawn_timer;
	milliseconds spawn_delay{ 500 };

	void Enter() override {
		central_block  = CreateRect(*this, center, block_size, color::Blue);
		auto& collider = central_block.Add<BoxCollider>(block_size);
		collider.SetCollisionCategory(player_category);
		central_block.SetInteractive();
		central_block.Enable();
		auto& rb	 = central_block.Add<RigidBody>();
		rb.immovable = true;
		// collider.overlap_only = true;
		spawn_timer.Start();
	}

	Entity CreateBox(const V2_float& position, const V2_float& velocity_target) {
		auto rect = CreateRect(*this, position, block_size, color::Red);
		auto& rb  = rect.Add<RigidBody>();

		V2_float velocity_dir	  = (velocity_target - position).Normalized();
		constexpr float max_speed = 100.0f;
		static RNG<float> speed_rng{ max_speed / 2.0f, max_speed };
		rb.velocity = velocity_dir * std::invoke(speed_rng);
		constexpr float angular_max_speed{ DegToRad(200.0f) };
		static RNG<float> angular_rng{ -angular_max_speed, angular_max_speed };
		rb.angular_velocity = std::invoke(angular_rng);
		rect.Enable();
		Origin origin{ Origin::Center };
		rect.SetOrigin(origin);
		auto& collider = rect.Add<BoxCollider>(block_size, origin);
		collider.SetCollisionCategory(block_category);
		// collider.overlap_only = true;
		//  collider.overlap_only = true;
		rect.AddScript<CollisionScript>();
		return rect;
	}

	// float SnapAngle(float angle_rad) {
	//	float sector_size = two_pi<float> / num_directions;
	//	angle_rad = ClampAngle2Pi(angle_rad);
	//	// Snap to nearest sector
	//	int sector_index = static_cast<int>(std::round(angle_rad / sector_size));
	//	return sector_index * sector_size;
	// }

	float rotation{ 0.0f };

	void Update() override {
		auto dt{ game.dt() };
		if (game.input.KeyPressed(Key::D)) {
			rotation += rotation_speed * dt;
		}
		if (game.input.KeyPressed(Key::A)) {
			rotation -= rotation_speed * dt;
		}
		if (spawn_timer.Completed(spawn_delay)) {
			spawn_delay -= milliseconds{ 1 };
			spawn_delay	 = std::max(spawn_delay, milliseconds{ 100 });
			spawn_timer.Start(true);
			CreateBox(GetRandomEdgePosition(resolution), center);
		}
		if (game.input.KeyDown(Key::Space)) {
			CreateBox({ 0.0f, resolution.y / 2.0f }, center);
		}
		central_block.SetRotation(rotation);
		//  PTGN_LOG("Rotation (deg)", RadToDeg(rotation));
	}

	void Exit() override {}
};

int main() {
	game.Init(window_title, resolution, window_color);
	game.scene.Enter<GameScene>("game");
	return 0;
}