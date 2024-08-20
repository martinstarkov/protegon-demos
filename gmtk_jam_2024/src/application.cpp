#include "protegon/protegon.h"

using namespace ptgn;

constexpr const V2_int resolution{ 960, 540 };
constexpr const V2_int center{ resolution / 2 };
constexpr const bool draw_hitboxes{ true };

struct WallComponent {};

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
	float weight_factor{ 1.0f };
};

struct SortByZ {};

struct Position {
	Position(const V2_float& pos) : p{ pos } {}

	/*V2_float GetPosition(ecs::Entity e) const {
		V2_float offset;
		if (e.Has<SpriteSheet>() && e.Has<Origin>()) {
			offset = GetDrawOffset(e.Get<SpriteSheet>().source_size / scale, e.Get<Origin>());
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
	PTGN_ASSERT(e.Has<Position>());
	PTGN_ASSERT(e.Has<Hitbox>());
	PTGN_ASSERT(e.Has<Origin>());
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

struct GridComponent {
	GridComponent(const V2_int& cell) : cell{ cell } {}

	V2_int cell;
};

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

enum class DogRequest {
	None,
	Food,
	Cleanup,
	Outside,
	Toy,
	Pet
};

struct Dog {
	ecs::Entity dog;

	Dog(ecs::Entity dog_entity, const V2_float& start_target, std::size_t walk,
		const std::vector<std::size_t>& bark_keys, std::size_t whine_key,
		const V2_float& bark_offset) :
		dog{ dog_entity },
		target{ start_target },
		walk{ walk },
		bark_keys{ bark_keys },
		whine_key{ whine_key },
		bark_offset{ bark_offset } {
		animations_to_goal = dog.Get<AnimationComponent>().column_count;
		for (std::size_t i = 0; i < bark_keys.size(); i++) {
			PTGN_ASSERT(game.sound.Has(bark_keys[i]));
		}
	}

	void SpawnBarkAnimation() {
		std::size_t bark_tween_key{ dog.GetId() + Hash("bark") };
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
			auto& h{ dog.Get<Hitbox>() };
			auto flip{ dog.Get<Flip>() };
			float sign	= (flip == Flip::Horizontal ? -1.0f : 1.0f);
			auto offset = GetDrawOffset(h.size, dog.Get<Origin>());
			V2_float pos{ dog.Get<Position>().p + offset +
						  V2_float{ sign * (h.size.x / 2 + bark_offset.x), -bark_offset.y } };
			// TODO: Move this to draw functions and use an entity instead.
			game.renderer.DrawTexture(
				t, pos, source_size, source_pos, source_size, dog.Get<Origin>(), flip, 0.0f, {},
				0.8f
			);
		};
		auto& tween = game.tween.Load(bark_tween_key, 0.0f, 1.0f, milliseconds{ 135 }, config);
		tween.Start();
	}

	void SpawnRequestAnimation(DogRequest dog_request) {
		std::size_t request_tween_key{ dog.GetId() + Hash("request") };
		if (game.tween.Has(request_tween_key)) {
			// Already in the middle of a request animation.
			// This means a dog cannot request multiple things at the same time (probably a good
			// thing :) ).
			return;
		}
		// Setup new request.
		request = dog_request;
		// TODO: Consider using an entity component?
		TweenConfig config;

		float start_hold_threshold{ request_animation_popup_duration / total_request_duration };

		config.on_complete = [=](Tween& tw, float f) {
			dog.Get<Dog>().whined = false;
		};

		config.on_update = [=](Tween& tw, float f) {
			std::size_t request_texture_key{ Hash("request") + static_cast<std::size_t>(request) };
			const int request_animation_count{ 4 };
			Texture t{ game.texture.Get(request_texture_key) };
			V2_float source_size{ t.GetSize() / V2_float{ request_animation_count, 1 } };

			const int hold_frame{ request_animation_count - 1 };

			float column = 0.0f;

			if (f >= start_hold_threshold) {
				// Whine once after request has finished popping up and is just starting its hold
				// phase.
				if (!dog.Get<Dog>().whined) {
					Whine();
					dog.Get<Dog>().whined = true;
				}
				column = hold_frame;
			} else {
				column = std::floorf(f / start_hold_threshold * hold_frame);
			}

			V2_float source_pos = { column * source_size.x, 0.0f };
			auto& h{ dog.Get<Hitbox>() };
			auto flip{ dog.Get<Flip>() };
			float sign	= (flip == Flip::Horizontal ? -1.0f : 1.0f);
			auto offset = GetDrawOffset(h.size, dog.Get<Origin>());
			V2_float pos{ dog.Get<Position>().p + offset +
						  V2_float{ sign * (h.size.x / 2 + bark_offset.x), -bark_offset.y } +
						  V2_float{ sign * request_offset.x, -request_offset.y } };
			// TODO: Move this to draw functions and use an entity instead.
			// Origin o{ (flip == Flip::Horizontal ? Origin::BottomRight :
			// Origin::BottomLeft) };
			Origin o{ dog.Get<Origin>() };
			game.renderer.DrawTexture(
				t, pos, source_size, source_pos, source_size, o, flip, 0.0f, {}, 0.9f
			);
		};

		auto& tween = game.tween.Load(
			request_tween_key, 0.0f, 1.0f,
			std::chrono::duration_cast<milliseconds>(total_request_duration), config
		);
		tween.Start();
	}

	void Bark() {
		PTGN_ASSERT(bark_keys.size() > 0);
		RNG<std::size_t> bark_rng{ 0, bark_keys.size() - 1 };
		auto bark_index{ bark_rng() };
		PTGN_ASSERT(bark_index < bark_keys.size());
		auto bark_key{ bark_keys[bark_index] };
		PTGN_ASSERT(game.sound.Has(bark_key));
		game.sound.Get(bark_key).Play(0);
		SpawnBarkAnimation();
	}

	void Whine() {
		PTGN_ASSERT(game.sound.Has(whine_key));
		game.sound.Get(whine_key).Play(0);
	}

	void Update(float progress) {
		if (lingering) {
			// TODO: Different animation?
			// dog.Get<SpriteSheet>().row = X:
			// auto& an{ dog.Get<AnimationComponent>() };
			// an.column = static_cast<int>(an.column_count * f) %
			// an.column_count;
		} else {
			// dog.Get<SpriteSheet>().row = 0:
			dog.Get<Position>().p = Lerp(start, target, progress) - dog.Get<Hitbox>().offset;
			ApplyBounds(dog, game.texture.Get(Hash("house_background")).GetSize());
			ResolveStaticWallCollisions(dog);
			if (draw_hitboxes) {
				game.renderer.DrawLine(start, target, color::Purple, 5.0f);
			}
			auto& an{ dog.Get<AnimationComponent>() };
			an.column = static_cast<int>(animations_to_goal * progress) % an.column_count;
		}
	}

	void StartWalk() {
		if (start.IsZero()) {
			start  = dog.Get<Hitbox>().GetPosition();
			target = start;
		}
		ResolveStaticWallCollisions(dog);
		// Reset lingering state.
		lingering = false;
		SetNewTarget();
		// Reset animation.
		dog.Get<AnimationComponent>().column = 0;
	}

	void SetNewTarget() {
		start = target;
		static V2_float min{ 0.0f, 0.0f };
		static V2_float max = game.texture.Get(Hash("house_background")).GetSize();

		static float max_length{ (max - min).MagnitudeSquared() };
		PTGN_ASSERT(max_length != 0.0f);

		auto viable_path = [=](const V2_float& vel) {
			auto& hitbox{ dog.Get<Hitbox>() };
			Rectangle<float> dog_rect{ hitbox.GetPosition(), hitbox.size, dog.Get<Origin>() };
			for (auto [e, p, h, o, s, w] : GetWalls(dog.GetManager())) {
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
			if (viable_path(potential_velocity) && !OutOfBounds(dog, future_loc, max)) {
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
				dog.Get<Flip>() = Flip::Horizontal;
			} else {
				dog.Get<Flip>() = Flip::None;
			}

			float length{ (target - start).MagnitudeSquared() };

			float speed_ratio = std::sqrtf(length / max_length);

			if (run) {
				speed_ratio /= run_factor;
			}

			duration<float> path_duration{ diagonal_time * speed_ratio };

			if (NearlyEqual(path_duration.count(), 0.0f)) {
				path_duration = milliseconds{ 100 };
			}

			PTGN_ASSERT(path_duration > microseconds{ 100 });
			tween.SetDuration(std::chrono::duration_cast<milliseconds>(path_duration));

			auto& anim{ dog.Get<AnimationComponent>() };

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

	DogRequest request{ DogRequest::None };

	// For debugging purposes.
	V2_float potential_target;

	// Pixels from corner of hitbox that the mouth of the dog is.
	V2_float bark_offset;
	duration<float, seconds::period> request_animation_hold_duration{ 5 };
	duration<float, milliseconds::period> request_animation_popup_duration{ 500 };

	duration<float, milliseconds::period> total_request_duration{ request_animation_popup_duration +
																  request_animation_hold_duration };

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

	Timer return_timer;

	seconds level_time{ 60 };

	ecs::Entity player;
	ecs::Entity bowl;
	ecs::Entity dog_toy1;
	OrthographicCamera player_camera;

	V2_float world_bounds;

	GameScene() {
		progress_bar_texture = Surface{ "resources/ui/progress_bar.png" };
		progress_car_texture = Surface{ "resources/ui/progress_car.png" };
		level				 = Surface{ "resources/level/house_hitbox.png" };

		game.texture.Load(Hash("bark"), "resources/entity/bark.png");

		std::size_t r{ Hash("request") };
		auto load_request = [&](DogRequest request, const std::string& type) {
			path p{ "resources/ui/request_" + type + ".png" };
			PTGN_ASSERT(FileExists(p));
			game.texture.Load(r + static_cast<std::size_t>(request), p);
		};
		load_request(DogRequest::Food, "food");
		load_request(DogRequest::Cleanup, "cleanup");
		load_request(DogRequest::Outside, "outside");
		load_request(DogRequest::Toy, "toy");
		load_request(DogRequest::Pet, "pet");

		house_background = game.texture.Load(Hash("house_background"), "resources/level/house.png");
		world_bounds	 = house_background.GetSize();
		// TODO: Populate with actual barks.
		game.sound.Load(Hash("vizsla_bark1"), "resources/sound/random_bark.ogg");
		game.sound.Load(Hash("great_dane_bark1"), "resources/sound/random_bark.ogg");
		game.sound.Load(Hash("maltese_bark1"), "resources/sound/random_bark.ogg");
		game.sound.Load(Hash("dachshund_bark1"), "resources/sound/random_bark.ogg");
		// TODO: Populate with actual whines.
		game.sound.Load(Hash("vizsla_whine"), "resources/sound/random_whine.ogg");
		game.sound.Load(Hash("great_dane_whine"), "resources/sound/random_whine.ogg");
		game.sound.Load(Hash("maltese_whine"), "resources/sound/random_whine.ogg");
		game.sound.Load(Hash("dachshund_whine"), "resources/sound/random_whine.ogg");
	}

	void CreatePlayer() {
		player = manager.CreateEntity();

		auto& ppos = player.Add<Position>(V2_float{ 215, 290 });
		player.Add<Velocity>(V2_float{}, V2_float{ 700.0f });
		player.Add<Acceleration>(V2_float{}, V2_float{ 1200.0f });
		player.Add<Origin>(Origin::CenterBottom);
		player.Add<Flip>(Flip::None);
		player.Add<Direction>(V2_int{ 0, 1 });

		V2_int player_animation_count{ 4, 3 };

		// Must be added before AnimationComponent as it pauses the animation immediately.
		player.Add<SpriteSheet>(
			Texture{ "resources/entity/player.png" }, V2_int{}, player_animation_count
		);

		milliseconds animation_duration{ 400 };

		TweenConfig animation_config;
		animation_config.repeat = -1;

		animation_config.on_pause = [&](auto& t, auto v) {
			player.Get<AnimationComponent>().column = 0;
		};
		animation_config.on_update = [&](auto& t, auto v) {
			player.Get<AnimationComponent>().column = static_cast<int>(std::floorf(v));
		};
		animation_config.on_repeat = [&](auto& t, auto v) {
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
		player.Add<GridComponent>(ppos.p / grid_size);
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

		auto start_walk = std::function([=](Tween& tw, float f) { dog.Get<Dog>().StartWalk(); });

		tween_config.on_pause =
			std::function([=](Tween& tw, float f) { dog.Get<AnimationComponent>().column = 0; });
		tween_config.on_start  = start_walk;
		tween_config.on_resume = start_walk;
		tween_config.on_repeat = start_walk;
		tween_config.on_update =
			std::function([=](Tween& tw, float f) { dog.Get<Dog>().Update(f); });
		dog.Add<Size>(size);
		dog.Add<InteractHitbox>(dog, size);
		dog.Add<Hitbox>(dog, hitbox_size, hitbox_offset);
		dog.Add<Origin>(Origin::CenterBottom);
		dog.Add<Position>(pos);
		dog.Add<SpriteSheet>(t, V2_int{}, animation_count);
		dog.Add<SortByZ>();
		dog.Add<ZIndex>(0.0f);
		dog.Add<Velocity>(V2_float{}, V2_float{ 700.0f });
		dog.Add<Acceleration>(V2_float{}, V2_float{ 1200.0f });
		dog.Add<DynamicCollisionShape>(DynamicCollisionShape::Rectangle);
		dog.Add<Flip>(Flip::None);
		dog.Add<AnimationComponent>(Hash(dog), animation_count.x, milliseconds{ 2000 });
		dog.Add<Dog>(dog, V2_float{}, Hash(dog), bark_sound_keys, whine_sound_key, bark_offset);

		auto& tween = game.tween.Load(Hash(dog), 0.0f, 1.0f, seconds{ 1 }, tween_config);
		tween.Start();

		manager.Refresh();

		return dog;
	}

	ecs::Entity CreateItem(
		const V2_float& pos, const path& texture, float hitbox_scale = 1.0f,
		float weight_factor = 1.0f
	) {
		auto item = manager.CreateEntity();

		Texture t{ texture };
		V2_int texture_size{ t.GetSize() };

		auto& i			= item.Add<ItemComponent>();
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
		wall.Add<GridComponent>(cell);
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
			{ Hash("vizsla_bark1") }, Hash("vizsla_whine"), V2_float{ 8, 17 }
		);
	}

	void CreateGreatDane(const V2_float& pos) {
		CreateDog(
			pos, "resources/dog/great_dane.png", V2_float{ 34, 8 }, V2_float{ -4, 0 },
			V2_int{ 6, 1 }, { Hash("great_dane_bark1") }, Hash("great_dane_whine"),
			V2_float{ 11, 24 }
		);
	}

	void CreateMaltese(const V2_float& pos) {
		CreateDog(
			pos, "resources/dog/maltese.png", V2_float{ 11, 6 }, V2_float{ -2, 0 }, V2_int{ 4, 1 },
			{ Hash("maltese_bark1") }, Hash("maltese_whine"), V2_float{ 6, 5 }
		);
	}

	void CreateDachshund(const V2_float& pos, const std::string& suffix) {
		path p{ "resources/dog/dachshund_" + suffix + ".png" };
		PTGN_ASSERT(FileExists(p), "Could not find specified dachshund type");
		CreateDog(
			pos, p, V2_float{ 14, 5 }, V2_float{ -3, 0 }, V2_int{ 4, 1 },
			{ Hash("dachshund_bark1") }, Hash("dachshund_whine"), V2_float{ 7, 3 }
		);
	}

	void Init() final {
		CreatePlayer();

		player_camera = game.camera.Load(Hash("player_camera"));
		// player_camera.SetSizeToWindow();
		player_camera.SetSize(game.window.GetSize() / 2.0f);
		game.camera.SetPrimary(Hash("player_camera"));
		player_camera.SetClampBounds({ {}, world_bounds, Origin::TopLeft });

		player_camera.SetPosition(player.Get<Position>().p);

		/*
		camera_motion = game.tween.Load(Hash("camera_tween"), 0.0f, 800.0f, seconds{ 10 });
		camera_motion.SetCallback(TweenEvent::Update, [&](auto& t, auto v) {
			player_camera.SetPosition(V2_float{ v, v });
		});
		*/

