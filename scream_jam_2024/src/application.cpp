#include <functional>
#include <set>
#include <tuple>
#include <type_traits>
#include <vector>

#include "components/collider.h"
#include "components/sprite.h"
#include "components/transform.h"
#include "core/game.h"
#include "core/manager.h"
#include "ecs/ecs.h"
#include "event/key.h"
#include "math/geometry/polygon.h"
#include "math/math.h"
#include "math/vector2.h"
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

constexpr V2_float resolution{ 960, 540 };

class GameScene : public Scene {
public:
	ecs::Manager manager;

	Texture boss1{ "resources/entity/boss1.png" };
	Texture player_walk1{ "resources/entity/player_walk_1.png" };

	ecs::Entity player;
	ecs::Entity platform1;
	ecs::Entity boss1_entity;

	float gravity{ 1000000.0f };
	V2_float player_acceleration{ 500000.0f, 1800000.0f };

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
		entity.Add<BoxCollider>(entity, r.size, r.origin);
		entity.Add<DrawColor>(color::White);
		return entity;
	}

	ecs::Entity CreatePlayer() {
		ecs::Entity entity = manager.CreateEntity();

		entity.Add<Transform>(resolution / 2.0f + V2_float{ 100, 100 });
		auto& rb		= entity.Add<RigidBody>();
		rb.gravity		= gravity;
		rb.max_velocity = 1000.0f;
		rb.drag			= 15.0f;

		auto& cg = entity.Add<ColliderGroup>(entity, manager);
		cg.AddBox(
			  "body", { 70 - 0 * 0, 88 }, 0, { 55, 129 }, Origin::Center, true,
			  [](ecs::Entity e1, ecs::Entity e2) {
				  PTGN_LOG("overlap started between ", e1.GetId(), " and ", e2.GetId());
			  },
			  [](ecs::Entity e1, ecs::Entity e2) {
				  PTGN_LOG("overlap between ", e1.GetId(), " and ", e2.GetId());
				  PTGN_ASSERT(e1.Has<BoxCollider>());
				  auto& b1 = e1.Get<BoxCollider>();
				  PTGN_ASSERT(b1.parent.IsAlive());
				  PTGN_ASSERT(b1.parent.Has<Transform>());
				  PTGN_ASSERT(b1.parent.Has<RigidBody>());
				  PTGN_ASSERT(e2.Has<BoxCollider>());
				  auto& b2			= e2.Get<BoxCollider>();
				  auto intersection = b1.GetAbsoluteRect().Intersects(b2.GetAbsoluteRect());
				  b1.parent.Get<Transform>().position	+= intersection.normal * intersection.depth;
				  b1.parent.Get<RigidBody>().velocity.y	 = 0;
			  },
			  [](ecs::Entity e1, ecs::Entity e2) {
				  PTGN_LOG("overlap stopped between ", e1.GetId(), " and ", e2.GetId());
			  }
		).Add<DrawColor>(color::Purple);
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

		player_rb.acceleration = {};
		if (game.input.KeyPressed(Key::W)) {
			player_rb.acceleration.y = -player_acceleration.y * dt;
		}
		if (game.input.KeyPressed(Key::S)) {
			player_rb.acceleration.y = player_acceleration.y * dt;
		}
		if (game.input.KeyPressed(Key::A)) {
			player_rb.acceleration.x = -player_acceleration.x * dt;
		}
		if (game.input.KeyPressed(Key::D)) {
			player_rb.acceleration.x = player_acceleration.x * dt;
		}
		for (auto [e, t, rb] : manager.EntitiesWith<Transform, RigidBody>()) {
			rb.Update();
			t.position += rb.velocity * game.dt();
		}
		game.collision.Update(manager);
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