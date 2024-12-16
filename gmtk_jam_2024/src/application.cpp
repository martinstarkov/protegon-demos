#include "protegon/protegon.h"

using namespace ptgn;

constexpr const V2_int resolution{ 960, 540 };
constexpr const V2_int center{ resolution / 2 };
constexpr const bool draw_hitboxes{ true };

constexpr int button_y_offset{ 14 };
constexpr V2_int button_size{ 250, 50 };
constexpr V2_int first_button_coordinate{ 250, 220 };

enum class Difficulty {
	Easy,
	Medium,
	Hard,
};

enum class BubbleAnimation {
	None,
	Food,
	Cleanup,
	Bone,
	Outside,
	Toy,
	Pet,
	Love,
	Anger0,
	Anger1,
	Anger2,
	Anger3,
	Anger4,
	AngerStop,
};

enum class FadeState {
	Win,
	Lose,
	None,
};

struct WallComponent {};

struct FadeComponent {
	FadeComponent(FadeState state) : state{ state } {}

	FadeState state{ FadeState::None };
};

struct Human {};

struct Wife {
	Wife() = default;

	bool returned{ false };
	bool voice_heard{ false };
};

struct ItemComponent {
	ItemComponent() = default;
	bool held{ false };
	BubbleAnimation type{ BubbleAnimation::None };
	float weight_factor{ 1.0f };
};

struct SortByZ {};

struct HandComponent {
	HandComponent(float radius, const V2_float& offset) : radius{ radius }, offset{ offset } {}

	V2_float GetPosition(ecs::Entity e) const {
		const auto& t = e.Get<Transform>();

		return t.position + V2_float{ Sign(t.scale.x) * offset.x, offset.y };
	}

	bool HasItem() const {
		return current_item != ecs::Entity{};
	}

	float GetWeightFactor() const {
		return HasItem() ? weight_factor : 1.0f;
	};

	ecs::Entity current_item;
	V2_float offset;
	// How much slower the player acceleration is when holding an item.
	float weight_factor{ 1.0f };
	float radius{ 0.0f };
};

static bool OutOfBounds(ecs::Entity e, const V2_float& pos, const V2_float& max_bounds) {
	const auto& hitbox = e.Get<BoxCollider>();
	Rect h{ hitbox.GetAbsoluteRect() };
	V2_float min{ h.Min() };
	V2_float max{ h.Max() };
	return min.x < 0 || max.x > max_bounds.x || min.y < 0 || max.y > max_bounds.y;
}

static void ApplyBounds(ecs::Entity e, const V2_float& max_bounds) {
	if (!e.Has<Transform>()) {
		return;
	}
	if (!e.Has<BoxCollider>()) {
		return;
	}
	auto& pos		   = e.Get<Transform>();
	const auto& hitbox = e.Get<BoxCollider>();
	Rect h{ hitbox.GetAbsoluteRect() };
	V2_float min{ h.Min() };
	V2_float max{ h.Max() };
	if (min.x < 0) {
		pos.position.x -= min.x;
	} else if (max.x > max_bounds.x) {
		pos.position.x += max_bounds.x - max.x;
	}
	if (min.y < 0) {
		pos.position.y -= min.y;
	} else if (max.y > max_bounds.y) {
		pos.position.y += max_bounds.y - max.y;
	}
}

auto GetWalls(ecs::Manager& manager) {
	return manager.EntitiesWith<Transform, BoxCollider, WallComponent>();
}

bool IsRequest(BubbleAnimation anim) {
	switch (anim) {
		case BubbleAnimation::Food:	   return true;
		case BubbleAnimation::Cleanup: return true;
		case BubbleAnimation::Bone:	   return true;
		case BubbleAnimation::Pet:	   return true;
		case BubbleAnimation::Toy:	   return true;
		case BubbleAnimation::Outside: return true;
		default:					   return false;
	}
}

struct AngerComponent {
	AngerComponent(BubbleAnimation bubble) : bubble{ bubble } {}

	bool started{ false };
	bool yelling{ false };
	BubbleAnimation bubble;
};

static void SpawnBubbleAnimation(
	ecs::Entity entity, BubbleAnimation bubble_type, std::size_t bubble_tween_key,
	milliseconds popup_duration, milliseconds total_duration, const TweenCallback& start_callback,
	const std::function<V2_float()>& get_pos_callback, const TweenCallback& hold_start_callback,
	const TweenCallback& complete_callback
) {
	if (game.tween.Has(bubble_tween_key)) {
		// Already in the middle of an animation.
		return;
	}

	const int request_animation_count{ 4 };
	const int hold_frame{ request_animation_count - 1 };
	std::size_t bubble_texture_key{ Hash("bubble") + static_cast<std::size_t>(bubble_type) };

	game.tween.Load(bubble_tween_key)
		.During(popup_duration)
		.OnStart(start_callback)
		.OnUpdate([=](auto& tw, auto f) {
			Texture t{ game.texture.Get(bubble_texture_key) };
			V2_float source_size{ t.GetSize() /
								  V2_float{ static_cast<float>(request_animation_count), 1.0f } };

			float column = std::floorf(f * hold_frame);

			V2_float source_pos = { column * source_size.x, 0.0f };

			// TODO: Move this to draw functions and use an entity instead.
			game.draw.Texture(
				t, Rect{ get_pos_callback(), source_size, entity.Get<Origin>(), 0.0f },
				{ source_pos, source_size, entity.Get<SpriteFlip>() }, { 1.0f, 0 }
			);
		})
		.During(total_duration - popup_duration)
		.OnStart(hold_start_callback)
		.OnUpdate([=](auto& tw, auto f) {
			Texture t{ game.texture.Get(bubble_texture_key) };
			V2_float source_size{ t.GetSize() /
								  V2_float{ static_cast<float>(request_animation_count), 1.0f } };

			float column = static_cast<float>(hold_frame);

			V2_float source_pos = { column * source_size.x, 0.0f };

			// TODO: Move this to draw functions and use an entity instead.
			game.draw.Texture(
				t, Rect{ get_pos_callback(), source_size, entity.Get<Origin>(), 0.0f },
				{ source_pos, source_size, entity.Get<SpriteFlip>() }, { 1.0f, 0 }
			);
		})
		.OnComplete(complete_callback)
		.OnStop(complete_callback)
		.Start();
}

struct Dog {
	Timer patience;
	seconds patience_duration{ 5 };
	BubbleAnimation req{ BubbleAnimation::None };

	bool spawn_thingy = false;

	Dog(ecs::Entity e, const V2_float& start_target, std::size_t walk,
		const std::vector<std::size_t>& bark_keys, std::size_t whine_key,
		const V2_float& bark_offset) :
		target{ start_target },
		walk{ walk },
		bark_keys{ bark_keys },
		whine_key{ whine_key },
		bark_offset{ bark_offset } {
		/*for (std::size_t i = 0; i < bark_keys.size(); i++) {
			PTGN_ASSERT(game.sound.Has(bark_keys[i]));
		}*/
	}

	void SpawnRequestAnimation(ecs::Entity e, BubbleAnimation dog_request) {
		std::size_t request_key{ e.GetId() + Hash("request") };
		if (game.tween.Has(request_key)) {
			// Already in the middle of an animation.
			return;
		}
		request = dog_request;

		SpawnBubbleAnimation(
			e, request, request_key, request_animation_popup_duration,
			request_animation_hold_duration + request_animation_popup_duration,
			[=](Tween& tw, float f) mutable { e.Get<Dog>().whined = false; },
			[=]() {
				if (e.IsAlive() && IsRequest(e.Get<Dog>().request)) {
					auto& h{ e.Get<BoxCollider>() };
					auto flip{ e.Get<SpriteFlip>() };
					float sign	= (flip == Flip::Horizontal ? -1.0f : 1.0f);
					auto offset = GetOffsetFromCenter(h.size, e.Get<Origin>());
					V2_float pos{ e.Get<Transform>().position + offset +
								  V2_float{ sign * (h.size.x / 2 + e.Get<Dog>().bark_offset.x),
											-e.Get<Dog>().bark_offset.y } +
								  V2_float{ sign * e.Get<Dog>().request_offset.x,
											-e.Get<Dog>().request_offset.y } };
					return pos;
				} else {
					return V2_float{ -10000, -10000 };
				}
			},
			[=]() mutable {
				// Whine once after request has finished popping up and is just starting its hold
				// phase.
				if (!e.Get<Dog>().whined) {
					Whine();
					e.Get<Dog>().whined = true;
				}
			},
			[](auto& tw, float f) {}
		);
	}

