#include "audio/audio.h"
#include "components/draw.h"
#include "components/generic.h"
#include "components/movement.h"
#include "core/entity.h"
#include "core/game.h"
#include "core/manager.h"
#include "core/transform.h"
#include "math/collision/collider.h"
#include "math/noise.h"
#include "math/vector2.h"
#include "physics/rigid_body.h"
#include "protegon/protegon.h"
#include "renderer/color.h"
#include "renderer/origin.h"
#include "scene/camera.h"
#include "scene/scene.h"
#include "scene/scene_manager.h"
#include "ui/button.h"
#include "utility/time.h"

using namespace ptgn;

constexpr V2_int window_size{ 1280, 720 };
constexpr float camera_zoom{ 4.0f };
constexpr Color color1{ 206, 79, 25, 255 };

constexpr int walk_volume{ 30 };
constexpr int repair_volume{ 30 };
constexpr int music_volume{ 50 };
constexpr int wind_volume{ 20 };
constexpr std::size_t walk_sound_frequency{ 2 }; // every second repeat of the walk animation

constexpr CollisionCategory player_category{ 0 };
constexpr CollisionCategory interaction_category{ 1 };

struct WalkRepeats : public ArithmeticComponent<int> {
	using ArithmeticComponent::ArithmeticComponent;
};

class GameScene : public Scene {
public:
	FractalNoise fractal_noise;

	GameScene() {
		LoadResources("resources/data/resources.json");
	}

	Entity CreatePlayer() {
		auto entity = manager.CreateEntity();

		V2_float player_starting_position{ 0.0f, 0.0f };

		entity.Add<Transform>(player_starting_position);
		auto& rb = entity.Add<RigidBody>();
		entity.Add<Enabled>();

		V2_float hitbox_size{ 10, 6 };
		V2_float hitbox_offset{ 0, 8 };

		auto body_hitbox = manager.CreateEntity();
		body_hitbox.Add<BoxCollider>(hitbox_size, Origin::CenterBottom);
		body_hitbox.Add<Transform>(hitbox_offset);

		auto interaction_hitbox = manager.CreateEntity();
		interaction_hitbox.Add<BoxCollider>(V2_float{ 28, 28 }, Origin::Center);
		interaction_hitbox.Add<Transform>(V2_float{});

		entity.AddChild("body", body_hitbox);
		entity.AddChild("interaction", interaction_hitbox);

		auto& movement = entity.Add<TopDownMovement>();

		// Maximum movement speed.
		movement.max_speed = 0.7f * 60.0f;
		// How fast to reach max speed.
		movement.max_acceleration = 20.0f * 60.0f;
		// How fast to stop after letting go.
		movement.max_deceleration = 20.0f * 60.0f;
		// How fast to stop when changing direction.
		movement.max_turn_speed = 60.0f * 60.0f;

		movement.friction = 1.0f;

		V2_uint animation_count{ 4, 3 };
		V2_int player_size		= { 12, 24 };
		V2_float animation_size = player_size;
		milliseconds animation_duration{ 1000 };

		entity.Add<WalkRepeats>();

		auto& anim_map = entity.Add<AnimationMap>(
			"down",
			CreateAnimation(
				manager, "player_anim", animation_count.x, animation_size, animation_duration
			)
		);
		auto& a0 = anim_map.GetActive();
		auto& a1 = anim_map.Load(
			"right", CreateAnimation(
						 manager, "player_anim", animation_count.x, animation_size,
						 animation_duration, V2_float{ 0, animation_size.y }
					 )
		);
		auto& a2 = anim_map.Load(
			"up", CreateAnimation(
					  manager, "player_anim", animation_count.x, animation_size, animation_duration,
					  V2_float{ 0, 2.0f * animation_size.y }
				  )
		);

		auto on_repeat = [=]() {
			auto& repeats{ entity.Get<WalkRepeats>() };
			++repeats.GetValue();
			bool repeat{ repeats % walk_sound_frequency == 0 };
			if (!repeat) {
				return;
			}
			game.sound.Play("walk");
		};

		a0.SetParent(entity);
		a1.SetParent(entity);
		a2.SetParent(entity);

		a0.Add<callback::AnimationRepeat>(on_repeat);
		a1.Add<callback::AnimationRepeat>(on_repeat);
		a2.Add<callback::AnimationRepeat>(on_repeat);

		movement.on_move_start = [=]() {
			entity.Get<AnimationMap>().GetActive().Get<Tween>().Start(false);
		};
		movement.on_direction_change = [=](MoveDirection) {
			auto& a{ entity.Get<AnimationMap>() };
			auto dir{ entity.Get<TopDownMovement>().GetDirection() };
			auto& prev_active{ a.GetActive() };
			bool active_changed{ false };

			switch (dir) {
				case MoveDirection::Down:	   active_changed = a.SetActive("down"); break;
				case MoveDirection::Up:		   active_changed = a.SetActive("up"); break;
				case MoveDirection::Left:	   [[fallthrough]];
				case MoveDirection::DownLeft:  [[fallthrough]];
				case MoveDirection::UpLeft:	   [[fallthrough]];
				case MoveDirection::UpRight:   [[fallthrough]];
				case MoveDirection::DownRight: [[fallthrough]];
				case MoveDirection::Right:	   active_changed = a.SetActive("right"); break;
				default:					   break;
			}
			if (active_changed) {
				prev_active.Get<Tween>().Reset();
			}
			auto& current_active{ a.GetActive() };
			current_active.Get<Tween>().Start(false);
		};
		movement.on_move_stop = [=]() {
			entity.Get<AnimationMap>().GetActive().Get<Tween>().Reset();
		};

		return entity;
	}

	Entity player;

	void Enter() override {
		fractal_noise.SetOctaves(2);
		fractal_noise.SetFrequency(0.055f);
		fractal_noise.SetLacunarity(5);
		fractal_noise.SetPersistence(3);

		player = CreatePlayer();

		camera.primary.SetZoom(camera_zoom);

		auto ship1 = CreateSprite(manager, "ship");
		ship1.Add<Transform>();

		game.sound.SetVolume("wind", wind_volume);
		game.sound.Play("wind", 0);
		game.sound.SetVolume("walk", walk_volume);
		game.sound.SetVolume("repair", repair_volume);
	}

	void Update() override {
		auto& player_pos = player.Get<Transform>().position;

		if (player.Get<TopDownMovement>().keys_enabled) {
			camera.primary.SetPosition(player_pos);
		}
	}
};

class MainMenu : public Scene {
public:
	Button play;

	MainMenu() {}

	void Enter() override {}

	void Update() override {}
};

int main([[maybe_unused]] int c, [[maybe_unused]] char** v) {
	game.Init("One Bit Jam", window_size, color1);
	game.scene.Enter<GameScene>("game");
	return 0;
}