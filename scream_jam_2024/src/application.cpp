#include <functional>
#include <set>
#include <tuple>
#include <type_traits>
#include <vector>

#include "components/collider.h"
#include "components/movement.h"
#include "components/sprite.h"
#include "components/transform.h"
#include "core/game.h"
#include "core/manager.h"
#include "ecs/ecs.h"
#include "event/key.h"
#include "math/geometry/polygon.h"
#include "math/math.h"
#include "math/vector2.h"
#include "physics/physics.h"
#include "protegon/protegon.h"
#include "renderer/color.h"
#include "renderer/origin.h"
#include "renderer/texture.h"
#include "scene/scene.h"
#include "scene/scene_manager.h"
#include "utility/debug.h"
#include "utility/log.h"
#include "utility/time.h"

using namespace ptgn;

constexpr CollisionCategory ground_category{ 1 };

constexpr V2_float resolution{ 960, 540 };

class GameScene : public Scene {
public:
	ecs::Manager manager;

	Texture boss1{ "resources/entity/boss1.png" };
	Texture player_walk1{ "resources/entity/player_walk_1.png" };

	ecs::Entity player;
	ecs::Entity platform1;
	ecs::Entity boss1_entity;

	float gravity{ 10000.0f };
	V2_float player_acceleration{ 5000.0f, 5000.0f };

	GameScene() {}

	ecs::Entity CreateBoss1() {
		ecs::Entity entity = manager.CreateEntity();
		entity.Add<Transform>(resolution / 2.0f);

		// Animation anim1{ boss1, { 273, 233 }, 2, milliseconds{ 100 } };
		/*.AddFrame()
		.AddHitbox({ 195, 116 }, -5.0f, { 142, 214 })
		.AddHitbox({ 67, 129 }, 2.0f, { 119, 55 })
		.AddFrame()
		.AddHitbox({ 195, 116 }, -5.0f, { 142, 214 })
		.AddHitbox({ 70, 64 }, 0.0f, { 102, 54 });*/

		/*auto& anim = entity.Add<AnimationHitboxMap>("anim1", anim1);
		anim.GetActive().animation.Start();
		anim.Load("anim0", anim0);*/
		return entity;
	}

	using ColliderNames = std::vector<ColliderGroup::Name>;

	using AnimationHitboxMap = ActiveMapManager<ColliderNames, std::size_t, std::size_t, false>;

	ecs::Entity CreatePlatform(const Rect& r) {
		ecs::Entity entity = manager.CreateEntity();
		entity.Add<Transform>(r.position, r.rotation);
		auto& box = entity.Add<BoxCollider>(entity, r.size, r.origin);
		box.SetCollisionCategory(ground_category);
		entity.Add<DrawColor>(color::White);
		return entity;
	}

	ecs::Entity CreatePlayer() {
		ecs::Entity entity = manager.CreateEntity();

		entity.Add<Transform>(resolution / 2.0f + V2_float{ 100, 100 });
		auto& rb				 = entity.Add<RigidBody>();
		rb.gravity				 = 1.0f;
		auto& m					 = entity.Add<Movement>();
		m.data.run_max_speed	 = 9;
		m.data.run_acceleration	 = 13;
		m.data.run_decceleration = 16;

		rb.drag						  = 0.22f;
		m.data.jump_force			  = 13;
		m.data.jump_cut_gravity		  = 0.4f;
		m.data.coyote_time			  = 0.15f;
		m.data.jump_input_buffer_time = 0.1f;
		m.data.fall_gravity			  = 2.0f;

		auto& cg = entity.Add<ColliderGroup>(entity, manager);
		cg.AddBox(
			  "body", { 70 - 0 * 0, 88 }, 0, { 55, 129 }, Origin::Center, true, 0, {},
			  [](ecs::Entity e1, ecs::Entity e2) {
				  PTGN_LOG("collision started between ", e1.GetId(), " and ", e2.GetId());
			  },
			  [=](ecs::Entity e1, ecs::Entity e2) {
				  if (e2.Get<BoxCollider>().IsCategory(ground_category)) {
					  PTGN_LOG("Grounded");
				  }
			  },
			  [](ecs::Entity e1, ecs::Entity e2) {
				  PTGN_LOG("collision stopped between ", e1.GetId(), " and ", e2.GetId());
			  },
			  nullptr, false, true
		)
			.Add<DrawColor>(color::Purple);
		/*cg.AddBox("head0", { 74 - 0 * 0, 27 }, 0, { 144, 14 }).Add<DrawColor>(color::Green);
		cg.AddBox("head1", { 211 - 150 * 1, 27 }, 0, { 116, 13 }).Add<DrawColor>(color::Green);
		cg.AddBox("head2", { 352 - 150 * 2, 28 }, 0, { 103, 11 }).Add<DrawColor>(color::Green);
		cg.AddBox("head3", { 498 - 150 * 3, 25 }, DegToRad(2.77f), { 95, 17
		}).Add<DrawColor>(color::Green); cg.AddBox("head4", { 649 - 150 * 4, 25 }, 0, { 95, 16
		}).Add<DrawColor>(color::Green);*/

		auto& ahm = entity.Add<AnimationHitboxMap>();
		ahm.Load(0, ColliderNames{ "body" /*, "head0"*/ });
		ahm.Load(1, ColliderNames{ "body" /*, "head1"*/ });
		ahm.Load(2, ColliderNames{ "body" /*, "head2"*/ });
		ahm.Load(3, ColliderNames{ "body" /*, "head3"*/ });
		ahm.Load(4, ColliderNames{ "body" /*, "head4"*/ });

		auto& anim =
			entity.Add<Animation>(player_walk1, V2_float{ 150, 160 }, 5, milliseconds{ 1000 });
		anim.Start();

		return entity;
	}

