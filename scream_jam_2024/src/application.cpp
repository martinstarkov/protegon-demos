#include <functional>
#include <set>
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

struct Hitbox {
	Hitbox() = default;

	Hitbox(const Transform& transform, const BoxCollider& box) :
		transform{ transform }, box{ box } {}

	Transform transform;
	BoxCollider box;
	std::function<void(ecs::Entity, ecs::Entity)> overlap_start_callback;
	std::function<void(ecs::Entity, ecs::Entity)> overlap_end_callback;
	std::function<void(ecs::Entity, ecs::Entity)> overlap_callback;
	Color color{ color::Green };
	std::unordered_set<ecs::Entity> overlaps;

	void Overlap(ecs::Entity parent, ecs::Entity e) {
		PTGN_ASSERT(parent != e, "Cannot overlap with itself");
		if (overlaps.count(e) == 0) {
			overlaps.insert(e);
			if (overlap_start_callback != nullptr) {
				std::invoke(overlap_start_callback, parent, e);
			}
		} else {
			if (overlap_callback != nullptr) {
				std::invoke(overlap_callback, parent, e);
			}
		}
	}

	void Unoverlap(ecs::Entity parent, ecs::Entity e) {
		PTGN_ASSERT(parent != e, "Cannot unoverlap with itself");
		if (overlaps.count(e) == 0) {
			return;
		}
		overlaps.erase(e);
		if (overlap_end_callback != nullptr) {
			std::invoke(overlap_end_callback, parent, e);
		}
	}

	Rect GetRectangle() const {
		return { transform.position, box.size, box.origin };
	}

	void Draw() const {
		game.draw.Rectangle(
			transform.position, box.size, color, box.origin, 1.0f, transform.rotation
		);
	}
};

struct FrameHitbox {
	FrameHitbox() = default;

	FrameHitbox(const std::vector<Hitbox>& hitboxes) : hitboxes{ hitboxes } {}

	// Note that these hitboxes are relative to the sprite.
	// They must be converted to world coordinates.
	std::vector<Hitbox> hitboxes;
};

struct AnimationHitbox {
	AnimationHitbox() = default;

	AnimationHitbox(ecs::Entity parent, const Animation& animation, bool start = false);

	ecs::Entity parent;
	Animation animation;

	std::vector<FrameHitbox> frame_hitboxes;

	AnimationHitbox& AddFrame() {
		frame_hitboxes.emplace_back();
		return *this;
	}

	// Add hitbox to the current frame (last added frame).
	// Rotation in degrees unlike everything else in the engine.
	AnimationHitbox& AddHitbox(
		const V2_float& pos, float rotation_degrees, const V2_float& size,
		const std::function<void(ecs::Entity, ecs::Entity)>& overlap_start_callback = nullptr,
		const std::function<void(ecs::Entity, ecs::Entity)>& overlap_callback		= nullptr,
		const std::function<void(ecs::Entity, ecs::Entity)>& overlap_end_callback	= nullptr
	) {
		PTGN_ASSERT(frame_hitboxes.size() > 0, "Cannot add hitbox before adding frame");
		auto& hitbox = frame_hitboxes.back().hitboxes.emplace_back(
			Transform{ pos, DegToRad(rotation_degrees) }, BoxCollider{ size }
		);
		hitbox.overlap_callback		  = overlap_callback;
		hitbox.overlap_start_callback = overlap_start_callback;
		hitbox.overlap_end_callback	  = overlap_end_callback;
		return *this;
	}

	std::size_t ClampFrame(std::size_t frame) const {
		if (frame >= frame_hitboxes.size()) {
			PTGN_WARN(
				"No hitboxes specified for the given animation frame ", frame,
				" using 0th frame by default"
			);
			frame = 0;
		}
		PTGN_ASSERT(frame < frame_hitboxes.size());
		return frame;
	}