	void SpawnBarkAnimation(ecs::Entity e) {
		std::size_t bark_tween_key{ e.GetId() + Hash("bark") };
		if (game.tween.Has(bark_tween_key)) {
			// Already in the middle of a bark animation.
			return;
		}
		game.tween.Load(bark_tween_key, milliseconds{ 135 })
			.OnUpdate([=](Tween& tw, float f) {
				std::size_t bark_texture_key{ Hash("bark") };
				const int count{ 6 };
				float column = std::floorf(f * (count - 1));
				Texture t{ game.texture.Get(bark_texture_key) };
				V2_float source_size{ t.GetSize() / V2_float{ count, 1 } };
				V2_float source_pos = { column * source_size.x, 0.0f };
				auto& h{ e.Get<BoxCollider>() };
				auto flip{ e.Get<SpriteFlip>() };
				float sign	= (flip == Flip::Horizontal ? -1.0f : 1.0f);
				auto offset = GetOffsetFromCenter(h.size, e.Get<Origin>());
				Rect rect{ h.GetAbsoluteRect() };
				rect.position += V2_float{ sign * (h.size.x / 2 + bark_offset.x), -bark_offset.y };
				rect.size	   = source_size;
				// TODO: Move this to draw functions and use an entity instead.
				game.draw.Texture(t, rect, { source_pos, source_size, flip }, { 1.0f, 0 });
			})
			.Start();
	}

	void Bark(ecs::Entity e) {
		PTGN_ASSERT(bark_keys.size() > 0);
		RNG<std::size_t> bark_rng{ 0, bark_keys.size() - 1 };
		auto bark_index{ bark_rng() };
		PTGN_ASSERT(bark_index < bark_keys.size());
		auto bark_key{ bark_keys[bark_index] };
		PTGN_ASSERT(game.sound.Has(bark_key));
		game.sound.Get(bark_key).Play(-1);
		SpawnBarkAnimation(e);
	}

	void Whine() {
		if (!game.sound.Has(whine_key)) {
			return;
		}
		RNG<int> channel_rng{ 1, 4 };
		game.sound.Get(whine_key).Play(-1);
	}

	void Update(ecs::Entity e, float progress) {
		if (lingering) {
			// TODO: Different animation?
			// dog.Get<::SpriteSheet>().row = X:
			// auto& an{ dog.Get<AnimationComponent>() };
			// an.column = static_cast<int>(an.column_count * f) %
			// an.column_count;
		} else {
			// dog.Get<::SpriteSheet>().row = 0:
			e.Get<Transform>().position = Lerp(start, target, progress);
			ApplyBounds(e, game.texture.Get(Hash("house_background")).GetSize());
			if (draw_hitboxes) {
				game.draw.Line(start, target, color::Purple, 5.0f);
			}
		}
	}

	float neediness{ 0.3f };

	void StartWalk(ecs::Entity e) {
		RNG<int> rng_request{ 0, 6 };
		int index_request = rng_request();
		RNG<float> chance_rng{ 0.0f, 1.0f };
		if (chance_rng() < neediness) {
			BubbleAnimation r = BubbleAnimation::None;
			if (index_request == 0) {
				r = BubbleAnimation::Food;
			} else if (index_request == 1) {
				r = BubbleAnimation::Pet;
			} else if (index_request == 2) {
				r = BubbleAnimation::Toy;
			} else if (index_request == 3) {
				// r = BubbleAnimation::Outside;
			} else if (index_request == 4) {
				r = BubbleAnimation::Cleanup;
			} else if (index_request == 5) {
				r = BubbleAnimation::Bone;
			}
			if (IsRequest(r)) {
				e.Get<Dog>().spawn_thingy = true;
				e.Get<Dog>().req		  = r;
			}
		}

		if (start.IsZero()) {
			start  = e.Get<BoxCollider>().GetAbsoluteRect().position;
			target = start;
		}
		//  Reset lingering state.
		lingering = false;
		SetNewTarget(e);
		// Reset animation.
		e.Get<Animation>().Start();
	}

	void SetNewTarget(ecs::Entity e) {
		start = target;
		V2_float min{ 0.0f, 0.0f };
		V2_float max = game.texture.Get(Hash("house_background")).GetSize();

		float max_length{ (max - min).MagnitudeSquared() };
		PTGN_ASSERT(max_length != 0.0f);

		auto viable_path = [=](const V2_float& vel) mutable {
			auto& hitbox{ e.Get<BoxCollider>() };
			Rect dog_rect{ hitbox.GetAbsoluteRect() };
			for (auto [en, t, b, w] : GetWalls(e.GetManager())) {
				Rect r{ b.GetAbsoluteRect() };
				Raycast c{ dog_rect.Raycast(vel, r) };
				if (c.Occurred()) {
					return false;
				}
			}
			return true;
		};

		V2_float potential_velocity;
		float potential_heading{ heading };

		Gaussian<float> distance_rng{ 0.0f, 1.0f };
		RNG<float> run_rng{ 0.0f, 1.0f };

		bool run{ run_rng() >= (1.0f - run_chance) };

		float run_multiplier = (run ? run_factor : 1.0f);

		const std::size_t max_attempts{ 1000 };

		bool found_path{ false };

		for (std::size_t i = 0; i < max_attempts; i++) {
			V2_float potential_dir{ V2_float::RandomHeading() };
			potential_velocity =
				potential_dir * distance_rng() * max_walk_distance * run_multiplier;
			V2_float future_loc = start + potential_velocity;
			if (viable_path(potential_velocity) && !OutOfBounds(e, future_loc, max)) {
				potential_target  = future_loc;
				found_path		  = true;
				potential_heading = ClampAngle2Pi(potential_dir.Angle());
				break;
			}
		}

		auto& tween{ game.tween.Get(walk) };

		RNG<float> linger_rng{ 0.0f, 1.0f };

		// Dog has a certaian chance not to take the identified path.
		lingering = linger_rng() >= (1.0f - linger_chance);
		heading	  = potential_heading;

		if (!found_path || lingering) {
			target = start;
			PTGN_ASSERT(linger_duration > milliseconds{ 1 });
			tween.SetDuration(linger_duration);
			if (!found_path) {
				// PTGN_LOG("Dog found no path");
			} else {
				// PTGN_LOG("Dog chose to linger");
			}
		} else {
			target = start + potential_velocity;
			if (target.x < start.x) {
				e.Get<SpriteFlip>() = Flip::Horizontal;
			} else {
				e.Get<SpriteFlip>() = Flip::None;
			}

			float length{ (target - start).MagnitudeSquared() };

			float speed_ratio = std::sqrt(length / max_length);

			if (run) {
				speed_ratio /= run_factor;
			}

			duration<float> path_duration{ diagonal_time * speed_ratio };

			if (NearlyEqual(path_duration.count(), 0.0f)) {
				path_duration = milliseconds{ 100 };
			}

			PTGN_ASSERT(path_duration > microseconds{ 100 });
			tween.SetDuration(std::chrono::duration_cast<milliseconds>(path_duration));
		}
	}

	void Pause() const {
		game.tween.Get(walk).Pause();
	}

	void Resume() const {
		game.tween.Get(walk).Resume();
	}

	bool whined{ false };

	V2_float request_offset{ -11, 11 };

	BubbleAnimation request{ BubbleAnimation::None };

	// For debugging purposes.
	V2_float potential_target;

	// Pixels from corner of hitbox that the mouth of the dog is.
	V2_float bark_offset;
	seconds request_animation_hold_duration{ 7 };
	milliseconds request_animation_popup_duration{ 200 };

	float max_walk_distance{ 300.0f };
	float run_factor{ 5.0f };

	// chance that the dog will multiply their max_walk_distance by run_factor and divide
	// diagonal time by run_factor.
	float run_chance{ 0.3f };
	// % chance  that the dog will wait instead of walking.
	float linger_chance{ 0.3f };
	seconds linger_duration{ 3 };
	bool lingering{ false };

	float heading{ 0.0f };

