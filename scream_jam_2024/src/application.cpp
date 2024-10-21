#include <vector>

#include "components/collider.h"
#include "components/sprite.h"
#include "components/transform.h"
#include "core/game.h"
#include "core/manager.h"
#include "ecs/ecs.h"
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

	void Draw(const Color& color) const {
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

	AnimationHitbox(ecs::Entity parent, const Animation& animation) :
		parent{ parent }, animation{ animation } {}

	ecs::Entity parent;
	Animation animation;

	std::vector<FrameHitbox> frame_hitboxes;

	AnimationHitbox& AddFrame() {
		frame_hitboxes.emplace_back();
		return *this;
	}

	// Add hitbox to the current frame (last added frame).
	// Rotation in degrees unlike everything else in the engine.
	AnimationHitbox& AddHitbox(const V2_float& pos, float rotation_degrees, const V2_float& size) {
		PTGN_ASSERT(frame_hitboxes.size() > 0, "Cannot add hitbox before adding frame");
		frame_hitboxes.back().hitboxes.emplace_back(
			Transform{ pos, DegToRad(rotation_degrees) }, BoxCollider{ size }
		);
		return *this;
	}

	// Hitboxes relative to the world.
	std::vector<Hitbox> GetUntweenedHitboxes(std::size_t frame) const {
		if (frame >= frame_hitboxes.size()) {
			PTGN_WARN(
				"No hitboxes specified for the given animation frame ", frame,
				" using 0th frame by default"
			);
			frame = 0;
		}
		PTGN_ASSERT(frame < frame_hitboxes.size());
		const auto& frame_hitbox{ frame_hitboxes[frame] };
		const Transform& transform = parent.Get<Transform>();
		Rect rect{ transform.position, animation.sprite_size * transform.scale, animation.origin };
		std::vector<Hitbox> hitboxes;
		hitboxes.reserve(frame_hitbox.hitboxes.size());
		// Convert sprite relative hitboxes to world coordinates.
		for (const auto& hb : frame_hitbox.hitboxes) {
			hitboxes.emplace_back(
				Transform{ rect.Min() + (hb.transform.position + hb.box.offset) * transform.scale,
						   transform.rotation + hb.transform.rotation },
				BoxCollider{ hb.box.size * transform.scale, hb.box.origin }
			);
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

			Hitbox tweened_hitbox;
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

	void DrawCurrentHitboxes(const Color& color) const {
		auto hitboxes = GetCurrentHitboxes(); // GetUntweenedHitboxes(animation.GetCurrentFrame());
		for (const auto& hb : hitboxes) {
			hb.Draw(color);
		}
	}
};

using AnimationHitboxMap = ActiveMapManager<AnimationHitbox>;

class GameScene : public Scene {
public:
	ecs::Manager manager;

	Texture boss1{ "resources/entity/boss1.png" };

	GameScene() {}

	ecs::Entity CreateBoss1() {
		ecs::Entity entity = manager.CreateEntity();
		entity.Add<Transform>(resolution / 2.0f);
		Animation anim1{ boss1, { 273, 233 }, 2, milliseconds{ 100 } };
		auto& anim = entity.Add<AnimationHitboxMap>(
			"anim1", AnimationHitbox{ entity, anim1 }
						 .AddFrame()
						 .AddHitbox({ 195, 116 }, -5.0f, { 142, 214 })
						 .AddHitbox({ 67, 129 }, 2.0f, { 119, 55 })
						 .AddFrame()
						 .AddHitbox({ 195, 116 }, -5.0f, { 142, 214 })
						 .AddHitbox({ 70, 64 }, 0.0f, { 102, 54 })
		);
		auto& active = anim.GetActive();
		active.animation.Start();
		// active.animation.on_repeat = [&active]() {};
		active.animation.on_update = [&active](float t) {
			active.UpdateTweenedHitboxes(t);
		};
		return entity;
	}

	void Init() override {
		manager.Clear();
		CreateBoss1();
		manager.Refresh();
	}

	void Update() override {
		Draw();
	}

	void Draw() {
		for (auto [e, t, a] : manager.EntitiesWith<Transform, AnimationHitboxMap>()) {
			const auto& ah = a.GetActive();
			ah.animation.Draw(
				e, { t.position, ah.animation.GetSource().size, ah.animation.origin }, t.rotation
			);
			ah.DrawCurrentHitboxes(color::Green);
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