	// Hitboxes relative to the world.
	std::vector<Hitbox> GetUntweenedHitboxes(std::size_t frame) const {
		frame = ClampFrame(frame);
		const auto& frame_hitbox{ frame_hitboxes[frame] };
		PTGN_ASSERT(parent.Has<Transform>());
		const Transform& transform = parent.Get<Transform>();
		Rect rect{ transform.position, animation.sprite_size * transform.scale, animation.origin };
		std::vector<Hitbox> hitboxes;
		hitboxes.reserve(frame_hitbox.hitboxes.size());
		// Convert sprite relative hitboxes to world coordinates.
		for (const auto& hb : frame_hitbox.hitboxes) {
			Hitbox h = hb;
			h.transform =
				Transform{ rect.Min() + (hb.transform.position + hb.box.offset) * transform.scale,
						   transform.rotation + hb.transform.rotation };
			h.box = BoxCollider{ hb.box.size * transform.scale, hb.box.origin };
			hitboxes.emplace_back(h);
		}
		return hitboxes;
	}

	std::vector<Hitbox> tweened_hitboxes;

	std::vector<Hitbox> GetCurrentHitboxes() const {
		return tweened_hitboxes;
	}

	// Update tweened_hitboxes relative to the world and tweening.
	// @param t 0 to 1 value by which the tweening happens.
	void UpdateTweenedHitboxes(float t) {
		PTGN_ASSERT(t >= 0.0f && t <= 1.0f);
		std::size_t current_frame	= animation.GetCurrentFrame();
		std::size_t next_frame		= (current_frame + 1) % animation.GetCount();
		const auto current_hitboxes = GetUntweenedHitboxes(current_frame);
		const auto next_hitboxes	= GetUntweenedHitboxes(next_frame);

		std::vector<Hitbox> tweened;
		tweened.reserve(current_hitboxes.size());

		for (std::size_t i = 0; i < current_hitboxes.size(); i++) {
			const Hitbox& current{ current_hitboxes[i] };

			// Hitboxes are tethered to each other between frames by their index in the hitboxes
			// array. To remove hitboxes for certain frames, simply place an empty entry
			// (dimensionless hitbox).
			if (i >= next_hitboxes.size()) {
				tweened.emplace_back(current);
				continue;
			}

			const Hitbox& next{ next_hitboxes[i] };

			Hitbox tweened_hitbox = current;
			tweened_hitbox.transform.position =
				Lerp(current.transform.position, next.transform.position, t);
			tweened_hitbox.transform.rotation =
				Lerp(current.transform.rotation, next.transform.rotation, t);
			tweened_hitbox.transform.scale = Lerp(current.transform.scale, next.transform.scale, t);
			tweened_hitbox.box.size		   = Lerp(current.box.size, next.box.size, t);

			tweened.emplace_back(tweened_hitbox);
		}

		tweened_hitboxes = tweened;
	}

	std::vector<Hitbox>& GetCurrentHitboxesReference() {
		std::size_t frame = ClampFrame(animation.GetCurrentFrame());
		return frame_hitboxes[frame].hitboxes;
	};

	void DrawCurrentHitboxes() const {
		auto hitboxes = GetCurrentHitboxes(); // GetUntweenedHitboxes(animation.GetCurrentFrame());
		for (const auto& hb : hitboxes) {
			hb.Draw();
		}
	}
};

struct AnimationHitboxMap : public ActiveMapManager<AnimationHitbox> {
	using ActiveMapManager<AnimationHitbox>::ActiveMapManager;

	AnimationHitboxMap(
		const ActiveMapManager<AnimationHitbox>::Key& active_key,
		const ActiveMapManager<AnimationHitbox>::Item& active_item
	) :
		ActiveMapManager<AnimationHitbox>{ active_key, active_item } {}

	void SetActive(const Key& key) {
		GetActive().animation.Stop();
		ActiveMapManager<AnimationHitbox>::SetActive(key);
		GetActive().animation.Start();
	}
};