	int animations_to_goal{ 1 };

	seconds diagonal_time{ 100 };
	std::vector<std::size_t> bark_keys;
	std::size_t whine_key;
	std::size_t walk;
	V2_float target;
	V2_float start;
};

class GameScene : public Scene {
public:
	ecs::Manager manager;

	V2_int tile_size{ V2_int{ 16, 16 } };
	V2_int grid_size{ V2_int{ 60, 34 } };

	constexpr static const float zoom{ 2.0f };
	const float player_accel{ 1000.0f };
	const float player_max_speed{ 70.0f };

	Key item_interaction_key{ Key::E };

	Surface level;

	Texture house_background;
	Texture progress_bar_texture;
	Texture progress_car_texture;
	Texture dog_counter_texture;
	Texture barkometer_texture;

	Timer return_timer;
	Timer bark_timer;

	float bark_count{ 0.0f };
	float bark_threshold{ 60.0f };

	ecs::Entity player;
	ecs::Entity bowl;
	ecs::Entity dog_toy1;
	OrthographicCamera player_camera;
	OrthographicCamera neighbor_camera;

	V2_float world_bounds;
	std::size_t basic_font	 = Hash("basic_font");
	std::size_t counter_font = basic_font;

	Tween fade_to_black;
	ecs::Entity fade_entity;

	Difficulty difficulty;

	seconds level_time{ 6 };

	seconds dog_spawn_rate{ 10 };

	seconds bark_reset_time{ 3 };

	ecs::Entity neighbor;
	V2_float neighbor_camera_pos{ 150, 0.0f };
	V2_float neighbor_walk_end_pos{ 150, 360.0f };

	Texture win{ "resources/ui/win.png" };
	Texture lose{ "resources/ui/lose.png" };
	Texture player_texture{ "resources/entity/player.png" };

	// V2_float neighbor_pos{ 150, 0.0f };

	std::array<std::string_view, 3> yell_keys{ "neighbor_yell0", "neighbor_yell1",
											   "neighbor_yell2" };

	~GameScene() {
		game.tween.Clear();
	}

	GameScene(Difficulty difficulty) : difficulty{ difficulty } {
		switch (difficulty) {
			case Difficulty::Easy: {
				level_time		= seconds{ 80 };
				dog_spawn_rate	= seconds{ 6 };
				bark_reset_time = seconds{ 4 };
				bark_threshold	= 40.0f;
				break;
			};
			case Difficulty::Medium: {
				level_time		= seconds{ 120 };
				dog_spawn_rate	= seconds{ 4 };
				bark_reset_time = seconds{ 4 };
				bark_threshold	= 30.0f;
				break;
			};
			case Difficulty::Hard: {
				level_time		= seconds{ 180 };
				dog_spawn_rate	= seconds{ 2 };
				bark_reset_time = seconds{ 4 };
				bark_threshold	= 20.0f;
				break;
			};
			default: PTGN_ERROR("Failed to identify difficulty");
		}

		game.font.Load(basic_font, "resources/font/retro_gaming.ttf", 32);
		progress_bar_texture = Texture{ "resources/ui/progress_bar.png" };
		progress_bar_texture = Texture{ "resources/ui/progress_bar.png" };
		progress_car_texture = Texture{ "resources/ui/progress_car.png" };
		dog_counter_texture	 = Texture{ "resources/ui/dog_counter.png" };
		barkometer_texture	 = Texture{ "resources/ui/barkometer.png" };
		level				 = Surface{ "resources/level/house_hitbox.png" };

		game.texture.Load("bark", "resources/entity/bark.png");

		auto load_request = [&](BubbleAnimation animation_type, const std::string& name) {
			path p{ "resources/ui/bubble_" + name + ".png" };
			PTGN_ASSERT(FileExists(p));
			game.texture.Load(Hash("bubble") + static_cast<std::size_t>(animation_type), p);
		};
		load_request(BubbleAnimation::Food, "food");
		load_request(BubbleAnimation::Cleanup, "cleanup");
		load_request(BubbleAnimation::Bone, "bone");
		load_request(BubbleAnimation::Outside, "outside");
		load_request(BubbleAnimation::Toy, "toy");
		load_request(BubbleAnimation::Pet, "pet");
		load_request(BubbleAnimation::Love, "love");
		load_request(BubbleAnimation::Anger0, "anger0");
		load_request(BubbleAnimation::Anger1, "anger1");
		load_request(BubbleAnimation::Anger2, "anger2");
		load_request(BubbleAnimation::Anger3, "anger3");
		load_request(BubbleAnimation::Anger4, "anger4");

		house_background = game.texture.Load("house_background", "resources/level/house.png");
		world_bounds	 = house_background.GetSize();
		game.sound.Load("vizsla_bark1", "resources/sound/bark_1.ogg");
		game.sound.Load("vizsla_bark2", "resources/sound/bark_2.ogg");
		game.sound.Load("great_dane_bark1", "resources/sound/bark_1.ogg");
		game.sound.Load("great_dane_bark2", "resources/sound/bark_2.ogg");
		game.sound.Load("maltese_bark1", "resources/sound/small_bark.ogg");
		game.sound.Load("maltese_bark2", "resources/sound/small_bark.ogg");
		game.sound.Load("dachshund_bark1", "resources/sound/small_bark.ogg");
		game.sound.Load("dachshund_bark2", "resources/sound/small_bark.ogg");
		game.sound.Load("vizsla_whine", "resources/sound/dog_whine.ogg");
		game.sound.Load("great_dane_whine", "resources/sound/dog_whine.ogg");
		game.sound.Load("maltese_whine", "resources/sound/small_dog_whine.ogg");
		game.sound.Load("dachshund_whine", "resources/sound/small_dog_whine.ogg");
		game.sound.Load("neighbor_yell0", "resources/sound/yell0.ogg");
		game.sound.Load("neighbor_yell1", "resources/sound/yell1.ogg");
		game.sound.Load("neighbor_yell2", "resources/sound/yell2.ogg");
		game.sound.Load("door_close", "resources/sound/door_close.ogg");
		game.sound.Load("door_open", "resources/sound/door_open.ogg");
		game.sound.Load("wife_arrives", "resources/sound/wife_arrives.ogg");
	}

	void CreatePlayer() {
		player = manager.CreateEntity();

		auto& ppos		= player.Add<Transform>(V2_float{ 215, 290 });
		auto& rb		= player.Add<RigidBody>();
		rb.max_velocity = player_max_speed;
		/*player.Add<Human>();
		player.Add<Wife>();*/

		V2_uint animation_count{ 4, 3 };
		V2_int animation_size{ 16, 32 };

		auto& anim_map = player.Add<AnimationMap>(
			"down",
			Animation{ player_texture, animation_count.x, animation_size, milliseconds{ 400 } }
		);
		anim_map.Load(
			"right", Animation{ player_texture,
								animation_count.x,
								animation_size,
								milliseconds{ 400 },
								{ 0.0f, static_cast<float>(animation_size.y) } }
		);
		anim_map.Load(
			"up", Animation{ player_texture,
							 animation_count.x,
							 animation_size,
							 milliseconds{ 400 },
							 { 0.0f, 2.0f * animation_size.y } }
		);

		auto& box = player.Add<BoxCollider>(player, V2_int{ 8, 8 });
		player.Add<HandComponent>(8.0f, V2_float{ 8.0f, -2.0f * tile_size.y * 0.3f });
		// player.Add<SortByZ>();
		// player.Add<LayerInfo>(0.0f, 0);

		manager.Refresh();
	}

