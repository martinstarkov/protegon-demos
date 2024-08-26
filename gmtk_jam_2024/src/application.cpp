#include "protegon/protegon.h"

using namespace ptgn;

constexpr const V2_int resolution{ 960, 540 };
constexpr const V2_int center{ resolution / 2 };
constexpr const bool draw_hitboxes{ false };

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

struct TintColor {
	TintColor(const Color& tint) : tint{ tint } {}

	Color tint;
};

struct Wife {
	Wife() = default;

	bool returned{ false };
	bool voice_heard{ false };
};

struct SpriteSheet {
	SpriteSheet(
		const Texture& texture, const V2_int& source_pos = {}, const V2_int& animation_count = {}
	) :
		texture{ texture },
		source_pos{ source_pos },
		animation_count{ animation_count },
		source_size{ [&]() {
			if (animation_count.IsZero()) {
				return V2_int{};
			}
			PTGN_ASSERT(animation_count.x != 0 && animation_count.y != 0);
			return texture.GetSize() / animation_count;
		}() }

	{}

	int row{ 0 };
	V2_int animation_count; // in each direction of the sprite sheet.
	V2_int source_pos;
	// Empty vector defaults to entire texture size.
	V2_int source_size;
	Texture texture;
};

struct ItemComponent {
	ItemComponent() = default;
	bool held{ false };
	BubbleAnimation type{ BubbleAnimation::None };
	float weight_factor{ 1.0f };
};

struct SortByZ {};

struct Position {
	Position(const V2_float& pos) : p{ pos } {}

	/*V2_float GetPosition(ecs::Entity e) const {
		V2_float offset;
		if (e.Has<SpriteSheet>() && e.Has<Origin>()) {
			offset = GetOffsetFromCenter(e.Get<SpriteSheet>().source_size / scale, e.Get<Origin>());
		}
		return p + offset;
	}*/

	V2_float p;
};

struct Size {
	Size(const V2_float& size) : s{ size } {}

	V2_float s;
};

struct Direction {
	Direction(const V2_int& dir) : dir{ dir } {}

	V2_int dir;
};

struct Hitbox {
	Hitbox(ecs::Entity parent, const V2_float& size, const V2_float& offset = {}) :
		parent{ parent }, size{ size }, offset{ offset } {}

	V2_float GetPosition() const {
		PTGN_ASSERT(parent.IsAlive());
		PTGN_ASSERT(parent.Has<Position>());
		return parent.Get<Position>().p + offset;
	}

	Color color{ color::Blue };
	ecs::Entity parent;
	V2_float size;
	V2_float offset;
};

struct PickupHitbox : public Hitbox {
	using Hitbox::Hitbox;
};

struct InteractHitbox : public Hitbox {
	using Hitbox::Hitbox;
};

struct HandComponent {
	HandComponent(float radius, const V2_float& offset) : radius{ radius }, offset{ offset } {}