AnimationHitbox::AnimationHitbox(ecs::Entity parent, const Animation& animation, bool start) :
	parent{ parent }, animation{ animation } {
	this->animation.on_start = [parent]() mutable {
		PTGN_ASSERT(parent.Has<AnimationHitboxMap>());
		parent.Get<AnimationHitboxMap>().GetActive().UpdateTweenedHitboxes(0.0f);
	};
	this->animation.on_update = [parent](float t) mutable {
		PTGN_ASSERT(parent.Has<AnimationHitboxMap>());
		parent.Get<AnimationHitboxMap>().GetActive().UpdateTweenedHitboxes(t);
	};
}

class GameScene : public Scene {
public:
	ecs::Manager manager;

	Texture boss1{ "resources/entity/boss1.png" };
	Texture player_walk1{ "resources/entity/player_walk_1.png" };

	ecs::Entity player;
	ecs::Entity boss1_entity;

	float player_speed{ 100.0f };

	GameScene() {}

	ecs::Entity CreateBoss1() {
		ecs::Entity entity = manager.CreateEntity();
		entity.Add<Transform>(resolution / 2.0f);

		AnimationHitbox anim0 =
			AnimationHitbox{ entity,
							 { player_walk1, { 150, 160 }, 5, milliseconds{ 1000 } },
							 false }
				.AddFrame()
				.AddHitbox({ 70, 88 }, 0.0f, { 55, 129 })
				.AddHitbox({ 74, 27 }, 0.0f, { 144, 14 })
				.AddFrame()
				.AddHitbox({ 70, 88 }, 0.0f, { 55, 129 })
				.AddHitbox({ 211 - 150, 27 }, 0.0f, { 116, 13 })
				.AddFrame()
				.AddHitbox({ 70, 88 }, 0.0f, { 55, 129 })
				.AddHitbox({ 352 - 150 * 2, 28 }, 0.0f, { 103, 11 })
				.AddFrame()
				.AddHitbox({ 70, 88 }, 0.0f, { 55, 129 })
				.AddHitbox({ 498 - 150 * 3, 25 }, 2.77f, { 95, 17 })
				.AddFrame()
				.AddHitbox({ 70, 88 }, 0.0f, { 55, 129 })
				.AddHitbox({ 649 - 150 * 4, 25 }, 0.0f, { 95, 16 });

		AnimationHitbox anim1 =
			AnimationHitbox{ entity, { boss1, { 273, 233 }, 2, milliseconds{ 100 } }, true }
				.AddFrame()
				.AddHitbox({ 195, 116 }, -5.0f, { 142, 214 })
				.AddHitbox({ 67, 129 }, 2.0f, { 119, 55 })
				.AddFrame()
				.AddHitbox({ 195, 116 }, -5.0f, { 142, 214 })
				.AddHitbox({ 70, 64 }, 0.0f, { 102, 54 });

		auto& anim = entity.Add<AnimationHitboxMap>("anim1", anim1);
		anim.GetActive().animation.Start();
		anim.Load("anim0", anim0);
		return entity;
	}

	ecs::Entity CreatePlayer() {
		ecs::Entity entity = manager.CreateEntity();
		entity.Add<Transform>(resolution / 2.0f + V2_float{ 100, 100 });

		auto player_body_callback = [](ecs::Entity e, ecs::Entity e2) {
			PTGN_ASSERT(e2.Has<Transform>());
			PTGN_LOG("Player body collided with entity position: ", e2.Get<Transform>().position);
		};

		AnimationHitbox anim0 =
			AnimationHitbox{ entity,
							 { player_walk1, { 150, 160 }, 5, milliseconds{ 1000 } },
							 false }
				.AddFrame()
				.AddHitbox({ 70, 88 }, 0.0f, { 55, 129 }, player_body_callback)
				.AddHitbox({ 74, 27 }, 0.0f, { 144, 14 })
				.AddFrame()
				.AddHitbox({ 70, 88 }, 0.0f, { 55, 129 }, player_body_callback)
				.AddHitbox({ 211 - 150, 27 }, 0.0f, { 116, 13 })
				.AddFrame()
				.AddHitbox({ 70, 88 }, 0.0f, { 55, 129 }, player_body_callback)
				.AddHitbox({ 352 - 150 * 2, 28 }, 0.0f, { 103, 11 })
				.AddFrame()
				.AddHitbox({ 70, 88 }, 0.0f, { 55, 129 }, player_body_callback)
				.AddHitbox({ 498 - 150 * 3, 25 }, 2.77f, { 95, 17 })
				.AddFrame()
				.AddHitbox({ 70, 88 }, 0.0f, { 55, 129 }, player_body_callback)
				.AddHitbox({ 649 - 150 * 4, 25 }, 0.0f, { 95, 16 });

		auto& anim = entity.Add<AnimationHitboxMap>("player_walk1", anim0);
		anim.GetActive().animation.Start();
		return entity;
	}