	/*
	ecs::Entity CreateDog(
		const V2_float& pos, const path& texture, const V2_float& hitbox_size,
		const V2_float& hitbox_offset, V2_int animation_count,
		const std::vector<std::size_t>& bark_sound_keys, std::size_t whine_sound_key,
		const V2_float& bark_offset
	) {
		auto dog = manager.CreateEntity();

		Texture t{ texture };

		V2_int texture_size{ t.GetSize() / animation_count };

		V2_float size{ texture_size };

		auto start_walk = [=](Tween& tw, float f) mutable {
			dog.Get<Dog>().StartWalk(dog);
		};

		auto& tween =
			game.tween.Load(Hash(dog), seconds{ 1 })
				.Repeat(-1)
				.OnPause([=](Tween& tw, float f) mutable { dog.Get<Animation>().Start(); })
				.OnStart(start_walk)
				.OnResume(start_walk)
				.OnRepeat(start_walk)
				.OnUpdate([=](Tween& tw, float f) mutable { dog.Get<Dog>().Update(dog, f); });

		dog.Add<Transform>(pos);
		dog.Add<Animation>(
			t, t.GetSize() / animation_count, animation_count.x, milliseconds{ 2000 }
		);
		dog.Add<Dog>(dog, V2_float{}, Hash(dog), bark_sound_keys, whine_sound_key, bark_offset);
		auto& b	 = dog.Add<BoxCollider>(dog, hitbox_size, Origin::CenterBottom);
		b.offset = hitbox_offset;
		dog.Add<SortByZ>();
		dog.Add<LayerInfo>(0.0f, 0);
		auto& rb		= dog.Add<RigidBody>();
		rb.max_velocity = 700.0f;
		dog.Add<BoxCollider>();
		dog.Add<SpriteFlip>(Flip::None);

		manager.Refresh();
		tween.Start();

		return dog;
	}

	ecs::Entity CreateItem(
		const V2_float& pos, const path& texture, float hitbox_scale = 1.0f,
		float weight_factor = 1.0f, BubbleAnimation type = BubbleAnimation::None
	) {
		auto item = manager.CreateEntity();

		Texture t{ texture };
		V2_int texture_size{ t.GetSize() };

		auto& i			= item.Add<ItemComponent>();
		i.type			= type;
		i.weight_factor = weight_factor;
		V2_float size{ texture_size };
		item.Add<BoxCollider>(item, size, Origin::Center);
		item.Add<Transform>(pos);
		item.Add<Sprite>(t);
		item.Add<SortByZ>();
		item.Add<LayerInfo>(0.0f, 0);
		auto& rb		= item.Add<RigidBody>();
		rb.max_velocity = 700.0f;
		item.Add<BoxCollider>();

		manager.Refresh();

		return item;
	}

	void CreateWall(const V2_int& hitbox_size, const V2_int& cell) {
		auto wall = manager.CreateEntity();
		wall.Add<WallComponent>();
		wall.Add<Transform>(cell * hitbox_size);
		wall.Add<BoxCollider>(wall, hitbox_size, Origin::TopLeft);

		manager.Refresh();
	}

	// Tween camera_motion;

	void CreateVizsla(const V2_float& pos) {
		auto e = CreateDog(
			pos, "resources/dog/vizsla.png", V2_float{ 22, 7 }, V2_float{ -3, 0 }, V2_int{ 6, 1 },
			{ Hash("vizsla_bark1"), Hash("vizsla_bark2") }, Hash("vizsla_whine"), V2_float{ 8, 17 }
		);
	}

	void CreateGreatDane(const V2_float& pos) {
		CreateDog(
			pos, "resources/dog/great_dane.png", V2_float{ 34, 8 }, V2_float{ -4, 0 },
			V2_int{ 6, 1 }, { Hash("great_dane_bark1"), Hash("great_dane_bark2") },
			Hash("great_dane_whine"), V2_float{ 11, 24 }
		);
	}

	void CreateMaltese(const V2_float& pos) {
		CreateDog(
			pos, "resources/dog/maltese.png", V2_float{ 11, 6 }, V2_float{ -2, 0 }, V2_int{ 4, 1 },
			{ Hash("maltese_bark1"), Hash("maltese_bark2") }, Hash("maltese_whine"),
			V2_float{ 6, 5 }
		);
	}

	void CreateDachshund(const V2_float& pos, const std::string& suffix) {
		path p{ "resources/dog/dachshund_" + suffix + ".png" };
		PTGN_ASSERT(FileExists(p), "Could not find specified dachshund type");
		CreateDog(
			pos, p, V2_float{ 14, 5 }, V2_float{ -3, 0 }, V2_int{ 4, 1 },
			{ Hash("dachshund_bark1"), Hash("dachshund_bark2") }, Hash("dachshund_whine"),
			V2_float{ 7, 3 }
		);
	}

	bool player_can_move{ true };

	ecs::Entity fade_text;
	ecs::Entity daughter;

	V2_float daughter_camera_pos{ 140, 0.0f };
	V2_float daughter_walk_end_pos{ 140, 238.0f };

	seconds daughter_spawn_rate{ 4 };

	Timer spawn_timer;

	void SpawnDog(const V2_float& pos) {
		RNG<int> dog_rng{ 0, 3 };
		int dog_index = dog_rng();
		if (dog_index == 0) {
			CreateVizsla(pos);
		} else if (dog_index == 1) {
			CreateGreatDane(pos);
		} else if (dog_index == 2) {
			CreateMaltese(pos);
		} else if (dog_index == 3) {
			CreateDachshund(pos, "purple");
		}
	}

	void CreateDaughter() {
		daughter = manager.CreateEntity();

		// DAUGHTER ANIMATION COUNT
		V2_int daughter_animation_count{ 4, 2 };

		Texture girl_texture{ "resources/entity/little_girl.png" };

		daughter_camera_pos.y = game.window.GetSize().y + 100.0f;

		// Must be added before AnimationComponent as it pauses the animation immediately.
		auto& spritesheet = daughter.Add<Animation>(
			girl_texture, girl_texture.GetSize() / daughter_animation_count,
			daughter_animation_count.x, milliseconds{ 200 }
		);
		spritesheet.Start();

		V2_float daughter_start_pos{ daughter_camera_pos.x,
									 daughter_camera_pos.y + spritesheet.source_size.y };

		auto& ppos		= daughter.Add<Transform>(daughter_start_pos);
		auto& rb		= daughter.Add<RigidBody>();
		rb.max_velocity = 700.0f;
		daughter.Add<Origin>(Origin::CenterBottom);
		daughter.Add<SpriteFlip>(Flip::None);
		daughter.Add<Human>();
		daughter.Add<SpriteTint>(color::White);

		V2_float neighbot_walk_start_pos = ppos.position;

		std::size_t love_key{ daughter.GetId() + Hash("love") };

		milliseconds bubble_pop_duration{ 300 };

		float love_bubble_offset{ 2.0f };

		auto daughter_complain_bubble = [=](BubbleAnimation anger,
											milliseconds bubble_hold_duration) {
			SpawnBubbleAnimation(
				daughter, anger, love_key, bubble_pop_duration,
				bubble_hold_duration + bubble_pop_duration, [=](Tween& tw, float f) {},
				[=]() {
					V2_float pos =
						daughter.Get<Transform>().position -
						V2_float{ 0.0f, daughter.Get<BoxCollider>().size.y + love_bubble_offset };
					return pos;
				},
				[=]() {},
				[=](auto& tw, float f) {
					daughter.Get<Animation>().Resume();
					// game.tween.Get(Hash("daughter_animation")).Resume();
				}
			);
		};

		const float anger_bubble_count{ 5.0f };

		daughter.Add<AngerComponent>(BubbleAnimation::Love);

		game.tween.Load(Hash("daughter_animation"), daughter_spawn_rate)
			.Yoyo()
			.Repeat(-1)
			.OnUpdate([=](Tween& tw, auto f) {
				V2_float start = neighbot_walk_start_pos;
				V2_float end   = daughter_walk_end_pos;

				if (f > 0.5f) {
					daughter.Get<Animation>().= 0;
					// daughter.Get<SpriteFlip>() = Flip::None;
					end	  = neighbot_walk_start_pos;
					start = daughter_walk_end_pos;
					daughter.Get<AnimationComponent>().Resume();
				} else {
					daughter.Get<::SpriteSheet>().row = 1;
					//  daughter.Get<SpriteFlip>() = Flip::Vertical;
					//   game.tween.Get(Hash("daughter_animation")).Pause();
					daughter.Get<AnimationComponent>().Pause();
					daughter_complain_bubble(
						daughter.Get<AngerComponent>().bubble, milliseconds{ 2000 }
					);
				}
				daughter.Get<Transform>().position = Lerp(start, end, (1.0f - f));
			})
			.Start();

		V2_int size{ tile_size.x, 26 };

		auto& s = daughter.Add<Size>(size);
		V2_float hitbox_size{ 8, 8 };
		daughter.Add<Hitbox>(daughter, hitbox_size, V2_float{ 0.0f, 0.0f });
		auto& box  = daughter.Add<BoxCollider>();
		box.size   = hitbox_size;
		box.offset = { -4, -8 };
		box.origin = Origin::TopLeft;
		daughter.Add<LayerInfo>(1.0f, 0);

		manager.Refresh();
	};

	*/