	V2_float GetPosition(ecs::Entity e) const {
		const auto& pos = e.Get<Position>().p;
		const auto& d	= e.Get<Direction>();

		return pos + V2_float{ d.dir.x * offset.x, offset.y };
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

struct AnimationComponent {
	AnimationComponent(
		std::size_t tween_key, int column_count, milliseconds duration, int column = 0
	) :
		tween_key{ tween_key },
		column_count{ column_count },
		duration{ duration },
		column{ column } {}

	void Pause() {
		game.tween.Get(tween_key).Pause();
	}

	void Resume() {
		game.tween.Get(tween_key).Resume();
	}

	// duration of one whole animation.
	milliseconds duration{ 1000 };
	std::size_t tween_key{ 0 };
	int column_count{ 1 };
	int column{ 0 };
};

struct Velocity {
	Velocity() = default;

	Velocity(const V2_float& velocity, const V2_float& max) : current{ velocity }, max{ max } {}

	V2_float current;
	V2_float max;
};

struct Acceleration {
	Acceleration(const V2_float& current, const V2_float& max) : current{ current }, max{ max } {}

	V2_float current;
	V2_float max;
};

struct ZIndex {
	ZIndex(float z_index) : z_index{ z_index } {}

	float z_index{ 0.0f };
};

static bool OutOfBounds(ecs::Entity e, const V2_float& pos, const V2_float& max_bounds) {
	const auto& hitbox = e.Get<Hitbox>();
	const auto& origin = e.Get<Origin>();
	V2_float size{ hitbox.size };
	Rectangle<float> h{ pos + hitbox.offset, size, origin };
	V2_float min{ h.Min() };
	V2_float max{ h.Max() };
	return min.x < 0 || max.x > max_bounds.x || min.y < 0 || max.y > max_bounds.y;
}

static void ApplyBounds(ecs::Entity e, const V2_float& max_bounds) {
	if (!e.Has<Position>()) {
		return;
	}
	if (!e.Has<Hitbox>()) {
		return;
	}
	if (!e.Has<Origin>()) {
		return;
	}
	auto& pos		   = e.Get<Position>();
	const auto& hitbox = e.Get<Hitbox>();
	const auto& origin = e.Get<Origin>();
	V2_float size{ hitbox.size };
	Rectangle<float> h{ hitbox.GetPosition(), size, origin };
	V2_float min{ h.Min() };
	V2_float max{ h.Max() };
	if (min.x < 0) {
		pos.p.x -= min.x;
	} else if (max.x > max_bounds.x) {
		pos.p.x += max_bounds.x - max.x;
	}
	if (min.y < 0) {
		pos.p.y -= min.y;
	} else if (max.y > max_bounds.y) {
		pos.p.y += max_bounds.y - max.y;
	}
}

auto GetWalls(ecs::Manager& manager) {
	return manager.EntitiesWith<Position, Hitbox, Origin, DynamicCollisionShape, WallComponent>();
}

void ResolveStaticWallCollisions(ecs::Entity obj) {
	auto& hitbox	  = obj.Get<Hitbox>();
	bool intersecting = false;
	IntersectCollision max;
	Rectangle<float> rect{ hitbox.GetPosition(), hitbox.size, obj.Get<Origin>() };
	for (auto [e, p, h, o, s, w] : GetWalls(obj.GetManager())) {
		Rectangle<float> r{ h.GetPosition(), h.size, o };
		IntersectCollision c;
		if (game.collision.intersect.RectangleRectangle(rect, r, c)) {
			intersecting = true;
			if (c.depth > max.depth) {
				max = c;
			}
		}
	}
	if (intersecting) {
		obj.Get<Position>().p += max.depth * max.normal;
	}
}

bool IsRequest(BubbleAnimation anim) {
	switch (anim) {
		case BubbleAnimation::Food:	   return true;
		case BubbleAnimation::Cleanup: return true;
		case BubbleAnimation::Bone:	   return true;
		case BubbleAnimation::Pet:	   return true;
		case BubbleAnimation::Toy:	   return true;
		case BubbleAnimation::Outside: return true;
	}
	return false;
}

struct AngerComponent {
	AngerComponent(BubbleAnimation bubble) : bubble{ bubble } {}

	bool started{ false };
	bool yelling{ false };
	BubbleAnimation bubble;
};

void SpawnBubbleAnimation(
	ecs::Entity entity, BubbleAnimation bubble_type, std::size_t bubble_tween_key,
	duration<float, milliseconds::period> popup_duration,
	duration<float, milliseconds::period> total_duration,
	const std::function<void(Tween& tw, float f)>& start_callback,
	const std::function<V2_float()>& get_pos_callback,
	const std::function<void()>& hold_start_callback,
	const std::function<void(Tween& tw, float f)>& complete_callback
) {
	if (game.tween.Has(bubble_tween_key)) {
		// Already in the middle of an animation.
		return;
	}

	float start_hold_threshold{ popup_duration / total_duration };

	TweenConfig config;

	config.on_start	   = start_callback;
	config.on_complete = complete_callback;
	config.on_stop	   = complete_callback;

	config.on_update = [=](auto& tw, auto f) {
		std::size_t bubble_texture_key{ Hash("bubble") + static_cast<std::size_t>(bubble_type) };
		const int request_animation_count{ 4 };
		Texture t{ game.texture.Get(bubble_texture_key) };
		V2_float source_size{ t.GetSize() / V2_float{ request_animation_count, 1 } };

		const int hold_frame{ request_animation_count - 1 };

		float column = 0.0f;

		if (f >= start_hold_threshold) {
			hold_start_callback();
			column = hold_frame;
		} else {
			column = std::floorf(f / start_hold_threshold * hold_frame);
		}

		V2_float source_pos = { column * source_size.x, 0.0f };

		// TODO: Move this to draw functions and use an entity instead.
		game.renderer.DrawTexture(
			t, get_pos_callback(), source_size, source_pos, source_size, entity.Get<Origin>(),
			entity.Get<Flip>(), 0.0f, {}, 1.0f
		);
	};

	auto& tween = game.tween.Load(
		bubble_tween_key, 0.0f, 1.0f, std::chrono::duration_cast<milliseconds>(total_duration),
		config
	);
	tween.Start();
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
		animations_to_goal = e.Get<AnimationComponent>().column_count;
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
					auto& h{ e.Get<Hitbox>() };
					auto flip{ e.Get<Flip>() };
					float sign	= (flip == Flip::Horizontal ? -1.0f : 1.0f);
					auto offset = GetOffsetFromCenter(h.size, e.Get<Origin>());
					V2_float pos{ e.Get<Position>().p + offset +
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
		TweenConfig config;
		// TODO: Consider using an entity.
		// auto bark_anim = dog.GetManager().CreateEntity();
		// bark_anim.Add<AnimationComponent>();
		config.on_update = [=](Tween& tw, float f) {
			std::size_t bark_texture_key{ Hash("bark") };
			const int count{ 6 };
			float column = std::floorf(f * (count - 1));
			Texture t{ game.texture.Get(bark_texture_key) };
			V2_float source_size{ t.GetSize() / V2_float{ count, 1 } };
			V2_float source_pos = { column * source_size.x, 0.0f };
			auto& h{ e.Get<Hitbox>() };
			auto flip{ e.Get<Flip>() };
			float sign	= (flip == Flip::Horizontal ? -1.0f : 1.0f);
			auto offset = GetOffsetFromCenter(h.size, e.Get<Origin>());
			V2_float pos{ e.Get<Position>().p + offset +
						  V2_float{ sign * (h.size.x / 2 + bark_offset.x), -bark_offset.y } };
			// TODO: Move this to draw functions and use an entity instead.
			game.renderer.DrawTexture(
				t, pos, source_size, source_pos, source_size, e.Get<Origin>(), flip, 0.0f, {}, 0.8f
			);
		};
		auto& tween = game.tween.Load(bark_tween_key, 0.0f, 1.0f, milliseconds{ 135 }, config);
		tween.Start();
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
			// dog.Get<SpriteSheet>().row = X:
			// auto& an{ dog.Get<AnimationComponent>() };
			// an.column = static_cast<int>(an.column_count * f) %
			// an.column_count;
		} else {
			// dog.Get<SpriteSheet>().row = 0:
			e.Get<Position>().p = Lerp(start, target, progress) - e.Get<Hitbox>().offset;
			ApplyBounds(e, game.texture.Get(Hash("house_background")).GetSize());
			// ResolveStaticWallCollisions(e);
			if (draw_hitboxes) {
				game.renderer.DrawLine(start, target, color::Purple, 5.0f);
			}
			auto& an{ e.Get<AnimationComponent>() };
			an.column = static_cast<int>(animations_to_goal * progress) % an.column_count;
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
			start  = e.Get<Hitbox>().GetPosition();
			target = start;
		}
		// ResolveStaticWallCollisions(e);
		//  Reset lingering state.
		lingering = false;
		SetNewTarget(e);
		// Reset animation.
		e.Get<AnimationComponent>().column = 0;
	}

	void SetNewTarget(ecs::Entity e) {
		start = target;
		V2_float min{ 0.0f, 0.0f };
		V2_float max = game.texture.Get(Hash("house_background")).GetSize();

		float max_length{ (max - min).MagnitudeSquared() };
		PTGN_ASSERT(max_length != 0.0f);

		auto viable_path = [=](const V2_float& vel) mutable {
			auto& hitbox{ e.Get<Hitbox>() };
			Rectangle<float> dog_rect{ hitbox.GetPosition(), hitbox.size, e.Get<Origin>() };
			for (auto [en, p, h, o, s, w] : GetWalls(e.GetManager())) {
				Rectangle<float> r{ h.GetPosition(), h.size, o };
				DynamicCollision c;
				if (game.collision.dynamic.RectangleRectangle(dog_rect, vel, r, c)) {
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
				e.Get<Flip>() = Flip::Horizontal;
			} else {
				e.Get<Flip>() = Flip::None;
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

			auto& anim{ e.Get<AnimationComponent>() };

			int animation_cycles =
				static_cast<int>(std::ceilf(path_duration / duration<float>(anim.duration)));

			if (run) {
				animation_cycles = static_cast<int>(animation_cycles * run_factor);
			}

			animations_to_goal = std::max(1, animation_cycles) * anim.column_count;
		}
	}

	void Pause() {
		game.tween.Get(walk).Pause();
	}

	void Resume() {
		game.tween.Get(walk).Resume();
	}

	bool whined{ false };

	V2_float request_offset{ -11, 11 };

	BubbleAnimation request{ BubbleAnimation::None };

	// For debugging purposes.
	V2_float potential_target;

	// Pixels from corner of hitbox that the mouth of the dog is.
	V2_float bark_offset;
	duration<float, seconds::period> request_animation_hold_duration{ 7 };
	duration<float, milliseconds::period> request_animation_popup_duration{ 200 };

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
		progress_bar_texture = Surface{ "resources/ui/progress_bar.png" };
		progress_bar_texture = Surface{ "resources/ui/progress_bar.png" };
		progress_car_texture = Surface{ "resources/ui/progress_car.png" };
		dog_counter_texture	 = Surface{ "resources/ui/dog_counter.png" };
		barkometer_texture	 = Surface{ "resources/ui/barkometer.png" };
		level				 = Surface{ "resources/level/house_hitbox.png" };

		game.texture.Load(Hash("bark"), "resources/entity/bark.png");

		std::size_t r{ Hash("bubble") };
		auto load_request = [&](BubbleAnimation animation_type, const std::string& name) {
			path p{ "resources/ui/bubble_" + name + ".png" };
			PTGN_ASSERT(FileExists(p));
			game.texture.Load(r + static_cast<std::size_t>(animation_type), p);
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

		house_background = game.texture.Load(Hash("house_background"), "resources/level/house.png");
		world_bounds	 = house_background.GetSize();
		// TODO: Populate with actual barks.
		game.sound.Load(Hash("vizsla_bark1"), "resources/sound/bark_1.ogg");
		game.sound.Load(Hash("vizsla_bark2"), "resources/sound/bark_2.ogg");
		game.sound.Load(Hash("great_dane_bark1"), "resources/sound/bark_1.ogg");
		game.sound.Load(Hash("great_dane_bark2"), "resources/sound/bark_2.ogg");
		game.sound.Load(Hash("maltese_bark1"), "resources/sound/small_bark.ogg");
		game.sound.Load(Hash("maltese_bark2"), "resources/sound/small_bark.ogg");
		game.sound.Load(Hash("dachshund_bark1"), "resources/sound/small_bark.ogg");
		game.sound.Load(Hash("dachshund_bark2"), "resources/sound/small_bark.ogg");
		// TODO: Populate with actual whines.
		game.sound.Load(Hash("vizsla_whine"), "resources/sound/dog_whine.ogg");
		game.sound.Load(Hash("great_dane_whine"), "resources/sound/dog_whine.ogg");
		game.sound.Load(Hash("maltese_whine"), "resources/sound/small_dog_whine.ogg");
		game.sound.Load(Hash("dachshund_whine"), "resources/sound/small_dog_whine.ogg");
		// TODO: Populate with actual complaining.
		game.sound.Load(Hash("neighbor_yell0"), "resources/sound/yell0.ogg");
		game.sound.Load(Hash("neighbor_yell1"), "resources/sound/yell1.ogg");
		game.sound.Load(Hash("neighbor_yell2"), "resources/sound/yell2.ogg");

		game.sound.Load(Hash("door_close"), "resources/sound/door_close.ogg");
		game.sound.Load(Hash("door_open"), "resources/sound/door_open.ogg");
		game.sound.Load(Hash("wife_arrives"), "resources/sound/wife_arrives.ogg");
	}

	void CreatePlayer() {
		player = manager.CreateEntity();

		auto& ppos = player.Add<Position>(V2_float{ 215, 290 });
		player.Add<Velocity>(V2_float{}, V2_float{ 700.0f });
		player.Add<Acceleration>(V2_float{}, V2_float{ 1200.0f });
		player.Add<Origin>(Origin::CenterBottom);
		player.Add<Flip>(Flip::None);
		player.Add<Direction>(V2_int{ 0, 1 });
		player.Add<Human>();
		player.Add<Wife>();

		V2_int player_animation_count{ 4, 3 };

		// Must be added before AnimationComponent as it pauses the animation immediately.
		player.Add<SpriteSheet>(
			Texture{ "resources/entity/player.png" }, V2_int{}, player_animation_count
		);

		milliseconds animation_duration{ 400 };

		TweenConfig animation_config;
		animation_config.repeat = -1;

		animation_config.on_pause = [=](auto& t, auto v) {
			player.Get<AnimationComponent>().column = 0;
		};
		animation_config.on_update = [=](auto& t, auto v) {
			player.Get<AnimationComponent>().column = static_cast<int>(std::floorf(v));
		};
		animation_config.on_repeat = [=](auto& t, auto v) {
			player.Get<AnimationComponent>().column = 0;
		};

		auto tween = game.tween.Load(
			Hash("player_movement_animation"), 0.0f, static_cast<float>(player_animation_count.x),
			animation_duration, animation_config
		);
		tween.Start();

		auto& anim = player.Add<AnimationComponent>(
			Hash("player_movement_animation"), player_animation_count.x, animation_duration
		);
		anim.Pause();

		V2_int size{ tile_size.x, 2 * tile_size.y };

		auto& s = player.Add<Size>(size);
		V2_float hitbox_size{ 8, 8 };
		player.Add<Hitbox>(player, hitbox_size, V2_float{ 0.0f, 0.0f });
		player.Add<HandComponent>(8.0f, V2_float{ 8.0f, -s.s.y * 0.3f });
		player.Add<DynamicCollisionShape>(DynamicCollisionShape::Rectangle);
		player.Add<SortByZ>();
		player.Add<ZIndex>(0.0f);

		manager.Refresh();
	}

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

		TweenConfig tween_config;
		tween_config.repeat = -1;

		auto start_walk = [=](Tween& tw, float f) mutable {
			dog.Get<Dog>().StartWalk(dog);
		};

		tween_config.on_pause = [=](Tween& tw, float f) mutable {
			dog.Get<AnimationComponent>().column = 0;
		};
		tween_config.on_start  = start_walk;
		tween_config.on_resume = start_walk;
		tween_config.on_repeat = start_walk;
		tween_config.on_update = [=](Tween& tw, float f) mutable {
			dog.Get<Dog>().Update(dog, f);
		};
		dog.Add<Position>(pos);
		dog.Add<Size>(size);
		dog.Add<SpriteSheet>(t, V2_int{}, animation_count);
		dog.Add<Origin>(Origin::CenterBottom);
		dog.Add<AnimationComponent>(Hash(dog), animation_count.x, milliseconds{ 2000 });
		dog.Add<Dog>(dog, V2_float{}, Hash(dog), bark_sound_keys, whine_sound_key, bark_offset);

		dog.Add<InteractHitbox>(dog, size);
		dog.Add<Hitbox>(dog, hitbox_size, hitbox_offset);
		dog.Add<SortByZ>();
		dog.Add<ZIndex>(0.0f);
		dog.Add<Velocity>(V2_float{}, V2_float{ 700.0f });
		dog.Add<Acceleration>(V2_float{}, V2_float{ 1200.0f });
		dog.Add<DynamicCollisionShape>(DynamicCollisionShape::Rectangle);
		dog.Add<Flip>(Flip::None);

		manager.Refresh();

		auto& tween = game.tween.Load(Hash(dog), 0.0f, 1.0f, seconds{ 1 }, tween_config);
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
		item.Add<Size>(size);
		item.Add<PickupHitbox>(item, size * hitbox_scale);
		item.Add<Hitbox>(item, size);
		item.Add<Origin>(Origin::Center);
		item.Add<Position>(pos);
		item.Add<SpriteSheet>(t, V2_int{}, V2_int{});
		item.Add<SortByZ>();
		item.Add<ZIndex>(0.0f);
		item.Add<Velocity>(V2_float{}, V2_float{ 700.0f });
		item.Add<Acceleration>(V2_float{}, V2_float{ 1500.0f });
		item.Add<DynamicCollisionShape>(DynamicCollisionShape::Rectangle);

		manager.Refresh();

		return item;
	}

	void CreateWall(const V2_int& hitbox_size, const V2_int& cell) {
		auto wall = manager.CreateEntity();
		wall.Add<WallComponent>();
		wall.Add<Size>(hitbox_size);
		wall.Add<Hitbox>(wall, hitbox_size);
		wall.Add<Position>(cell * hitbox_size);
		wall.Add<Origin>(Origin::TopLeft);
		wall.Add<DynamicCollisionShape>(DynamicCollisionShape::Rectangle);

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
		auto& spritesheet =
			daughter.Add<SpriteSheet>(girl_texture, V2_int{}, daughter_animation_count);

		V2_float daughter_start_pos{ daughter_camera_pos.x,
									 daughter_camera_pos.y + spritesheet.source_size.y };

		auto& ppos = daughter.Add<Position>(daughter_start_pos);
		daughter.Add<Velocity>(V2_float{}, V2_float{ 700.0f });
		daughter.Add<Acceleration>(V2_float{}, V2_float{ 1200.0f });
		daughter.Add<Origin>(Origin::CenterBottom);
		daughter.Add<Flip>(Flip::None);
		daughter.Add<Human>();
		daughter.Add<TintColor>(color::White);

		milliseconds animation_duration{ 200 };

		TweenConfig animation_config;
		animation_config.repeat = -1;

		animation_config.on_pause = [=](auto& t, auto v) {
			daughter.Get<AnimationComponent>().column = 0;
		};
		animation_config.on_update = [=](auto& t, auto v) {
			daughter.Get<AnimationComponent>().column = static_cast<int>(std::floorf(v));
		};
		animation_config.on_repeat = [=](auto& t, auto v) {
			daughter.Get<AnimationComponent>().column = 0;
		};

		auto tween = game.tween.Load(
			Hash("daughter_movement_animation"), 0.0f,
			static_cast<float>(daughter_animation_count.x), animation_duration, animation_config
		);
		tween.Start();

		auto& anim = daughter.Add<AnimationComponent>(
			Hash("daughter_movement_animation"), daughter_animation_count.x, animation_duration
		);

		TweenConfig daughter_scene_config;

		daughter_scene_config.yoyo	 = true;
		daughter_scene_config.repeat = -1;

		V2_float neighbot_walk_start_pos = ppos.p;

		std::size_t love_key{ daughter.GetId() + Hash("love") };

		duration<float, milliseconds::period> bubble_pop_duration{ 300 };

		float love_bubble_offset{ 2.0f };

		auto daughter_complain_bubble =
			[=](BubbleAnimation anger, duration<float, milliseconds::period> bubble_hold_duration) {
				SpawnBubbleAnimation(
					daughter, anger, love_key, bubble_pop_duration,
					bubble_hold_duration + bubble_pop_duration, [=](Tween& tw, float f) {},
					[=]() {
						V2_float pos =
							daughter.Get<Position>().p -
							V2_float{ 0.0f, daughter.Get<Size>().s.y + love_bubble_offset };
						return pos;
					},
					[=]() {},
					[=](auto& tw, float f) {
						daughter.Get<AnimationComponent>().Resume();
						// game.tween.Get(Hash("daughter_animation")).Resume();
					}
				);
			};

		const float anger_bubble_count{ 5.0f };

		daughter.Add<AngerComponent>(BubbleAnimation::Love);

		daughter_scene_config.on_update = [=](Tween& tw, auto f) {
			V2_float start = neighbot_walk_start_pos;
			V2_float end   = daughter_walk_end_pos;

			if (f > 0.5f) {
				daughter.Get<SpriteSheet>().row = 0;
				// daughter.Get<Flip>() = Flip::None;
				end	  = neighbot_walk_start_pos;
				start = daughter_walk_end_pos;
				daughter.Get<AnimationComponent>().Resume();
			} else {
				daughter.Get<SpriteSheet>().row = 1;
				//  daughter.Get<Flip>() = Flip::Vertical;
				//   game.tween.Get(Hash("daughter_animation")).Pause();
				daughter.Get<AnimationComponent>().Pause();
				daughter_complain_bubble(
					daughter.Get<AngerComponent>().bubble, milliseconds{ 2000 }
				);
			}
			daughter.Get<Position>().p = Lerp(start, end, (1.0f - f));
		};

		auto daughter_scene = game.tween.Load(
			Hash("daughter_animation"), 0.0f, 1.0f, daughter_spawn_rate, daughter_scene_config
		);
		daughter_scene.Start();

		V2_int size{ tile_size.x, 26 };

		auto& s = daughter.Add<Size>(size);
		V2_float hitbox_size{ 8, 8 };
		daughter.Add<Hitbox>(daughter, hitbox_size, V2_float{ 0.0f, 0.0f });
		daughter.Add<ZIndex>(1.0f);

		manager.Refresh();
	};

	constexpr static const float scale = 2.0f;

	void Init() final {
		CreatePlayer();

		player_camera = game.camera.Load(Hash("player_camera"));
		// player_camera.SetSizeToWindow();
		player_camera.SetSize(game.window.GetSize() / scale);
		game.camera.SetPrimary(Hash("player_camera"));
		player_camera.SetClampBounds({ {}, world_bounds, Origin::TopLeft });
		player_camera.SetPosition(player.Get<Position>().p);

		neighbor_camera = game.camera.Load(Hash("neighbor_camera"));
		neighbor_camera.SetSize(game.window.GetSize() / scale);

		// camera_motion = game.tween.Load(Hash("camera_tween"), 0.0f, 800.0f, seconds{ 10 });
		// camera_motion.SetCallback(TweenEvent::Update, [=](auto& t, auto v) {
		//	player_camera.SetPosition(V2_float{ v, v });
		// });

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

		Rectangle<float> garden{ V2_float{ 347, 332 }, V2_float{ 568, 368 } - V2_float{ 347, 332 },
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
		fade_entity.Add<TintColor>(Color{ 0, 0, 0, 0 });

		fade_text = manager.CreateEntity();
		fade_text.Add<TintColor>(Color{ 255, 255, 255, 0 });
		fade_text.Add<FadeComponent>(FadeState::None);

		TweenConfig fade_to_black_config;

		fade_to_black_config.on_update = [=](auto& tw, auto f) {
			fade_entity.Get<TintColor>().tint.a = static_cast<std::uint8_t>(f * 255.0f);
		};

		milliseconds background_fade_duration{ 500 };
		seconds text_fade_duration{ 3 };

		fade_to_black_config.on_complete = [=](auto& tw, auto f1) {
			TweenConfig text_config;
			text_config.ease	 = TweenEase::Linear;
			text_config.on_start = [=](auto& tw2, float f2) {
				game.sound.Get(Hash("door_close")).Play(-1);
			};
			text_config.on_update = [=](Tween& tw2, float f2) {
				if (f2 > 0.5) {
					f2 = 1.0f - f2;
				}
				fade_text.Get<TintColor>().tint.a = static_cast<std::uint8_t>(f2 / 0.5 * 255.0f);
			};
			text_config.on_complete = [=](auto& tw2, auto f2) {
				BackToMenu();
			};
			text_config.on_stop = text_config.on_complete;
			auto& text_fade_tween =
				game.tween.Load(Hash("fade_text"), 0.0f, 1.0f, text_fade_duration, text_config);
			text_fade_tween.Start();
		};
		fade_to_black_config.on_stop = fade_to_black_config.on_complete;

		game.tween.Load(
			Hash("fade_to_black"), 0.0f, 1.0f, background_fade_duration, fade_to_black_config
		);

		// TODO: Move elsewhere, perhaps react to some event.
		return_timer.Start();
		bark_timer.Start();

		CreateDaughter();

		spawn_timer.Start();
	}

	void PlayerMovementInput(float dt) {
		if (!player_can_move) {
			return;
		}

		PTGN_ASSERT(player.Has<Velocity>());
		PTGN_ASSERT(player.Has<Acceleration>());
		PTGN_ASSERT(player.Has<Flip>());
		PTGN_ASSERT(player.Has<SpriteSheet>());
		PTGN_ASSERT(player.Has<HandComponent>());
		PTGN_ASSERT(player.Has<AnimationComponent>());
		PTGN_ASSERT(player.Has<Direction>());

		auto& v	   = player.Get<Velocity>();
		auto& a	   = player.Get<Acceleration>();
		auto& f	   = player.Get<Flip>();
		auto& t	   = player.Get<SpriteSheet>();
		auto& hand = player.Get<HandComponent>();
		auto& anim = player.Get<AnimationComponent>();
		auto& dir  = player.Get<Direction>().dir;

		bool up{ game.input.KeyPressed(Key::W) };
		bool down{ game.input.KeyPressed(Key::S) };
		bool right{ game.input.KeyPressed(Key::D) };
		bool left{ game.input.KeyPressed(Key::A) };

		bool movement{ up || down || right || left };

		if (movement) {
			anim.Resume();
		} else {
			anim.Pause();
		}

		if (up) {
			a.current.y = -1;
		} else if (down) {
			a.current.y = 1;
		} else {
			a.current.y = 0;
			v.current.y = 0;
		}

		if (left) {
			a.current.x = -1;
			f			= Flip::Horizontal;
		} else if (right) {
			a.current.x = 1;
			f			= Flip::None;
		} else {
			a.current.x = 0;
			v.current.x = 0;
		}

		// Store previous direction.
		if (a.current.x != 0 || a.current.y != 0) {
			dir = V2_int{ a.current };
		}

		int front_row{ 0 };
		int side_row{ 1 };
		int back_row{ 2 };

		/*if (t.row != 2) {
			t.row = 0;
		}*/

		// Sideways movement / animation prioritized over up and down.

		if (dir.x != 0) {
			t.row = 1;
		} else if (dir.y == 1) {
			t.row = 0;
		} else if (dir.y == -1) {
			t.row = 2;
		}
		// PTGN_INFO("Animation frame: ", anim.column);

		a.current = a.current.Normalized() * a.max * hand.GetWeightFactor();
	}

	void UpdateAnimations() {
		for (auto [e, t, anim] : manager.EntitiesWith<SpriteSheet, AnimationComponent>()) {
			t.source_pos.x = t.source_size.x * anim.column;
			t.source_pos.y = t.source_size.y * t.row;
		}
	}

	void ResolveWallCollisions(
		float dt, V2_float& position, ecs::Entity entity, bool reset_velocity = false
	) {
		V2_float adjust = game.collision.dynamic.Sweep(
			dt, entity, GetWalls(manager),
			[](ecs::Entity e) { return e.Get<Hitbox>().GetPosition(); },
			[](ecs::Entity e) { return e.Get<Hitbox>().size; },
			[](ecs::Entity e) {
				if (e.Has<Velocity>()) {
					return e.Get<Velocity>().current;
				}
				return V2_float{};
			},
			[](ecs::Entity e) {
				if (e.Has<Origin>()) {
					return e.Get<Origin>();
				}
				return Origin::Center;
			},
			[](ecs::Entity e) {
				PTGN_ASSERT(e.Has<DynamicCollisionShape>());
				return e.Get<DynamicCollisionShape>();
			},
			DynamicCollisionResponse::Slide
		);
		position += adjust;

		if (reset_velocity && !adjust.IsZero()) {
			entity.Get<Velocity>().current = {};
		}
		ResolveStaticWallCollisions(entity);
	}

	void UpdatePhysics(float dt) {
		float drag{ 10.0f };

		for (auto [e, p, v, a] : manager.EntitiesWith<Position, Velocity, Acceleration>()) {
			v.current += a.current * dt;

			v.current.x = std::clamp(v.current.x, -v.max.x, v.max.x);
			v.current.y = std::clamp(v.current.y, -v.max.y, v.max.y);

			v.current.x -= drag * v.current.x * dt;
			v.current.y -= drag * v.current.y * dt;

			bool is_player{ e == player };

			if (e.Has<ItemComponent>()) {
				auto& item = e.Get<ItemComponent>();
				if (!item.held) {
					ResolveWallCollisions(dt, p.p, e, true);
				}
			} else if (is_player) {
				ResolveWallCollisions(dt, p.p, e);
			}

			p.p += v.current * dt;
		}

		if (player_can_move) {
			ApplyBounds(player, world_bounds);
		}
	}

	void UpdatePlayerHand() {
		auto& hand	 = player.Get<HandComponent>();
		auto& hitbox = player.Get<Hitbox>();
		Circle<float> circle{
			hand.GetPosition(player),
			hand.radius,
		};
		if (game.input.KeyUp(item_interaction_key)) {
			if (!hand.HasItem()) {
				for (auto [e, p, h, o, i] :
					 manager.EntitiesWith<Position, PickupHitbox, Origin, ItemComponent>()) {
					Rectangle<float> r{ h.GetPosition(), h.size, o };
					if (draw_hitboxes) {
						game.renderer.DrawRectangleHollow(r, color::Red);
					}
					if (game.collision.overlap.CircleRectangle(circle, r)) {
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
				Rectangle<float> r_item{ { h_pos.x - hand.offset.y, h_pos.y },
										 h_item.size,
										 o_item };

				// If item is not in the wall, throw it, otherwise push it out of the wall.
				bool wall{ false };
				IntersectCollision max;
				for (auto [e, p, h, o, s, w] : GetWalls(manager)) {
					Rectangle<float> r{ h.GetPosition(), h.size, o };
					r_item = { h_pos, h_item.size, o_item };
					IntersectCollision c;
					if (game.collision.intersect.RectangleRectangle(r_item, r, c)) {
						wall = true;
						if (c.depth > max.depth) {
							max = c;
						}
					}
				}
				if (wall) {
					hand.current_item.Get<Position>().p += max.depth * max.normal;
				} else {
					auto& item_vel{ hand.current_item.Get<Velocity>() };
					auto& vel{ player.Get<Velocity>() };
					const auto& accel{ player.Get<Acceleration>() };
					if (FastAbs(accel.current.x) == 0.0f && FastAbs(accel.current.y) == 0.0f) {
						// Throw item from ground (at low velocities).
					} else {
						// Throw item from hand.
						hand.current_item.Get<Position>().p.y += hand.offset.y;
					}
					item_vel.current = vel.current.Normalized() * vel.max * 0.65f;
				}
				// auto& item_accel{ hand.current_item.Get<Acceleration>() };
				// item_accel.current = item_accel.max * dir;
				//  TODO: Add effect to throw item in direction player is facing.
				hand.current_item = {};
			}
		}
		if (hand.HasItem()) {
			hand.current_item.Get<Position>().p = player.Get<Hitbox>().GetPosition();
			if (player.Get<Direction>().dir.y == -1) {
				player.Get<ZIndex>().z_index			= 0.1f;
				hand.current_item.Get<ZIndex>().z_index = 0.0f;
			} else {
				player.Get<ZIndex>().z_index			= 0.0f;
				hand.current_item.Get<ZIndex>().z_index = 0.1f;
			}
		}
	}

	void ResetHitboxColors() {
		for (auto [e, h] : manager.EntitiesWith<Hitbox>()) {
			h.color = color::Blue;
		}
	}

	/*void UpdateZIndices() {
		auto entities				= manager.EntitiesWith<Position, Hitbox, ZIndex, SortByZ>();
		std::vector<ecs::Entity> ev = entities.GetVector();

		std::sort(ev.begin(), ev.end(), [=](ecs::Entity a, ecs::Entity b) {
			return a.Get<Position>().p.y < b.Get<Position>().p.y;
		});

		const float z_delta{ 1.0f / (ev.size() - 1) };

		for (std::size_t i = 0; i < ev.size(); ++i) {
			ev[i].Get<ZIndex>().z_index = z_delta * static_cast<float>(i);
			Print(ev[i].GetId(), ", ");
		}
		PrintLine();
	}*/

	void Update(float dt) final {
		if (!game.tween.Has(Hash("neighbor_cutscene"))) {
			//  Camera follows the player.
			player_camera.SetPosition(player.Get<Position>().p);
		}

		DrawBackground();

		ResetHitboxColors();

		PlayerMovementInput(dt);

		UpdatePhysics(dt);

		if (player_can_move) {
			UpdatePlayerHand();
		}

		// UpdateZIndices();

		UpdateAnimations();

		float dist{ (daughter.Get<Position>().p - daughter_walk_end_pos).Magnitude() };

		if (spawn_timer.ElapsedPercentage(dog_spawn_rate) >= 1.0f && dist < 60.0f) {
			spawn_timer.Start();
			SpawnDog(daughter.Get<Position>().p);
		}

		// Debug testing dog graphical bug:
		/*
		RNG<int> dog_rng{ 0, 3 };

		for (size_t i = 0; i < dog_counter; i++) {
			Texture t;
			int dog_index = dog_rng();
			if (dog_index == 0) {
				t = Texture{ "resources/dog/vizsla.png" };
			} else if (dog_index == 1) {
				t = Texture{ "resources/dog/dachshund_purple.png" };
			} else if (dog_index == 2) {
				t = Texture{ "resources/dog/great_dane.png" };
			} else if (dog_index == 3) {
				t = Texture{ "resources/dog/maltese.png" };
			}

			game.renderer.DrawTexture(
				t, V2_float::Random({ 0.0f, 0.0f }, game.window.GetSize()),
				V2_float::Random({ 10.0f, 10.0f }, V2_float{ 30.0f, 30.0f }), {}, {},
				Origin::TopLeft, Flip::None, 0.0f, {},
				0.0f //, Color{ 255, 255, 255, 30 }
			);
		}*/

		Draw();
	}

	void DrawWalls() {
		for (auto [e, p, s, h, origin, w] :
			 manager.EntitiesWith<Position, Size, Hitbox, Origin, WallComponent>()) {
			game.renderer.DrawRectangleHollow(p.p, h.size, h.color, origin, 1.0f);
		}
	}

	void DrawItems() {
		for (auto [e, p, s, h, o, ss, item] :
			 manager.EntitiesWith<Position, Size, Hitbox, Origin, SpriteSheet, ItemComponent>()) {
			if (draw_hitboxes) {
				game.renderer.DrawRectangleHollow(p.p + h.offset, h.size, h.color, o, 1.0f);
			}
			V2_float offset;
			if (item.held) {
				auto& hand{ player.Get<HandComponent>() };
				offset = { player.Get<Direction>().dir.x * hand.offset.x, hand.offset.y };
			}
			game.renderer.DrawTexture(
				ss.texture, p.p + offset, s.s, ss.source_pos, ss.source_size, Origin::Center,
				Flip::None, 0.0f, {}, e.Has<ZIndex>() ? e.Get<ZIndex>().z_index : 0.0f
			);
		}
	}

	void DrawDogs() {
		for (auto [e, p, s, o, ss, dog] :
			 manager.EntitiesWith<Position, Size, Origin, SpriteSheet, Dog>()) {
			/*if (draw_hitboxes) {
				game.renderer.DrawRectangleHollow(p.p + h.offset, h.size, h.color, o, 1.0f);
			}*/
			game.renderer.DrawTexture(
				ss.texture, p.p, s.s, ss.source_pos, ss.source_size, o,
				e.Has<Flip>() ? e.Get<Flip>() : Flip::None, 0.0f, {},
				e.Has<ZIndex>() ? e.Get<ZIndex>().z_index : 0.0f //, Color{ 255, 255, 255, 30 }
			);
			/*if (draw_hitboxes) {
				game.renderer.DrawLine(dog.start, dog.target, color::Red, 3.0f);
			}*/
		}
	}

	void DrawHumans() {
		for (auto [e, pos, size, hitbox, origin, t, flip, human] :
			 manager.EntitiesWith<Position, Size, Hitbox, Origin, SpriteSheet, Flip, Human>()) {
			if (e.Has<HandComponent>() && draw_hitboxes) {
				const auto& hand = e.Get<HandComponent>();
				game.renderer.DrawCircleHollow(hand.GetPosition(e), hand.radius, hitbox.color);
			}
			if (draw_hitboxes) {
				game.renderer.DrawRectangleHollow(
					pos.p + hitbox.offset, hitbox.size, color::Blue, origin, 1.0f
				);
			}
			game.renderer.DrawTexture(
				t.texture, pos.p, size.s, t.source_pos, t.source_size, origin, flip, 0.0f,
				{ 0.5f, 0.5f }, (e.Has<ZIndex>() ? e.Get<ZIndex>().z_index : 0.0f),
				(e.Has<TintColor>() ? e.Get<TintColor>().tint : color::White)
			);
		}
	}

	void DrawBackground() {
		game.renderer.DrawTexture(
			house_background, {}, house_background.GetSize(), {}, {}, Origin::TopLeft
		);
	}

	void WifeReturn() {
		player_can_move = false;
		game.tween.Get(Hash("player_movement_animation")).Pause();
		player.Remove<Velocity>();
		player.Remove<Acceleration>();
		player.Get<AnimationComponent>().column = 0;
		player.Get<SpriteSheet>().row			= 0;

		TweenConfig config;
		config.on_start = [](auto& tw, auto f) {
			game.sound.Get(Hash("door_open")).Play(-1);
		};
		milliseconds wife_return_duration{ 3000 };
		duration<float, milliseconds::period> love_popup_dur{ 400 };
		duration<float, milliseconds::period> love_duration{ wife_return_duration };

		config.on_update = [=](auto& tw, auto f) {
			if (f > 0.2f && !player.Get<Wife>().voice_heard) {
				player.Get<Wife>().voice_heard = true;

				SpawnBubbleAnimation(
					player, BubbleAnimation::Love, Hash(player) + Hash("bubble"), love_popup_dur,
					love_duration, [=](Tween& tw, float f) {},
					[=]() {
						V2_float pos = player.Get<Position>().p -
									   V2_float{ 0.0f, player.Get<Size>().s.y + 1.0f };
						return pos;
					},
					[=]() {}, [=](auto& tw, float f) {}
				);

				game.sound.Get(Hash("wife_arrives")).Play(-1);
			}
		};
		config.on_complete = [=](auto& tw, auto f) {
			fade_text.Get<FadeComponent>().state = FadeState::Win;
			game.tween.Get(Hash("fade_to_black")).Start();
		};
		auto& wife_tween =
			game.tween.Load(Hash("wife_return_tween"), 0.0f, 1.0f, wife_return_duration, config);
		wife_tween.Start();
	}

	void DrawProgressBar() {
		V2_float cs{ game.camera.GetPrimary().GetSize() };
		float y_offset{ 0.0f };
		V2_float bar_size{ progress_bar_texture.GetSize() };
		V2_float bar_pos{ cs.x / 2.0f, y_offset };
		game.renderer.DrawTexture(
			progress_bar_texture, bar_pos, bar_size, {}, {}, Origin::CenterTop
		);
		V2_float car_size{ progress_car_texture.GetSize() };
		V2_float start{ bar_pos.x - bar_size.x / 2.0f + car_size.x / 2,
						bar_pos.y + bar_size.y / 2.0f };
		V2_float end{ bar_pos.x + bar_size.x / 2.0f - car_size.x / 2,
					  bar_pos.y + bar_size.y / 2.0f };
		float elapsed{ return_timer.ElapsedPercentage(level_time) };
		if (elapsed >= 1.0f && !player.Get<Wife>().returned && player.Has<Velocity>()) {
			player.Get<Wife>().returned = true;
			WifeReturn();
		}
		V2_float car_pos{ Lerp(start, end, elapsed) };
		game.renderer.DrawTexture(progress_car_texture, car_pos, car_size, {}, {}, Origin::Center);
	}

	void DrawDogCounter() {
		std::size_t dog_count = manager.EntitiesWith<Dog>().Count();
		V2_float cs{ game.camera.GetPrimary().GetSize() };
		V2_float ui_offset{ -12.0f, 12.0f };
		V2_float counter_size{ dog_counter_texture.GetSize() };
		V2_float text_offset{ -counter_size.x / 2,
							  counter_size.y - 14.0f }; // relative to counter ui top right
		Text t{ counter_font, std::to_string(dog_count), color::Black };
		V2_float pos{ cs.x + ui_offset.x, ui_offset.y };
		V2_float counter_text_size{ 20, 25 };
		game.renderer.DrawTexture(dog_counter_texture, pos, counter_size, {}, {}, Origin::TopRight);
		t.Draw({ pos + text_offset, counter_text_size, Origin::Center });
	}

	ecs::Entity neighbor;
	V2_float neighbor_camera_pos{ 150, 0.0f };
	V2_float neighbor_walk_end_pos{ 150, 360.0f };

	// V2_float neighbor_pos{ 150, 0.0f };

	std::array<std::size_t, 3> yell_keys{ Hash("neighbor_yell0"), Hash("neighbor_yell1"),
										  Hash("neighbor_yell2") };

	void NeighborYell() {
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
	}

	void CreateNeighbor(milliseconds scene_duration) {
		neighbor = manager.CreateEntity();

		V2_int neighbor_animation_count{ 4, 1 };

		Texture neighbor_texture{ "resources/entity/neighbor.png" };

		// Must be added before AnimationComponent as it pauses the animation immediately.
		auto& spritesheet =
			neighbor.Add<SpriteSheet>(neighbor_texture, V2_int{}, neighbor_animation_count);

		V2_float neighbor_start_pos{ neighbor_camera_pos.x,
									 neighbor_camera_pos.y + spritesheet.source_size.y };

		auto& ppos = neighbor.Add<Position>(neighbor_start_pos);
		neighbor.Add<Velocity>(V2_float{}, V2_float{ 700.0f });
		neighbor.Add<Acceleration>(V2_float{}, V2_float{ 1200.0f });
		neighbor.Add<Origin>(Origin::CenterBottom);
		neighbor.Add<Flip>(Flip::None);
		neighbor.Add<Human>();
		neighbor.Add<TintColor>(color::White);

		milliseconds animation_duration{ 400 };

		TweenConfig animation_config;
		animation_config.repeat = -1;

		animation_config.on_pause = [=](auto& t, auto v) {
			neighbor.Get<AnimationComponent>().column = 0;
		};
		animation_config.on_update = [=](auto& t, auto v) {
			neighbor.Get<AnimationComponent>().column = static_cast<int>(std::floorf(v));
		};
		animation_config.on_repeat = [=](auto& t, auto v) {
			neighbor.Get<AnimationComponent>().column = 0;
		};

		auto tween = game.tween.Load(
			Hash("neighbor_movement_animation"), 0.0f,
			static_cast<float>(neighbor_animation_count.x), animation_duration, animation_config
		);
		tween.Start();

		auto& anim = neighbor.Add<AnimationComponent>(
			Hash("neighbor_movement_animation"), neighbor_animation_count.x, animation_duration
		);

		TweenConfig neighbor_scene_config;

		V2_float neighbot_walk_start_pos = ppos.p;

		std::size_t anger_key{ neighbor.GetId() + Hash("anger") };

		duration<float, milliseconds::period> bubble_pop_duration{ 300 };

		float yell_bubble_offset{ 2.0f };

		auto neighbor_complain_bubble =
			[=](BubbleAnimation anger, duration<float, milliseconds::period> bubble_hold_duration) {
				SpawnBubbleAnimation(
					neighbor, anger, anger_key, bubble_pop_duration,
					bubble_hold_duration + bubble_pop_duration,
					[=](Tween& tw, float f) { neighbor.Get<AngerComponent>().yelling = false; },
					[=]() {
						V2_float pos =
							neighbor.Get<Position>().p -
							V2_float{ 0.0f, neighbor.Get<Size>().s.y + yell_bubble_offset };
						return pos;
					},
					[=]() {
						auto& b{ neighbor.Get<AngerComponent>() };
						if (!b.yelling) {
							// TODO: Add a check that this only plays once upon hold start.
							NeighborYell();
							b.yelling = true;
						}
					},
					[=](auto& tw, float f) {
						auto& b{ neighbor.Get<AngerComponent>() };
						auto new_bubble =
							static_cast<BubbleAnimation>(static_cast<int>(b.bubble) + 1);
						if (new_bubble != BubbleAnimation::AngerStop) {
							b.bubble = new_bubble;
						}
					}
				);
			};

		const float anger_bubble_count{ 5.0f };

		neighbor.Add<AngerComponent>(BubbleAnimation::Anger0);

		neighbor_scene_config.on_update = [=](auto& tw, auto f) {
			float walk_frac{ 0.2f };
			float bubble_frac{ 1.0f - walk_frac };
			auto bubble_duration{ bubble_frac * (scene_duration - milliseconds{ 100 }) /
								  (anger_bubble_count + 1) };
			float walk_progress = std::clamp(f / walk_frac, 0.0f, 1.0f);
			auto& walk_anim		= neighbor.Get<AnimationComponent>();
			if (walk_progress < 1.0f) {
				walk_anim.Resume();
				neighbor.Get<Position>().p =
					Lerp(neighbot_walk_start_pos, neighbor_walk_end_pos, walk_progress);
			} else {
				walk_anim.Pause();
				neighbor.Get<TintColor>().tint = color::Red;
				neighbor_complain_bubble(neighbor.Get<AngerComponent>().bubble, bubble_duration);
			}
		};

		auto neighbor_scene = game.tween.Load(
			Hash("neighbor_animation"), 0.0f, 1.0f, scene_duration, neighbor_scene_config
		);
		neighbor_scene.Start();

		V2_int size{ tile_size.x, 2 * tile_size.y };

		auto& s = neighbor.Add<Size>(size);
		V2_float hitbox_size{ 8, 8 };
		neighbor.Add<Hitbox>(neighbor, hitbox_size, V2_float{ 0.0f, 0.0f });
		neighbor.Add<ZIndex>(1.0f);

		manager.Refresh();
	}

	void BackToMenu();

	void StartNeighborCutscene() {
		TweenConfig config;
		// Starting point for camera pan.
		auto player_pos = player.Get<Position>().p;

		player_can_move = false;

		game.tween.Get(Hash("player_movement_animation")).Pause();
		player.Remove<Velocity>();
		player.Remove<Acceleration>();
		player.Get<AnimationComponent>().column = 0;
		player.Get<SpriteSheet>().row			= 0;
		neighbor_camera_pos.y					= world_bounds.y;
		// player_camera.SetClampBounds({});

		config.on_update = [=](auto& tw, auto f) {
			// how far into the tween these things happen:
			const float shift_frac{ 0.2f };			 // move cam to neighbor.
			const float backshift_frac{ 0.8f };		 // move cam back to player.
			const float neighbor_move_start{ 0.1f }; // spawn and start moving neighbor.
			if (f > neighbor_move_start && neighbor == ecs::Entity{}) {
				float scene_frac{ backshift_frac - neighbor_move_start };
				PTGN_ASSERT(scene_frac >= 0.01f);
				CreateNeighbor(
					std::chrono::duration_cast<milliseconds>(tw.GetDuration() * scene_frac)
				);
				// TODO: Neighbor complaint stuff here.
			}
			if (f <= shift_frac || f >= backshift_frac) {
				float shift_progress = std::clamp(f / shift_frac, 0.0f, 1.0f);
				V2_float start		 = player_pos;
				V2_float end		 = neighbor_camera_pos;
				// Camera moving back and then forth between player and neighbor.
				if (f >= backshift_frac) {
					shift_progress =
						std::clamp((f - backshift_frac) / (1.0f - backshift_frac), 0.0f, 1.0f);
					start = neighbor_camera_pos;
					end	  = player.Get<Position>().p;
				}
				V2_float pos = Lerp(start, end, shift_progress);
				player_camera.SetPosition(pos);
			}
		};
		config.on_complete = [=](auto& tw, float v) {
			auto& fade							 = game.tween.Get(Hash("fade_to_black"));
			fade_text.Get<FadeComponent>().state = FadeState::Lose;
			fade.Start();
		};
		config.on_stop = config.on_complete;
		auto& t = game.tween.Load(Hash("neighbor_cutscene"), 0.0f, 1.0f, seconds{ 10 }, config);
		t.Start();
	}

	void DrawBarkometer() {
		V2_float meter_pos{ 25, 258 };
		V2_float meter_size{ barkometer_texture.GetSize() };

		float bark_progress = std::clamp(bark_count / bark_threshold, 0.0f, 1.0f);

		if (bark_progress >= 1.0f && neighbor == ecs::Entity{} && player.Has<Velocity>()) {
			StartNeighborCutscene();
		}

		// TODO: If bark_progress > 0.8f (etc) give a warning to player.

		Color color = Lerp(color::Grey, color::Red, bark_progress);

		V2_float border_size{ 4, 4 };

		V2_float barkometer_fill_size{ meter_size - border_size * 2.0f };

		V2_float fill_pos{ meter_pos.x, meter_pos.y - border_size.y };

		game.renderer.DrawTexture(
			barkometer_texture, meter_pos, meter_size, {}, {}, Origin::CenterBottom
		);

		game.renderer.DrawRectangleFilled(
			fill_pos, { barkometer_fill_size.x, barkometer_fill_size.y * bark_progress }, color,
			Origin::CenterBottom
		);
	}

	void DrawUI() {
		auto prev_primary = game.camera.GetPrimary();

		game.renderer.Flush();

		OrthographicCamera c;
		c.SetPosition(game.window.GetCenter());
		c.SetSizeToWindow();
		c.SetClampBounds({});
		game.camera.SetPrimary(c);

		// Draw UI here...

		DrawProgressBar();
		DrawDogCounter();
		DrawBarkometer();
		DrawFadeEntity();

		game.renderer.Flush();

		if (game.camera.GetPrimary() == c) {
			game.camera.SetPrimary(prev_primary);
		}
	}

	Texture win{ "resources/ui/win.png" };
	Texture lose{ "resources/ui/lose.png" };

	void DrawFadeEntity() {
		PTGN_ASSERT(fade_entity.Has<TintColor>());
		auto& cam = game.camera.GetPrimary();
		game.renderer.DrawRectangleFilled(
			{ {}, cam.GetSize(), Origin::TopLeft }, fade_entity.Get<TintColor>().tint
		);
		auto fade_state = fade_text.Get<FadeComponent>().state;
		if (fade_state != FadeState::None) {
			Texture t = lose;
			if (fade_state == FadeState::Win) {
				t = win;
			}
			game.renderer.DrawTexture(
				t, { cam.GetPosition().x, cam.GetPosition().y }, cam.GetSize(), {}, {},
				Origin::Center, Flip::None, 0.0f, {}, 1.0f, fade_text.Get<TintColor>().tint
			);
		}
	}

	void Draw() {
		// For debugging purposes:
		if (draw_hitboxes) {
			DrawWalls();
		}

		DrawHumans();
		DrawDogs();
		DrawItems();

		for (auto [e, d] : manager.EntitiesWith<Dog>()) {
			if (d.spawn_thingy) {
				d.SpawnRequestAnimation(e, d.req);
				d.spawn_thingy = false;
			}
			if (IsRequest(d.request) && !d.patience.IsRunning()) {
				d.patience.Start();
			}
			if (d.patience.ElapsedPercentage(d.patience_duration) >= 1.0f) {
				d.Bark(e);
				bark_count++;
				d.patience.Start();
			}
			auto& h	 = e.Get<Hitbox>();
			auto& ph = player.Get<Hitbox>();
			Rectangle<float> dog_rect{ h.GetPosition(), h.size, e.Get<Origin>() };
			Rectangle<float> player_rect{ ph.GetPosition(), ph.size, player.Get<Origin>() };
			if (game.collision.overlap.RectangleRectangle(player_rect, dog_rect) &&
				player.Has<HandComponent>()) {
				ecs::Entity item{ player.Get<HandComponent>().current_item };

				auto reset_patience_request = [&]() {
					d.patience.Reset();
					d.patience.Stop();
					d.request = BubbleAnimation::None;
					// PTGN_INFO("Resetting dog patience!");
				};

				if (item != ecs::Entity{}) {
					if (item.Has<ItemComponent>()) {
						auto& i = item.Get<ItemComponent>();

						if (d.request == i.type) {
							switch (i.type) {
								case BubbleAnimation::Food: {
									reset_patience_request();
									break;
								}
								case BubbleAnimation::Bone: {
									reset_patience_request();
									break;
								}
								case BubbleAnimation::Cleanup: {
									reset_patience_request();
									break;
								}
								case BubbleAnimation::Toy: {
									reset_patience_request();
									break;
								}
								case BubbleAnimation::Outside: {
									reset_patience_request();
									break;
								}
							}
						}
					}
				} else if (d.request == BubbleAnimation::Pet) {
					reset_patience_request();
				}
			}
		}

		if (bark_timer.ElapsedPercentage(bark_reset_time) >= 1.0f) {
			bark_count--;
			bark_timer.Reset();
			bark_timer.Start();
		}

		// TODO: Move out of here.
		// TODO: Remove bark button.
		if (game.input.KeyDown(Key::B)) {
			bark_count += 30.0f;
			for (auto [e, d] : manager.EntitiesWith<Dog>()) {
				d.Bark(e);
			}
		}

		DrawUI();
	}
};

struct TextButton {
	TextButton(const std::shared_ptr<Button>& button, const Text& text) :
		button{ button }, text{ text } {}

	std::shared_ptr<Button> button;
	Text text;
};

const int button_y_offset{ 14 };
const V2_int button_size{ 250, 50 };
const V2_int first_button_coordinate{ 250, 220 };

TextButton CreateMenuButton(
	const std::string& content, const Color& text_color, const ButtonActivateFunction& f,
	const Color& color, const Color& hover_color
) {
	ColorButton b;
	b.SetOnActivate(f);
	b.SetColor(color);
	b.SetHoverColor(hover_color);
	Text text{ Hash("menu_font"), content, color };
	return TextButton{ std::make_shared<ColorButton>(b), text };
}

class LevelSelect : public Scene {
public:
	std::vector<TextButton> buttons;

	bool fade_in_on_init{ false };

	LevelSelect(bool f = false) : fade_in_on_init{ f } {}

	void StartGame(Difficulty difficulty) {
		game.scene.RemoveActive(Hash("level_select"));
		game.scene.Load<GameScene>(Hash("game"), difficulty);
		game.scene.SetActive(Hash("game"));
	}

	ecs::Manager manager;

	ecs::Entity fade_entity;
	TweenConfig fade_to_black_config_select;
	OrthographicCamera camera;

	void Init() final {
		camera.SetSizeToWindow();
		camera.SetPosition(game.window.GetCenter());
		game.camera.SetPrimary(camera);
		if (fade_in_on_init) {
			// fade_entity = manager.CreateEntity();
			// fade_entity.Add<TintColor>(Color{ 0, 0, 0, 255 });

			// fade_to_black_config_select.on_update = [=](auto& tw, float f) {
			//	fade_entity.Get<TintColor>().tint.a =
			//		static_cast<std::uint8_t>((1.0f - f) * 255.0f);
			//	// PTGN_LOG("Update Tint: ", fade_entity.Get<TintColor>().tint, ", f: ", f);
			// };
			// fade_to_black_config_select.on_complete = [=](auto& tw, float f) {
			//	fade_entity.Get<TintColor>().tint.a = 0;
			//	// PTGN_LOG("Complete Tint: ", fade_entity.Get<TintColor>().tint, ", f: ", f);
			// };
			// fade_to_black_config_select.on_stop = fade_to_black_config_select.on_complete;

			// milliseconds fade_duration{ 500 };

			// game.tween
			//	.Load(
			//		Hash("fade_to_black_level_select"), 0.0f, 1.0f, fade_duration,
			//		fade_to_black_config_select
			//	)
			//	.Start();

			//// TODO: Add fade in.
			// fade_in_on_init = false;
		}
		buttons.clear();
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
			[]() {
				game.scene.RemoveActive(Hash("level_select"));
				game.scene.SetActive(Hash("main_menu"));
			},
			color::LightGrey, color::Black
		));

		for (int i = 0; i < (int)buttons.size(); i++) {
			buttons[i].button->SetRectangle({ V2_int{ first_button_coordinate.x,
													  first_button_coordinate.y +
														  i * (button_size.y + button_y_offset) },
											  button_size, Origin::CenterTop });
			buttons[i].button->SubscribeToMouseEvents();
		}
	}

	void Shutdown() final {
		for (int i = 0; i < (int)buttons.size(); i++) {
			buttons[i].button->UnsubscribeFromMouseEvents();
		}
		manager.Clear();
	}

	void Update() final {
		game.scene.Get(Hash("level_select"))->camera.SetPrimary(camera);

		game.renderer.DrawTexture(
			game.texture.Get(Hash("menu_background")), game.window.GetCenter(), resolution, {}, {},
			Origin::Center, Flip::None, 0.0f, {}, -1.0f
		);
		for (std::size_t i = 0; i < buttons.size(); i++) {
			buttons[i].button->DrawHollow(6.0f);
			auto rect = buttons[i].button->GetRectangle();
			rect.size.x =
				buttons[i].text.GetSize(Hash("menu_font"), buttons[i].text.GetContent()).x * 0.5f;
			buttons[i].text.Draw(rect);
		}

		if (fade_entity.IsAlive() && fade_entity.Has<TintColor>()) {
			game.renderer.DrawRectangleFilled(
				{ {}, game.window.GetSize(), Origin::TopLeft }, fade_entity.Get<TintColor>().tint
			);
		}
	}
};

void GameScene::BackToMenu() {
	game.scene.Unload(Hash("game"));
	game.scene.Get<LevelSelect>(Hash("level_select"))->fade_in_on_init = true;
	game.scene.SetActive(Hash("level_select"));
}

class MainMenu : public Scene {
public:
	std::vector<TextButton> buttons;

	MainMenu() {
		// TODO: If has.
		game.font.Load(Hash("menu_font"), "resources/font/retro_gaming.ttf", button_size.y);
		game.texture.Load(Hash("menu_background"), "resources/ui/background.png");
		game.music.Load(Hash("background_music"), "resources/sound/background_music.ogg").Play(-1);
		game.scene.Load<LevelSelect>(Hash("level_select"));
	}

	void Init() final {
		buttons.clear();
		buttons.push_back(CreateMenuButton(
			"Play", color::Blue,
			[]() {
				game.scene.RemoveActive(Hash("main_menu"));
				game.scene.SetActive(Hash("level_select"));
			},
			color::Blue, color::Black
		));
		// buttons.push_back(CreateMenuButton(
		//	"Settings", color::Red,
		//	[]() {
		//		/*game.scene.RemoveActive(Hash("main_menu"));
		//		game.scene.SetActive(Hash("game"));*/
		//	},
		//	color::Red, color::Black
		//));

		for (int i = 0; i < (int)buttons.size(); i++) {
			buttons[i].button->SetRectangle({ V2_int{ first_button_coordinate.x,
													  first_button_coordinate.y +
														  i * (button_size.y + button_y_offset) },
											  button_size, Origin::CenterTop });
			buttons[i].button->SubscribeToMouseEvents();
		}
	}

	void Shutdown() final {
		for (int i = 0; i < (int)buttons.size(); i++) {
			buttons[i].button->UnsubscribeFromMouseEvents();
		}
	}

	void Update() final {
		game.renderer.DrawTexture(
			game.texture.Get(Hash("menu_background")), game.window.GetCenter(), resolution, {}, {},
			Origin::Center, Flip::None, 0.0f, {}, -1.0f
		);
		for (std::size_t i = 0; i < buttons.size(); i++) {
			buttons[i].button->DrawHollow(7.0f);
			auto rect = buttons[i].button->GetRectangle();
			rect.size.x =
				buttons[i].text.GetSize(Hash("menu_font"), buttons[i].text.GetContent()).x * 0.5f;
			buttons[i].text.Draw(rect);
		}
		// TODO: Make this a texture and global (perhaps run in the start scene?).
		// Draw Mouse Cursor.
		// game.renderer.DrawCircleFilled(game.input.GetMousePosition(), 5.0f, color::Red);
	}
};

class SetupScene : public Scene {
public:
	SetupScene() {}

	void Init() final {
		game.renderer.SetClearColor(color::Silver);
		game.window.SetSize(resolution);

		std::size_t initial_scene{ Hash("main_menu") };
		game.scene.Load<MainMenu>(initial_scene);
		game.scene.SetActive(initial_scene);
	}
};

int main() {
	game.Start<SetupScene>();
	return 0;
}