	void Init() override {
		manager.Clear();
		player		 = CreatePlayer();
		boss1_entity = CreateBoss1();
		manager.Refresh();
	}

	void Update() override {
		PTGN_ASSERT(boss1_entity.Has<AnimationHitboxMap>());
		PTGN_ASSERT(player.Has<Transform>());

		if (game.input.KeyPressed(Key::W)) {
			player.Get<Transform>().position.y -= player_speed * dt;
		}
		if (game.input.KeyPressed(Key::S)) {
			player.Get<Transform>().position.y += player_speed * dt;
		}
		if (game.input.KeyPressed(Key::A)) {
			player.Get<Transform>().position.x -= player_speed * dt;
		}
		if (game.input.KeyPressed(Key::D)) {
			player.Get<Transform>().position.x += player_speed * dt;
		}

		if (game.input.KeyDown(Key::B)) {
			boss1_entity.Get<AnimationHitboxMap>().SetActive("anim1");
		}
		if (game.input.KeyDown(Key::N)) {
			boss1_entity.Get<AnimationHitboxMap>().SetActive("anim0");
		}
		auto hitbox_entities = manager.EntitiesWith<Transform, AnimationHitboxMap>();
		for (auto [e, t, a] : hitbox_entities) {
			auto& ah		  = a.GetActive();
			auto& hitbox_refs = ah.GetCurrentHitboxesReference();
			for (std::size_t i = 0; i < hitbox_refs.size(); i++) {
				hitbox_refs[i].color = color::Green;
			}
		}
		for (auto [e, t, a] : hitbox_entities) {
			auto& ah		  = a.GetActive();
			auto hitboxes	  = ah.GetCurrentHitboxes();
			auto& hitbox_refs = ah.GetCurrentHitboxesReference();
			PTGN_ASSERT(hitboxes.size() == hitbox_refs.size());
			for (auto [e2, t2, a2] : hitbox_entities) {
				if (e2 == e) {
					continue;
				}
				auto& ah2		   = a2.GetActive();
				auto hitboxes2	   = ah2.GetCurrentHitboxes();
				auto& hitbox_refs2 = ah2.GetCurrentHitboxesReference();
				PTGN_ASSERT(hitboxes2.size() == hitbox_refs2.size());
				for (std::size_t i = 0; i < hitboxes.size(); i++) {
					Hitbox& hitbox1{ hitboxes[i] };
					Rect r{ hitbox1.GetRectangle() };
					for (std::size_t j = 0; j < hitboxes2.size(); j++) {
						Hitbox& hitbox2{ hitboxes2[j] };
						Rect r2{ hitbox2.GetRectangle() };
						if (e.IsAlive() && e2.IsAlive() && r.Overlaps(r2)) {
							hitbox_refs[i].color  = color::Red;
							hitbox_refs2[j].color = color::Red;
							hitbox_refs[i].Overlap(e, e2);
							hitbox_refs2[j].Overlap(e2, e);
						} else {
							hitbox_refs[i].Unoverlap(e, e2);
							hitbox_refs2[j].Unoverlap(e2, e);
						}
					}
				}
			}
		}
		Draw();
	}

	void Draw() {
		for (auto [e, t, a] : manager.EntitiesWith<Transform, AnimationHitboxMap>()) {
			const auto& ah = a.GetActive();
			ah.animation.Draw(
				e, { t.position, ah.animation.GetSource().size, ah.animation.origin }, t.rotation
			);
			ah.DrawCurrentHitboxes();
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