	void Init() final {
		CreatePlayer();

		player_camera = game.camera.Load("player_camera");
		player_camera.SetSize(game.window.GetSize());
		player_camera.SetZoom(zoom);
		game.camera.SetPrimary("player_camera");
		player_camera.SetBounds({ {}, world_bounds, Origin::TopLeft });
		player_camera.SetPosition(player.Get<Transform>().position);

		neighbor_camera = game.camera.Load("neighbor_camera");
		neighbor_camera.SetSize(game.window.GetSize());

		/*
		level.ForEachPixel([&](const V2_int& cell, const Color& color) {
			if (color == color::Black) {
				CreateWall(V2_int{ 8, 8 }, cell);
			}
		});

		bowl = CreateItem(
			{ 155, 150 }, "resources/entity/bowl.png", 1.0f, 0.7f, BubbleAnimation::Food
		);
		bowl = CreateItem(
			{ 216, 228 }, "resources/entity/bowl.png", 1.0f, 0.7f, BubbleAnimation::Food
		);
		bowl = CreateItem(
			{ 531, 110 }, "resources/entity/bowl.png", 1.0f, 0.7f, BubbleAnimation::Food
		);
		dog_toy1 = CreateItem(
			{ 300, 110 }, "resources/entity/dog_toy1.png", 1.0f, 0.9f, BubbleAnimation::Toy
		);
		dog_toy1 = CreateItem(
			{ 372, 143 }, "resources/entity/dog_toy1.png", 1.0f, 0.9f, BubbleAnimation::Toy
		);
		CreateItem(
			{ 300, 110 }, "resources/entity/cleanup.png", 1.0f, 1.0f, BubbleAnimation::Cleanup
		);
		CreateItem(
			{ 538, 280 }, "resources/entity/cleanup.png", 1.0f, 1.0f, BubbleAnimation::Cleanup
		);
		CreateItem({ 448, 354 }, "resources/entity/bone.png", 1.0f, 1.0f, BubbleAnimation::Bone);
		CreateItem({ 316, 89 }, "resources/entity/bone.png", 1.0f, 1.0f, BubbleAnimation::Bone);
		CreateItem({ 250, 290 }, "resources/entity/dog_toy2.png", 1.0f, 1.0f, BubbleAnimation::Toy);
		CreateItem({ 100, 140 }, "resources/entity/dog_toy2.png", 1.0f, 1.0f, BubbleAnimation::Toy);

		Rect garden{ V2_float{ 347, 332 }, V2_float{ 568, 368 } - V2_float{ 347, 332 },
					 Origin::TopLeft };
		std::size_t flowers = 10;
		RNG<int> flower_ring{ 0, 2 };

		// for (size_t i = 0; i < flowers; i++) {
		//	V2_float pppoooss = V2_float::Random(garden.Min(), garden.Max());
		//	int index		  = flower_ring();
		//	CreateItem(
		//		pppoooss, "resources/entity/flower" + std::to_string(index) + ".png", 1.0f, 1.0f,
		//		BubbleAnimation::Outside
		//	);
		// }

		fade_entity = manager.CreateEntity();
		fade_entity.Add<SpriteTint>(Color{ 0, 0, 0, 0 });

		fade_text = manager.CreateEntity();
		fade_text.Add<SpriteTint>(Color{ 255, 255, 255, 0 });
		fade_text.Add<FadeComponent>(FadeState::None);

		milliseconds background_fade_duration{ 500 };
		seconds text_fade_duration{ 3 };

		auto tint_change = [=](float f) {
			fade_text.Get<SpriteTint>().a = static_cast<std::uint8_t>(f * 255.0f);
		};

		auto completed = [=]() {
			BackToMenu();
		};

		game.tween
			.Load(Hash("fade_to_black"))

			.During(background_fade_duration)
			.OnUpdate([=](float f) {
				fade_entity.Get<SpriteTint>().a = static_cast<std::uint8_t>(f * 255.0f);
			})

			.During(text_fade_duration / 2)
			.OnStart([]() { game.sound.Get(Hash("door_close")).Play(-1); })
			.OnUpdate(tint_change)

			.During(text_fade_duration / 2)
			.Reverse()
			.OnUpdate(tint_change)
			.OnComplete(completed)
			.OnStop(completed);

		// TODO: Move elsewhere, perhaps react to some event.
		return_timer.Start();
		bark_timer.Start();

		CreateDaughter();

		spawn_timer.Start();
		*/
	}

	void PlayerMovementInput() {
		if (!player.Has<RigidBody>()) {
			return;
		}

		PTGN_ASSERT(player.Has<Transform>());
		PTGN_ASSERT(player.Has<HandComponent>());
		PTGN_ASSERT(player.Has<AnimationMap>());

		auto& t			 = player.Get<Transform>();
		auto& rb		 = player.Get<RigidBody>();
		const auto& hand = player.Get<HandComponent>();
		auto& anim		 = player.Get<AnimationMap>();

		bool up{ game.input.KeyPressed(Key::W) };
		bool down{ game.input.KeyPressed(Key::S) };
		bool right{ game.input.KeyPressed(Key::D) };
		bool left{ game.input.KeyPressed(Key::A) };

		bool movement{ up || down || right || left };

		V2_float d;

		if (up) {
			d.y = -1.0f;
		} else if (down) {
			d.y = 1.0f;
		} else {
			d.y			  = 0.0f;
			rb.velocity.y = 0;
		}

		if (left) {
			d.x = -1.0f;
		} else if (right) {
			d.x = 1.0f;
		} else {
			d.x			  = 0.0f;
			rb.velocity.x = 0;
		}

		if (left || right) {
			anim.SetActive("right");
		} else if (up) {
			anim.SetActive("up");
		} else if (down) {
			anim.SetActive("down");
		}

		if (d.x < 0.0f) {
			t.scale.x = -1.0f * FastAbs(t.scale.x);
		} else if (d.x > 0.0f) {
			t.scale.x = FastAbs(t.scale.x);
		}

		if (auto& active_anim{ anim.GetActive() }; movement && !active_anim.IsRunning()) {
			active_anim.Start();
		} else if (!movement) {
			active_anim.Reset();
			active_anim.Stop();
		}

		rb.AddAcceleration(d.Normalized() * player_accel * hand.GetWeightFactor());
	}

	void UpdatePhysics() {
		game.physics.Update(manager);
		ApplyBounds(player, world_bounds);
	}

	/*
	void UpdatePlayerHand() {
		auto& hand	 = player.Get<HandComponent>();
		auto& hitbox = player.Get<Hitbox>();
		Circle circle{
			hand.GetPosition(player),
			hand.radius,
		};
		if (game.input.KeyUp(item_interaction_key)) {
			if (!hand.HasItem()) {
				for (auto [e, p, h, o, i] :
					 manager.EntitiesWith<Transform, PickupHitbox, Origin, ItemComponent>()) {
					Rect r{ h.GetPosition(), h.size, o };
					if (draw_hitboxes) {
						r.Draw(color::Red, 1.0f);
					}
					if (circle.Overlaps(r)) {
						// hitbox.color	  = color::Red;
						// h.color			  = color::Red;
						hand.current_item = e;
					}
				}
				if (hand.HasItem()) {
					auto& item{ hand.current_item.Get<ItemComponent>() };
					hand.weight_factor = item.weight_factor;
					item.held		   = true;
				}
			} else {
				PTGN_ASSERT(hand.current_item.Has<ItemComponent>());
				hand.current_item.Get<ItemComponent>().held = false;
				const auto& dir								= player.Get<Direction>().dir;
				auto& h_item{ hand.current_item.Get<Hitbox>() };
				auto& o_item{ hand.current_item.Get<Origin>() };

				auto h_pos{ h_item.GetPosition() };
				Rect r_item{ { h_pos.x - hand.offset.y, h_pos.y }, h_item.size, o_item };

				auto& item_rb{ hand.current_item.Get<RigidBody>() };
				auto& rb{ player.Get<RigidBody>() };
				if (FastAbs(rb.velocity.x) == 0.0f && FastAbs(rb.velocity.y) == 0.0f) {
					// Throw item from ground (at low velocities).
				} else {
					// Throw item from hand.
					hand.current_item.Get<Transform>().position.y += hand.offset.y;
				}
				item_rb.velocity = rb.velocity.Normalized() * rb.max_velocity * 0.65f;
				//  TODO: Add effect to throw item in direction player is facing.
				hand.current_item = {};
			}
		}
		if (hand.HasItem()) {
			hand.current_item.Get<Transform>().position = player.Get<Hitbox>().GetPosition();
			if (player.Get<Direction>().dir.y == -1) {
				player.Get<LayerInfo>().z_index			   = 0.1f;
				hand.current_item.Get<LayerInfo>().z_index = 0.0f;
			} else {
				player.Get<LayerInfo>().z_index			   = 0.0f;
				hand.current_item.Get<LayerInfo>().z_index = 0.1f;
			}
		}
	}
	*/