	void Init() override {
		manager.Clear();
		player = CreatePlayer();
		platform1 =
			CreatePlatform({ { 0, resolution.y - 10 }, { resolution.x, 10 }, Origin::TopLeft });
		boss1_entity = CreateBoss1();
		manager.Refresh();
	}

	void Update() override {
		// PTGN_ASSERT(boss1_entity.Has<AnimationHitboxMap>());
		PTGN_ASSERT(player.Has<Transform>());
		PTGN_ASSERT(player.Has<RigidBody>());
		auto& player_rb = player.Get<RigidBody>();

		float dt = game.physics.dt();

		if (game.input.KeyDown(Key::W)) {
			player_rb.velocity.y += -player_acceleration.y * dt;
		}
		if (game.input.KeyPressed(Key::S)) {
			player_rb.velocity.y += player_acceleration.y * dt;
		}
		if (game.input.KeyPressed(Key::A)) {
			player_rb.velocity.x += -player_acceleration.x * dt;
		}
		if (game.input.KeyPressed(Key::D)) {
			player_rb.velocity.x += player_acceleration.x * dt;
		}
		for (auto [e, t, rb, m] : manager.EntitiesWith<Transform, RigidBody, Movement>()) {
			// m.Update(t, rb);
		}
		for (auto [e, t, rb] : manager.EntitiesWith<Transform, RigidBody>()) {
			rb.Update();
		}
		game.collision.Update(manager);
		for (auto [e, t, rb] : manager.EntitiesWith<Transform, RigidBody>()) {
			t.position += rb.velocity * game.physics.dt();
		}
		Draw();
	}

	void Draw() {
		for (auto [e, anim, t] : manager.EntitiesWith<Animation, Transform>()) {
			anim.Draw(e, t);
		}
		for (auto [e, b] : manager.EntitiesWith<BoxCollider>()) {
			DrawRect(e, b.GetAbsoluteRect());
		}

		// Debug: Draw hitbox colliders.
		for (auto [e, t, anim, ahm, cg] :
			 manager.EntitiesWith<Transform, Animation, AnimationHitboxMap, ColliderGroup>()) {
			std::size_t frame = anim.GetCurrentFrame();
			if (!ahm.Has(frame)) {
				continue;
			}
			auto collider_names{ ahm.Get(frame) };
			for (const auto& name : collider_names) {
				auto rect		   = cg.GetBox(name).GetAbsoluteRect();
				auto hitbot_entity = cg.Get(name);
				DrawRect(hitbot_entity, rect);
			}
		}
	}
};

class SetupScene : public Scene {
public:
	SetupScene() {
		game.window.SetSize(resolution);
		game.window.SetTitle("Game");
		game.draw.SetClearColor(color::Black);
	}

	void Init() override {
		game.scene.Load<GameScene>("game");
		game.scene.AddActive("game");
	}

	void Update() override {}
};

int main() {
	game.Start<SetupScene>();
	return 0;
}