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

int num_directions{ 4 };
constexpr CollisionCategory player_category{ 2 };
constexpr CollisionCategory block_category{ 3 };

// TODO: Fix script inputs being delayed.

float SnapAngle(float angle_rad) {
	float sector_size = two_pi<float> / num_directions;
	angle_rad		  = ClampAngle2Pi(angle_rad);
	// Snap to nearest sector
	int sector_index = static_cast<int>(std::round(angle_rad / sector_size));
	return sector_index * sector_size;
}

V2_float GetRandomEdgePosition(const V2_float& size) {
	static RNG<int> edge_rng{ 0, 3 };
	RNG<float> x_rng{ 0.0f, size.x };
	RNG<float> y_rng{ 0.0f, size.y };
	int edge{ edge_rng() };

	switch (edge) {
		case 0: // Top
			return V2_float{ x_rng(), 0.0f };
		case 1: // Right
			return V2_float{ size.x, y_rng() };
		case 2: // Bottom
			return V2_float{ x_rng(), size.y };
		case 3: // Left
			return V2_float{ 0.0f, y_rng() };
		default: PTGN_ERROR("Invalid edge");
	}
}

class CollisionScript : public Script<CollisionScript> {
	void OnCollisionStart(Collision c);
};

struct Connections {
	std::unordered_set<Entity> connections;
};

class BlockManager {
public:
	std::vector<Entity> GetConnections(const Entity& entity) const {
		std::vector<Entity> result;
		const auto& connections = entity.Get<Connections>().connections;
		result.insert(result.end(), connections.begin(), connections.end());
		return result;
	}

	bool DetectCycleAndColor(Scene& scene) {
		std::unordered_set<Entity> visited;
		std::vector<Entity> cycle_path;
		Entity parent{};

		for (const auto& [entity, connections] : scene.EntitiesWith<Connections>()) {
			if (visited.find(entity) == visited.end()) {
				if (Dfs(entity, parent, visited, cycle_path)) {
					for (Entity& e : cycle_path) {
						e.SetTint(color::Gold);
					}
					return true;
				}
			}
		}

		return false;
	}

	bool Dfs(
		const Entity& current, const Entity& parent, std::unordered_set<Entity>& visited,
		std::vector<Entity>& path
	) {
		visited.insert(current);
		path.push_back(current);

		for (const Entity& neighbor : current.Get<Connections>().connections) {
			if (neighbor == parent) {
				continue;
			}
			if (visited.find(neighbor) != visited.end()) {
				path.push_back(neighbor);
				return true;
			}
			if (Dfs(neighbor, current, visited, path)) {
				return true;
			}
		}

		path.pop_back();
		return false;
	}
};

class GameScene : public Scene {
public:
	BlockManager block_manager;
	V2_int block_size{ 32, 32 };
	float rotation_speed{ DegToRad(200.0f) };
	V2_float center{ resolution / 2.0f };

	Entity central_block;

	Timer spawn_timer;
	milliseconds spawn_delay{ 500 };

	std::unordered_set<V2_int> taken;

	void Enter() override {
		central_block  = CreateRect(*this, center, block_size, color::Blue);
		auto& collider = central_block.Add<BoxCollider>(block_size);
		collider.SetCollisionCategory(player_category);
		central_block.SetInteractive();
		central_block.Enable();
		auto& rb = central_block.Add<RigidBody>();
		central_block.Add<Connections>();
		rb.immovable = true;
		// collider.overlap_only = true;
		spawn_timer.Start();
	}

	Entity CreateBox(const V2_float& position, const V2_float& velocity_target) {
		auto rect			  = CreateRect(*this, position, block_size, color::Red);
		auto& rb			  = rect.Add<RigidBody>();
		V2_float velocity_dir = (velocity_target - position).Normalized();
		rect.Add<Connections>();
		constexpr float max_speed = 100.0f;
		static RNG<float> speed_rng{ max_speed / 2.0f, max_speed };
		rb.velocity = velocity_dir * speed_rng();
		constexpr float angular_max_speed{ DegToRad(200.0f) };
		static RNG<float> angular_rng{ -angular_max_speed, angular_max_speed };
		// rb.angular_velocity = angular_rng();
		rect.Enable();
		Origin origin{ Origin::Center };
		rect.SetOrigin(origin);
		auto& collider = rect.Add<BoxCollider>(block_size, origin);
		collider.SetCollisionCategory(block_category);
		// collider.overlap_only = true;
		rect.AddScript<CollisionScript>();
		return rect;
	}

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
			CreateBox({ resolution.x / 2.0f, 0.0f }, center);
		}
		central_block.SetRotation(rotation);
		// block_manager.DetectCycleAndColor(*this);

		/*	for (auto e : Entities()) {
				if (e.GetId() == 5) {
					auto connections = block_manager.GetConnections(e);
					for (auto e2 : connections) {
						e2.SetTint(color::Pink);
					}
					PTGN_LOG("Connections: ", connections.size());
				}
			}*/
		//  PTGN_LOG("Rotation (deg)", RadToDeg(rotation));
	}

	void Exit() override {}
};

void CollisionScript::OnCollisionStart(Collision c) {
	if (entity.HasParent()) {
		PTGN_LOG("Collided with ", entity.GetId(), " and ", c.entity.GetId());
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
	transform		   = transform.InverseRelativeTo(parent_transform);
	transform.rotation = SnapAngle(transform.rotation);
	V2_int moved_position{ Round(transform.position) };
	V2_int coordinate{ V2_int{ moved_position / entity.Get<Rect>().size } *
					   entity.Get<Rect>().size };
	auto& game_scene{ game.scene.Get<GameScene>("game") };
	int i = 0;
	while (i < 1000 && game_scene.taken.count(coordinate) > 0) {
		moved_position += c.normal * entity.Get<Rect>().size;
		V2_int new_coordinate =
			V2_int{ moved_position / entity.Get<Rect>().size } * entity.Get<Rect>().size;
		while (new_coordinate == coordinate) {
			moved_position += c.normal * entity.Get<Rect>().size;
			new_coordinate =
				V2_int{ moved_position / entity.Get<Rect>().size } * entity.Get<Rect>().size;
		}
		coordinate = new_coordinate;
		i++;
	}
	PTGN_ASSERT(i != 1000);
	game_scene.taken.insert(coordinate);
	transform.position = coordinate;
	entity.SetParent(parent);
}

int main() {
	game.Init(window_title, resolution, window_color);
	game.scene.Enter<GameScene>("game");
	return 0;
}