	void Update() final {
		if (!game.tween.Has("neighbor_cutscene")) {
			//  Camera follows the player.
			player_camera.SetPosition(player.Get<Transform>().position);
		}

		if (game.input.KeyDown(Key::ESCAPE)) {
			BackToMenu();
		}

		DrawBackground();

		PlayerMovementInput();

		UpdatePhysics();

		/*if (player_can_move) {
			UpdatePlayerHand();
		}*/

		/*float dist{ (daughter.Get<Transform>().position - daughter_walk_end_pos).Magnitude() };

		if (spawn_timer.ElapsedPercentage(dog_spawn_rate) >= 1.0f && dist < 60.0f) {
			spawn_timer.Start();
			SpawnDog(daughter.Get<Transform>().position);
		}*/

		Draw();
	}

	/*void DrawWalls() {
		for (auto [e, p, s, h, origin, w] :
			 manager.EntitiesWith<Transform, Size, Hitbox, Origin, WallComponent>()) {
			Rect r{ p.position, h.size, origin };
			r.Draw(h.color, 1.0f);
		}
	}*/

	/*void DrawItems() {
		for (auto [e, p, s, h, o, ss, item] :
			 manager.EntitiesWith<Transform, Size, Hitbox, Origin, ::SpriteSheet, ItemComponent>(
			 )) {
			if (draw_hitboxes) {
				Rect r{ p.position + h.offset, h.size, o };
				r.Draw(h.color, 1.0f);
			}
			V2_float offset;
			if (item.held) {
				auto& hand{ player.Get<HandComponent>() };
				offset = { player.Get<Direction>().dir.x * hand.offset.x, hand.offset.y };
			}
			game.draw.Texture(
				ss.texture, { p.position + offset, s.s, Origin::Center },
				{ ss.source_pos, ss.source_size, Flip::None },
				e.Has<LayerInfo>() ? e.Get<LayerInfo>() : LayerInfo{}
			);
		}
	}*/

	// void DrawDogs() {
	//	for (auto [e, p, s, o, ss, dog] :
	//		 manager.EntitiesWith<Transform, Size, Origin, ::SpriteSheet, Dog>()) {
	//		game.draw.Texture(
	//			ss.texture, { p.position, s.s, o },
	//			{ ss.source_pos, ss.source_size, e.Has<Flip>() ? e.Get<SpriteFlip>() : Flip::None },
	//			e.Has<LayerInfo>() ? e.Get<LayerInfo>() : LayerInfo{} //, Color{ 255, 255, 255, 30 }
	//		);
	//	}
	// }

	void DrawAnimations() {
		for (const auto [e, anim] : manager.EntitiesWith<Animation>()) {
			anim.Draw(e);
		}
		for (const auto [e, anim_map] : manager.EntitiesWith<AnimationMap>()) {
			anim_map.Draw(e);
		}
	}

	void DrawBackground() const {
		house_background.Draw({ {}, house_background.GetSize(), Origin::TopLeft });
	}

	/*void WifeReturn() {
		player_can_move = false;
		game.tween.Get(Hash("player_movement_animation")).Pause();
		player.Remove<RigidBody>();
		player.Get<AnimationComponent>().column = 0;
		player.Get<::SpriteSheet>().row			= 0;

		milliseconds wife_sound_duration{ 600 };
		milliseconds wife_return_duration{ 2400 };
		milliseconds love_popup_dur{ 400 };
		milliseconds love_duration{ wife_return_duration };

		game.tween.Load(Hash("wife_return_tween"))
			.During(wife_sound_duration)
			.OnStart([]() { game.sound.Get(Hash("door_open")).Play(-1); })
			.During(wife_return_duration)
			.OnUpdate([=](auto& tw, auto f) {
				if (!player.Get<Wife>().voice_heard) {
					player.Get<Wife>().voice_heard = true;

					SpawnBubbleAnimation(
						player, BubbleAnimation::Love, Hash(player) + Hash("bubble"),
						love_popup_dur, love_duration, [=]() {},
						[=]() {
							V2_float pos = player.Get<Transform>().position -
										   V2_float{ 0.0f, player.Get<Size>().s.y + 1.0f };
							return pos;
						},
						[=]() {}, [=]() {}
					);

					game.sound.Get(Hash("wife_arrives")).Play(-1);
				}
			})
			.OnComplete([=]() {
				fade_text.Get<FadeComponent>().state = FadeState::Win;
				game.tween.Get(Hash("fade_to_black")).Start();
			})
			.Start();
	}*/

	/*void DrawProgressBar() {
		V2_float cs{ game.camera.GetPrimary().GetSize() };
		float y_offset{ 0.0f };
		V2_float bar_size{ progress_bar_texture.GetSize() };
		V2_float bar_pos{ cs.x / 2.0f, y_offset };
		game.draw.Texture(progress_bar_texture, { bar_pos, bar_size, Origin::CenterTop });
		V2_float car_size{ progress_car_texture.GetSize() };
		V2_float start{ bar_pos.x - bar_size.x / 2.0f + car_size.x / 2,
						bar_pos.y + bar_size.y / 2.0f };
		V2_float end{ bar_pos.x + bar_size.x / 2.0f - car_size.x / 2,
					  bar_pos.y + bar_size.y / 2.0f };
		float elapsed{ return_timer.ElapsedPercentage(level_time) };
		if (elapsed >= 1.0f && !player.Get<Wife>().returned && player.Has<RigidBody>()) {
			player.Get<Wife>().returned = true;
			WifeReturn();
		}
		V2_float car_pos{ Lerp(start, end, elapsed) };
		game.draw.Texture(progress_car_texture, { car_pos, car_size, Origin::Center });
	}*/

	// void DrawDogCounter() {
	//	std::size_t dog_count = manager.EntitiesWith<Dog>().Count();
	//	V2_float cs{ game.camera.GetPrimary().GetSize() };
	//	V2_float ui_offset{ -12.0f, 12.0f };
	//	V2_float counter_size{ dog_counter_texture.GetSize() };
	//	V2_float text_offset{ -counter_size.x / 2,
	//						  counter_size.y - 14.0f }; // relative to counter ui top right
	//	Text t{ std::to_string(dog_count), color::Black, counter_font };
	//	V2_float pos{ cs.x + ui_offset.x, ui_offset.y };
	//	V2_float counter_text_size{ 20, 25 };
	//	game.draw.Texture(dog_counter_texture, { pos, counter_size, Origin::TopRight });
	//	t.Draw({ pos + text_offset, counter_text_size, Origin::Center });
	// }

	/*void NeighborYell() {
		for (std::size_t i = 0; i < yell_keys.size(); i++) {
			PTGN_ASSERT(game.sound.Has(yell_keys[i]));
		}
		PTGN_ASSERT(yell_keys.size() > 0);
		std::size_t yell_key = yell_keys[0];
		if (yell_keys.size() > 1) {
			RNG<std::size_t> yell_rng{ 0, yell_keys.size() - 1 };
			auto yell_index{ yell_rng() };
			PTGN_ASSERT(yell_index < yell_keys.size());
			yell_key = yell_keys[yell_index];
		}
		PTGN_ASSERT(game.sound.Has(yell_key));
		game.sound.Get(yell_key).Play(-1);
	}*/