		level.ForEachPixel([&](const V2_int& cell, const Color& color) {
			if (color == color::Black) {
				CreateWall(V2_int{ 8, 8 }, cell);
			}
		});

		bowl	 = CreateItem({ 310, 300 }, "resources/entity/bowl.png", 1.0f, 0.7f);
		dog_toy1 = CreateItem({ 600, 220 }, "resources/entity/dog_toy1.png", 1.0f, 0.9f);
		CreateItem({ 500, 230 }, "resources/entity/dog_toy2.png", 1.0f, 1.0f);
		CreateItem({ 220, 280 }, "resources/entity/dog_toy2.png", 1.0f, 1.0f);

		CreateGreatDane({ 514 / 2, 232 / 2 });

		/*CreateVizsla({ 300, 300 });

		CreateGreatDane({ 270 * 2, 385 * 2 });
		CreateMaltese({ 490, 232 });
		CreateDachshund({ 600, 232 }, "purple");
		CreateDachshund({ 550, 232 }, "purple");*/

		// TODO: Move elsewhere, perhaps react to some event.
		return_timer.Start();
	}

	void PlayerMovementInput(float dt) {
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

		ApplyBounds(player, world_bounds);
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

		std::sort(ev.begin(), ev.end(), [&](ecs::Entity a, ecs::Entity b) {
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
		DrawBackground();
		// Camera follows the player.
		player_camera.SetPosition(player.Get<Position>().p);

		ResetHitboxColors();

		PlayerMovementInput(dt);

		UpdatePhysics(dt);

		UpdatePlayerHand();

		// UpdateZIndices();

		UpdateAnimations();

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
		for (auto [e, p, s, h, o, ss, dog] :
			 manager.EntitiesWith<Position, Size, Hitbox, Origin, SpriteSheet, Dog>()) {
			if (draw_hitboxes) {
				game.renderer.DrawRectangleHollow(p.p + h.offset, h.size, h.color, o, 1.0f);
			}
			game.renderer.DrawTexture(
				ss.texture, p.p, s.s, ss.source_pos, ss.source_size, o, e.Get<Flip>(), 0.0f, {},
				e.Has<ZIndex>() ? e.Get<ZIndex>().z_index : 0.0f //, Color{ 255, 255, 255, 30 }
			);
			if (draw_hitboxes) {
				game.renderer.DrawLine(dog.start, dog.target, color::Red, 3.0f);
			}
		}
	}

	void DrawPlayer() {
		const auto& pos	   = player.Get<Position>().p;
		const auto& size   = player.Get<Size>().s;
		const auto& hitbox = player.Get<Hitbox>();
		const auto& hand   = player.Get<HandComponent>();
		const auto origin  = player.Get<Origin>();
		const auto& t	   = player.Get<SpriteSheet>();
		const auto& flip   = player.Get<Flip>();
		const auto& dir	   = player.Get<Direction>();

		if (draw_hitboxes) {
			game.renderer.DrawCircleHollow(hand.GetPosition(player), hand.radius, hitbox.color);
			game.renderer.DrawRectangleHollow(
				pos + hitbox.offset, hitbox.size, color::Blue, origin, 1.0f
			);
		}
		game.renderer.DrawTexture(
			t.texture, pos, size, t.source_pos, t.source_size, origin, flip, 0.0f, { 0.5f, 0.5f },
			player.Has<ZIndex>() ? player.Get<ZIndex>().z_index : 0.0f
		);
	}

	void DrawBackground() {
		game.renderer.DrawTexture(
			house_background, {}, house_background.GetSize(), {}, {}, Origin::TopLeft
		);
	}

	void DrawProgressBar() {
		V2_float cs{ game.camera.GetPrimary().GetSize() };
		float y_offset{ 12.0f };
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
		static bool over = false;
		if (elapsed >= 1.0f && !over) {
			over = true;
			// Trigger wife return event.
		}
		V2_float car_pos{ Lerp(start, end, elapsed) };
		game.renderer.DrawTexture(progress_car_texture, car_pos, car_size, {}, {}, Origin::Center);
	}

	void DrawUI() {
		game.renderer.Flush();
		OrthographicCamera c;
		c.SetSizeToWindow();
		game.camera.SetPrimary(c);

		// Draw UI here...

		DrawProgressBar();
		game.renderer.Flush();

		game.camera.SetPrimary(Hash("player_camera"));
	}

	void Draw() {
		// For debugging purposes:
		if (draw_hitboxes) {
			DrawWalls();
		}

		DrawPlayer();
		DrawDogs();
		DrawItems();

		if (game.input.KeyDown(Key::F)) {
			for (auto [e, d] : manager.EntitiesWith<Dog>()) {
				d.SpawnRequestAnimation(DogRequest::Food);
			}
		}
		if (game.input.KeyDown(Key::O)) {
			for (auto [e, d] : manager.EntitiesWith<Dog>()) {
				d.SpawnRequestAnimation(DogRequest::Outside);
			}
		}
		if (game.input.KeyDown(Key::P)) {
			for (auto [e, d] : manager.EntitiesWith<Dog>()) {
				d.SpawnRequestAnimation(DogRequest::Pet);
			}
		}
		if (game.input.KeyDown(Key::C)) {
			for (auto [e, d] : manager.EntitiesWith<Dog>()) {
				d.SpawnRequestAnimation(DogRequest::Cleanup);
			}
		}
		if (game.input.KeyDown(Key::T)) {
			for (auto [e, d] : manager.EntitiesWith<Dog>()) {
				d.SpawnRequestAnimation(DogRequest::Toy);
			}
		}

		// TODO: Move out of here.
		// TODO: Remove bark button.
		if (game.input.KeyDown(Key::B)) {
			for (auto [e, d] : manager.EntitiesWith<Dog>()) {
				d.Bark();
			}
		}

		DrawUI();
	}
};

class MainMenu : public Scene {
public:
	struct TextButton {
		TextButton(const std::shared_ptr<Button>& button, const Text& text) :
			button{ button }, text{ text } {}

		std::shared_ptr<Button> button;
		Text text;
	};

	std::vector<TextButton> buttons;

	Texture background;

	MainMenu() {}

	void Init() final {
		game.scene.Load<GameScene>(Hash("game"));

		const int button_y_offset{ 14 };
		const V2_int button_size{ 192, 48 };
		const V2_int first_button_coordinate{ 161, 193 };

		Font font{ "resources/font/retro_gaming.ttf", button_size.y };

		auto add_solid_button = [&](const std::string& content, const Color& text_color,
									const ButtonActivateFunction& f, const Color& color,
									const Color& hover_color) {
			ColorButton b;
			b.SetOnActivate(f);
			b.SetColor(color);
			b.SetHoverColor(hover_color);
			Text text{ font, content, color };
			buttons.emplace_back(std::make_shared<ColorButton>(b), text);
		};

		add_solid_button(
			"Play", color::Blue,
			[]() {
				game.scene.Unload(Hash("main_menu"));
				game.scene.SetActive(Hash("game"));
			},
			color::Blue, color::Black
		);
		add_solid_button(
			"Instructions", color::Green,
			[]() {
				game.scene.Unload(Hash("main_menu"));
				game.scene.SetActive(Hash("game"));
			},
			color::Green, color::Black
		);
		add_solid_button(
			"Settings", color::Red,
			[]() {
				game.scene.Unload(Hash("main_menu"));
				game.scene.SetActive(Hash("game"));
			},
			color::Red, color::Black
		);

		for (int i = 0; i < (int)buttons.size(); i++) {
			buttons[i].button->SetRectangle({ V2_int{ first_button_coordinate.x,
													  first_button_coordinate.y +
														  i * (button_size.y + button_y_offset) },
											  button_size, Origin::CenterTop });
			buttons[i].button->SubscribeToMouseEvents();
		}

		background = Texture{ "resources/ui/background.png" };
	}

	void Update() final {
		game.renderer.DrawTexture(background, game.window.GetCenter(), resolution);
		for (std::size_t i = 0; i < buttons.size(); i++) {
			buttons[i].button->DrawHollow(3.0f);
			buttons[i].text.Draw(buttons[i].button->GetRectangle());
		}
		// TODO: Make this a texture and global (perhaps run in the start scene?).
		// Draw Mouse Cursor.
		game.renderer.DrawCircleFilled(game.input.GetMousePosition(), 5.0f, color::Red);
	}
};

class SetupScene : public Scene {
public:
	SetupScene() {}

	void Init() final {
		game.renderer.SetClearColor(color::Silver);
		game.window.SetSize(resolution);

		// TODO: For some reason, going straight to "GameScene" causes camera not to follow
		// player.
		/*std::size_t initial_scene{ Hash("game") };
		game.scene.Load<GameScene>(initial_scene);*/
		std::size_t initial_scene{ Hash("main_menu") };
		game.scene.Load<MainMenu>(initial_scene);
		game.scene.SetActive(initial_scene);
	}
};

int main() {
	game.Start<SetupScene>();
	return 0;
}