	// void CreateNeighbor(milliseconds scene_duration) {
	//	neighbor = manager.CreateEntity();

	//	V2_int neighbor_animation_count{ 4, 1 };

	//	Texture neighbor_texture{ "resources/entity/neighbor.png" };

	//	// Must be added before AnimationComponent as it pauses the animation immediately.
	//	auto& spritesheet =
	//		neighbor.Add<::SpriteSheet>(neighbor_texture, V2_int{}, neighbor_animation_count);

	//	V2_float neighbor_start_pos{ neighbor_camera_pos.x,
	//								 neighbor_camera_pos.y + spritesheet.source_size.y };

	//	auto& ppos		= neighbor.Add<Transform>(neighbor_start_pos);
	//	auto& rb		= neighbor.Add<RigidBody>();
	//	rb.max_velocity = 700.0f;
	//	neighbor.Add<Origin>(Origin::CenterBottom);
	//	neighbor.Add<SpriteFlip>(Flip::None);
	//	neighbor.Add<Human>();
	//	neighbor.Add<SpriteTint>(color::White);

	//	milliseconds animation_duration{ 400 };

	//	game.tween.Load(Hash("neighbor_movement_animation"), animation_duration)
	//		.Repeat(-1)
	//		.OnPause([=]() { neighbor.Get<AnimationComponent>().column = 0; })
	//		.OnUpdate([=](float v) {
	//			neighbor.Get<AnimationComponent>().column =
	//				static_cast<int>(std::floorf(neighbor_animation_count.x * v));
	//		})
	//		.OnRepeat([=]() { neighbor.Get<AnimationComponent>().column = 0; })
	//		.Start();

	//	auto& anim = neighbor.Add<AnimationComponent>(
	//		Hash("neighbor_movement_animation"), neighbor_animation_count.x, animation_duration
	//	);

	//	V2_float neighbot_walk_start_pos = ppos.position;

	//	std::size_t anger_key{ neighbor.GetId() + Hash("anger") };

	//	milliseconds bubble_pop_duration{ 300 };

	//	float yell_bubble_offset{ 2.0f };

	//	auto neighbor_complain_bubble = [=](BubbleAnimation anger,
	//										milliseconds bubble_hold_duration) {
	//		SpawnBubbleAnimation(
	//			neighbor, anger, anger_key, bubble_pop_duration,
	//			bubble_hold_duration + bubble_pop_duration,
	//			[=]() { neighbor.Get<AngerComponent>().yelling = false; },
	//			[=]() {
	//				V2_float pos = neighbor.Get<Transform>().position -
	//							   V2_float{ 0.0f, neighbor.Get<Size>().s.y + yell_bubble_offset };
	//				return pos;
	//			},
	//			[=]() {
	//				auto& b{ neighbor.Get<AngerComponent>() };
	//				if (!b.yelling) {
	//					// TODO: Add a check that this only plays once upon hold start.
	//					NeighborYell();
	//					b.yelling = true;
	//				}
	//			},
	//			[=]() {
	//				auto& b{ neighbor.Get<AngerComponent>() };
	//				auto new_bubble = static_cast<BubbleAnimation>(static_cast<int>(b.bubble) + 1);
	//				if (new_bubble != BubbleAnimation::AngerStop) {
	//					b.bubble = new_bubble;
	//				}
	//			}
	//		);
	//	};

	//	const float anger_bubble_count{ 5.0f };

	//	neighbor.Add<AngerComponent>(BubbleAnimation::Anger0);

	//	float walk_frac{ 0.2f };
	//	float bubble_frac{ 1.0f - walk_frac };
	//	auto bubble_duration{ bubble_frac * (scene_duration - milliseconds{ 100 }) /
	//						  (anger_bubble_count + 1) };

	//	game.tween.Load(Hash("neighbor_animation"))
	//		.During(std::chrono::duration_cast<milliseconds>(walk_frac * scene_duration))
	//		.OnUpdate([=](float f) {
	//			neighbor.Get<AnimationComponent>().Resume();
	//			neighbor.Get<Transform>().position =
	//				Lerp(neighbot_walk_start_pos, neighbor_walk_end_pos, f);
	//		})
	//		.During(std::chrono::duration_cast<milliseconds>(bubble_frac * scene_duration))
	//		.OnStart([=]() {
	//			neighbor.Get<AnimationComponent>().Pause();
	//			neighbor.Get<SpriteTint>() = color::Red;
	//		})
	//		.OnUpdate([=]() {
	//			neighbor_complain_bubble(
	//				neighbor.Get<AngerComponent>().bubble,
	//				std::chrono::duration_cast<milliseconds>(bubble_duration)
	//			);
	//		})
	//		.Start();

	//	V2_int size{ tile_size.x, 2 * tile_size.y };

	//	auto& s = neighbor.Add<Size>(size);
	//	V2_float hitbox_size{ 8, 8 };
	//	neighbor.Add<Hitbox>(neighbor, hitbox_size, V2_float{ 0.0f, 0.0f });
	//	auto& box  = neighbor.Add<BoxCollider>();
	//	box.size   = hitbox_size;
	//	box.offset = { -4, -8 };
	//	box.origin = Origin::TopLeft;
	//	neighbor.Add<LayerInfo>(1.0f, 0);

	//	manager.Refresh();
	//}

	void BackToMenu();

	// void StartNeighborCutscene() {
	//	// Starting point for camera pan.
	//	auto player_pos = player.Get<Transform>().position;

	//	player_can_move = false;

	//	game.tween.Get(Hash("player_movement_animation")).Pause();
	//	player.Remove<RigidBody>();
	//	player.Get<AnimationComponent>().column = 0;
	//	player.Get<::SpriteSheet>().row			= 0;
	//	neighbor_camera_pos.y					= world_bounds.y;
	//	// player_camera.SetBounds({});

	//	auto completed = [=]() {
	//		auto& fade							 = game.tween.Get(Hash("fade_to_black"));
	//		fade_text.Get<FadeComponent>().state = FadeState::Lose;
	//		fade.Start();
	//	};

	//	// TODO: Simplify using tween points.
	//	game.tween.Load(Hash("neighbor_cutscene"), seconds{ 10 })
	//		.OnUpdate([=](auto& tw, auto f) {
	//			// how far into the tween these things happen:
	//			const float shift_frac{ 0.2f };			 // move cam to neighbor.
	//			const float backshift_frac{ 0.8f };		 // move cam back to player.
	//			const float neighbor_move_start{ 0.1f }; // spawn and start moving neighbor.
	//			if (f > neighbor_move_start && neighbor == ecs::Entity{}) {
	//				float scene_frac{ backshift_frac - neighbor_move_start };
	//				PTGN_ASSERT(scene_frac >= 0.01f);
	//				CreateNeighbor(
	//					std::chrono::duration_cast<milliseconds>(tw.GetDuration() * scene_frac)
	//				);
	//				// TODO: Neighbor complaint stuff here.
	//			}
	//			if (f <= shift_frac || f >= backshift_frac) {
	//				float shift_progress = std::clamp(f / shift_frac, 0.0f, 1.0f);
	//				V2_float start		 = player_pos;
	//				V2_float end		 = neighbor_camera_pos;
	//				// Camera moving back and then forth between player and neighbor.
	//				if (f >= backshift_frac) {
	//					shift_progress =
	//						std::clamp((f - backshift_frac) / (1.0f - backshift_frac), 0.0f, 1.0f);
	//					start = neighbor_camera_pos;
	//					end	  = player.Get<Transform>().position;
	//				}
	//				V2_float pos = Lerp(start, end, shift_progress);
	//				player_camera.SetPosition(pos);
	//			}
	//		})
	//		.OnComplete(completed)
	//		.OnStop(completed)
	//		.Start();
	//}

	// void DrawBarkometer() {
	//	V2_float meter_pos{ 25, 258 };
	//	V2_float meter_size{ barkometer_texture.GetSize() };

	//	float bark_progress = std::clamp(bark_count / bark_threshold, 0.0f, 1.0f);

	//	if (bark_progress >= 1.0f && neighbor == ecs::Entity{} && player.Has<RigidBody>()) {
	//		StartNeighborCutscene();
	//	}

	//	// TODO: If bark_progress > 0.8f (etc) give a warning to player.

	//	Color color = Lerp(color::Gray, color::Red, bark_progress);

	//	V2_float border_size{ 4, 4 };

	//	V2_float barkometer_fill_size{ meter_size - border_size * 2.0f };

	//	V2_float fill_pos{ meter_pos.x, meter_pos.y - border_size.y };

	//	game.draw.Texture(barkometer_texture, { meter_pos, meter_size, Origin::CenterBottom });

	//	Rect rect{ fill_pos,
	//			   { barkometer_fill_size.x, barkometer_fill_size.y * bark_progress },
	//			   Origin::CenterBottom };
	//	rect.Draw(color, -1.0f);
	//}

	// void DrawUI() {
	//	auto prev_primary = game.camera.GetPrimary();

	//	game.draw.Flush();

	//	OrthographicCamera c;
	//	c.SetPosition(game.window.GetCenter());
	//	c.SetSizeToWindow();
	//	c.SetBounds({});
	//	game.camera.SetPrimary(c);

	//	// Draw UI here...

	//	DrawProgressBar();
	//	DrawDogCounter();
	//	DrawBarkometer();
	//	DrawFadeEntity();

	//	game.draw.Flush();

	//	if (game.camera.GetPrimary() == c) {
	//		game.camera.SetPrimary(prev_primary);
	//	}
	//}

	void Draw() {
		// For debugging purposes:
		/*if (draw_hitboxes) {
			DrawWalls();
		}*/

		DrawAnimations();
		/*DrawDogs();
		DrawItems();*/

		// for (auto [e, d] : manager.EntitiesWith<Dog>()) {
		//	if (d.spawn_thingy) {
		//		d.SpawnRequestAnimation(e, d.req);
		//		d.spawn_thingy = false;
		//	}
		//	if (IsRequest(d.request) && !d.patience.IsRunning()) {
		//		d.patience.Start();
		//	}
		//	if (d.patience.ElapsedPercentage(d.patience_duration) >= 1.0f) {
		//		d.Bark(e);
		//		bark_count++;
		//		d.patience.Start();
		//	}
		//	auto& h	 = e.Get<Hitbox>();
		//	auto& ph = player.Get<Hitbox>();
		//	Rect dog_rect{ h.GetPosition(), h.size, e.Get<Origin>() };
		//	Rect player_rect{ ph.GetPosition(), ph.size, player.Get<Origin>() };
		//	if (player_rect.Overlaps(dog_rect) && player.Has<HandComponent>()) {
		//		ecs::Entity item{ player.Get<HandComponent>().current_item };

		//		auto reset_patience_request = [&]() {
		//			d.patience.Stop();
		//			d.request = BubbleAnimation::None;
		//			// PTGN_INFO("Resetting dog patience!");
		//		};

		//		if (item != ecs::Entity{}) {
		//			if (item.Has<ItemComponent>()) {
		//				auto& i = item.Get<ItemComponent>();

		//				if (d.request == i.type) {
		//					switch (i.type) {
		//						case BubbleAnimation::Food: {
		//							reset_patience_request();
		//							break;
		//						}
		//						case BubbleAnimation::Bone: {
		//							reset_patience_request();
		//							break;
		//						}
		//						case BubbleAnimation::Cleanup: {
		//							reset_patience_request();
		//							break;
		//						}
		//						case BubbleAnimation::Toy: {
		//							reset_patience_request();
		//							break;
		//						}
		//						case BubbleAnimation::Outside: {
		//							reset_patience_request();
		//							break;
		//						}
		//						default: break;
		//					}
		//				}
		//			}
		//		} else if (d.request == BubbleAnimation::Pet) {
		//			reset_patience_request();
		//		}
		//	}
		//}

		if (bark_timer.ElapsedPercentage(bark_reset_time) >= 1.0f) {
			bark_count--;
			bark_timer.Start();
		}

		// DrawUI();
	}
};

static Button CreateMenuButton(
	const std::string& content, const Color& text_color, const ButtonCallback& f,
	const Color& color, const Color& hover_color
) {
	Button b;
	b.Set<ButtonProperty::OnActivate>(f);
	b.Set<ButtonProperty::BackgroundColor>(color);
	b.Set<ButtonProperty::BackgroundColor>(hover_color, ButtonState::Hover);
	Text text{ content, color, Hash("menu_font") };
	b.Set<ButtonProperty::Text>(text);
	b.Set<ButtonProperty::TextSize>(V2_float{
		static_cast<float>(b.Get<ButtonProperty::Text>().GetSize().x), 0.0f });
	b.Set<ButtonProperty::LineThickness>(7.0f);
	return b;
}

class LevelSelect : public Scene {
public:
	std::vector<Button> buttons;

	LevelSelect() {
		buttons.push_back(CreateMenuButton(
			"Easy", color::Blue, [&]() { StartGame(Difficulty::Easy); }, color::Blue, color::Black
		));
		buttons.push_back(CreateMenuButton(
			"Medium", color::Green, [&]() { StartGame(Difficulty::Medium); }, color::Gold,
			color::Black
		));
		buttons.push_back(CreateMenuButton(
			"Hard", color::Red, [&]() { StartGame(Difficulty::Hard); }, color::Red, color::Black
		));
		buttons.push_back(CreateMenuButton(
			"Back", color::Black,
			[]() { game.scene.TransitionActive("level_select", "main_menu"); }, color::LightGray,
			color::Black
		));

		for (std::size_t i = 0; i < buttons.size(); i++) {
			buttons[i].SetRect({ V2_int{ first_button_coordinate.x,
										 first_button_coordinate.y +
											 (int)i * (button_size.y + button_y_offset) },
								 button_size, Origin::CenterTop });
			buttons[i].Disable();
		}
	}

	void StartGame(Difficulty difficulty) {
		game.scene.Load<GameScene>("game", difficulty);
		game.scene.TransitionActive("level_select", "game");
	}

	void Init() final {
		for (auto& b : buttons) {
			b.Enable();
		}
	}

	void Shutdown() final {
		for (auto& b : buttons) {
			b.Disable();
		}
	}

	void Update() final {
		game.draw.Texture(
			game.texture.Get("menu_background"),
			Rect{ game.window.GetCenter(), resolution, Origin::Center }, {}, LayerInfo{ -1.0f, 0 }
		);
		for (auto& b : buttons) {
			b.Draw();
		}
	}
};

// TODO: Re-enable.
void GameScene::BackToMenu() {
	game.scene.TransitionActive(
		"game", "level_select",
		SceneTransition{ TransitionType::FadeThroughColor, milliseconds{ 1000 } }
	);
	game.scene.Unload("game");
}

class MainMenu : public Scene {
public:
	std::vector<Button> buttons;

	MainMenu() {
		// TODO: Add if has not check.
		game.font.Load("menu_font", "resources/font/retro_gaming.ttf", button_size.y);
		game.texture.Load("menu_background", "resources/ui/background.png");
		game.music.Load("background_music", "resources/sound/background_music.ogg").Play(-1);
		game.scene.Load<LevelSelect>("level_select");

		buttons.push_back(CreateMenuButton(
			"Play", color::Blue, []() { game.scene.TransitionActive("main_menu", "level_select"); },
			color::Blue, color::Black
		));

		for (int i = 0; i < buttons.size(); i++) {
			buttons[i].SetRect({ V2_int{ first_button_coordinate.x,
										 first_button_coordinate.y +
											 (int)i * (button_size.y + button_y_offset) },
								 button_size, Origin::CenterTop });
			buttons[i].Disable();
		}
	}

	void Init() final {
		for (auto& b : buttons) {
			b.Enable();
		}
	}

	void Shutdown() final {
		for (auto& b : buttons) {
			b.Disable();
		}
	}

	void Update() final {
		game.texture.Get("menu_background")
			.Draw(
				{ game.window.GetCenter(), resolution, Origin::Center }, {}, LayerInfo{ -1.0f, 0 }
			);
		for (const auto& b : buttons) {
			b.Draw();
		}
	}
};

class SetupScene : public Scene {
public:
	SetupScene() {}

	void Init() final {
		game.draw.SetClearColor(color::Silver);
		game.window.SetSize(resolution);

		game.scene.Load<MainMenu>("main_menu");
		game.scene.AddActive("main_menu");
	}
};

// int main() {
//	game.Start<SetupScene>();
//	return 0;
// }
