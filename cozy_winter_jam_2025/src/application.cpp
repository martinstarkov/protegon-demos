#include "protegon/protegon.h"

using namespace ptgn;

// #define MENU_SCENES
// #define START_SEQUENCE

constexpr V2_int window_size{ 1280, 720 };
constexpr V2_int tile_size{ 8, 8 };
constexpr float camera_zoom{ 4.0f };
constexpr int tooltip_text_size{ 20 };
constexpr Color shading_color{ color::White.SetAlpha(0.5f) };

constexpr CollisionCategory wall_category{ 1 };
constexpr CollisionCategory item_category{ 2 };
constexpr CollisionCategory tree_category{ 3 };
constexpr CollisionCategory player_category{ 4 };
constexpr CollisionCategory interaction_category{ 5 };
const path json_path{ "resources/data/data_sample.json" };
const path wind_music_path{ "resources/audio/breeze.ogg" };
const path text_font_path{ "resources/font/BubbleGum-Regular.ttf" };

struct Tree {};

struct ItemName {
	std::string name;
};

Tween CreateFadingTween(
	const std::function<void(float)>& fade_function,
	const std::function<void(float)>& update_function,
	const std::function<void()>& start_function	   = nullptr,
	const std::function<void()>& complete_function = nullptr
) {
	Tween tween{ game.tween.Load()
					 .During(milliseconds{ 150 })
					 .OnStart(start_function)
					 .OnUpdate(fade_function)
					 .During(seconds{ 1 })
					 .Yoyo()
					 .Repeat(-1)
					 .Ease(TweenEase::InOutSine)
					 .OnUpdate(update_function)
					 .During(milliseconds{ 150 })
					 .Reverse()
					 .OnUpdate(fade_function)
					 .OnComplete(complete_function) };
	return tween;
}

struct Tooltip {
	Tooltip() = default;

	Tooltip(const Text& t, const V2_float& static_offset) : text{ t } {
		float up_distance{ 15.0f / camera_zoom };

		auto draw_text = [this,
						  static_offset](float alpha /* [0.0f, 1.0f] */, float v_offset) mutable {
			PTGN_ASSERT(alpha >= 0.0f && alpha <= 1.0f);
			vertical_offset = v_offset;
			auto old_cam{ game.camera.GetPrimary() };
			Rect r{ old_cam.TransformToScreen(
						GetPosition() + static_offset + V2_float{ 0.0f, vertical_offset }
					),
					{},
					Origin::Center };
			text.SetColor(text.GetColor().SetAlpha(alpha));
			game.renderer.Flush();
			game.camera.SetPrimary({});
			text.Draw(r);
			game.renderer.Flush();
			game.camera.SetPrimary(old_cam);
		};

		tween = CreateFadingTween(
			[=](float f) mutable { std::invoke(draw_text, f / 2.0f, vertical_offset); },
			[=](float f) mutable {
				float vertical_distance{ up_distance };
				// How much distance above the og_position the tween text moves up.
				std::invoke(draw_text, 1.0f, -f * vertical_distance);
			},
			[&]() { vertical_offset = 0.0f; },
			[&]() {
				vertical_offset = 0.0f;
				Invoke(on_complete);
			}
		);
	}

	// @return True if the tooltip is currently visible.
	bool IsShowing() const {
		return tween.IsRunning();
	}

	void FadeIn() {
		tween.StartIfNotRunning();
	}

	void FadeOut() {
		tween.IncrementTweenPoint();
	}

	void SetPosition(const V2_float& position) {
		anchor_position = position;
	}

	V2_float GetPosition() const {
		return anchor_position;
	}

	Text text;
	std::function<void()> on_complete;

private:
	V2_float anchor_position;
	float vertical_offset{ 0.0f };
	// Static offset of tooltip compared to anchor position.
	Tween tween;
};

struct Waypoint {
	Waypoint() = default;

	Waypoint(const Texture& texture) {
		auto draw_waypoint = [&](float alpha, float vertical_offset) {
			PTGN_ASSERT(texture.IsValid());
			PTGN_ASSERT(alpha >= 0.0f && alpha <= 1.0f);
			Rect r{ anchor_position + offset + V2_float{ 0.0f, vertical_offset },
					{},
					Origin::Center };
			texture.Draw(r, { color::White.SetAlpha(alpha) });
		};

		tween = CreateFadingTween(
			[=](float f) mutable { std::invoke(draw_waypoint, f / 2.0f, -f * up_distance); },
			[=](float f) mutable { std::invoke(draw_waypoint, 1.0f, -f * up_distance); }
		);
	}

	// @return True if the waypoint is currently visible.
	bool IsShowing() const {
		return tween.IsRunning();
	}

	void FadeIn() {
		tween.StartIfNotRunning();
	}

	void FadeOut() {
		tween.IncrementTweenPoint();
	}

	V2_float GetAnchorPosition() const {
		return anchor_position;
	}

	void SetAnchorPosition(const V2_float& position) {
		anchor_position = position;
	}

	// Offset of the texture from the anchor position.
	void SetStaticOffset(const V2_float& static_offset) {
		offset = static_offset;
	}

private:
	V2_float anchor_position;

	// How much distance above the og_position the tween text moves up.
	float up_distance{ 15.0f / camera_zoom };
	// Static offset of tooltip compared to anchor position.
	V2_float offset;
	Tween tween;
};

void CreateFloatingText(
	Text text, seconds duration, seconds yoyo_duration, float vertical_distance,
	const std::function<V2_float()>& get_position, const std::function<void()>& on_complete
) {
	auto vertical_offset = std::make_shared<float>(0.0f);

	auto draw_text = [vertical_offset, text,
					  get_position](float alpha /* [0.0f, 1.0f] */, float v_offset) mutable {
		PTGN_ASSERT(alpha >= 0.0f && alpha <= 1.0f);
		*vertical_offset = v_offset;
		auto old_cam{ game.camera.GetPrimary() };
		Rect r{ old_cam.TransformToScreen(
					std::invoke(get_position) + V2_float{ 0.0f, *vertical_offset }
				),
				{},
				Origin::Center };
		text.SetColor(text.GetColor().SetAlpha(alpha));
		game.renderer.Flush();
		game.camera.SetPrimary({});
		text.Draw(r);
		game.renderer.Flush();
		game.camera.SetPrimary(old_cam);
	};

	auto fade_function = [=](float f) mutable {
		std::invoke(draw_text, f / 2.0f, *vertical_offset);
	};

	auto update_function = [=](float f) mutable {
		float v_distance{ vertical_distance };
		// How much distance above the og_position the tween text moves up.
		std::invoke(draw_text, 1.0f, -f * v_distance);
	};

	milliseconds fade_duration{ 150 };

	Tween text_tween{ game.tween.Load()
						  .During(fade_duration)
						  .OnUpdate(fade_function)
						  .During(yoyo_duration)
						  .Yoyo()
						  .Repeat(-1)
						  .Ease(TweenEase::InOutSine)
						  .OnUpdate(update_function)
						  .During(milliseconds{ 150 })
						  .Reverse()
						  .OnUpdate(fade_function)
						  .OnComplete([=]() { Invoke(on_complete); }) };
	Tween tween{ game.tween.Load().During(duration).OnStart([=]() mutable { text_tween.Start(); }
	).OnComplete([=]() mutable { text_tween.IncrementTweenPoint(); }) };
	tween.Start();
}

class GameScene : public Scene {
	FractalNoise fractal_noise;

	Texture player_animation{ "resources/entity/player.png" };
	Texture snow_texture{ "resources/tile/snow.png" };
	Texture tree_texture{ "resources/tile/tree.png" };
	Texture house_texture{ "resources/tile/house.png" };
	Texture waypoint_texture{ "resources/ui/waypoint.png" };
	Texture arrow_texture{ "resources/ui/arrow.png" };

	json data;

	Waypoint waypoint{ waypoint_texture };

	std::size_t sequence_index{ 0 };

	ecs::Entity CreateWall(const Rect& r) {
		ecs::Entity entity = manager.CreateEntity();
		entity.Add<Transform>(r.position, r.rotation);
		auto& box = entity.Add<BoxCollider>(entity, r.size, r.origin);
		box.SetCollisionCategory(wall_category);
		entity.Add<DrawColor>(color::Purple);
		return entity;
	}

	ecs::Entity CreatePlayer() {
		ecs::Entity entity = manager.CreateEntity();

		V2_float player_starting_position{ -400.0f, 0.0f };
		entity.Add<Transform>(player_starting_position);
		auto& rb = entity.Add<RigidBody>();

		V2_float hitbox_size{ 10, 6 };
		V2_float hitbox_offset{ 0, 8 };

		auto& b = entity.Add<BoxColliderGroup>(entity, manager);
		b.AddBox(
			"body", hitbox_offset, 0.0f, hitbox_size, Origin::CenterBottom, true, player_category,
			{ wall_category, item_category, tree_category }, nullptr, nullptr, nullptr, nullptr,
			false, true
		);
		b.AddBox(
			"interaction", {}, 0.0f, { 28, 28 }, Origin::Center, false, interaction_category,
			{ interaction_category, tree_category }, [&](Collision collision) {},
			[&](Collision collision) {
				/*collision.entity1.Add<DrawColor>()	= color::Red;
				collision.entity2.Add<DrawColor>()	= color::Red;
				collision.entity1.Add<SpriteTint>() = color::Red;
				collision.entity2.Add<SpriteTint>() = color::Red;*/
			},
			[&](Collision collision) {
				/*collision.entity1.Add<DrawColor>()	= color::Green;
				collision.entity2.Add<DrawColor>()	= color::Green;
				collision.entity1.Add<SpriteTint>() = color::Green;
				collision.entity2.Add<SpriteTint>() = color::Green;*/
			},
			nullptr, true, false
		);

		// entity.Add<Sprite>(player_animation);

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
		V2_float animation_size{ 16, 17 };
		milliseconds animation_duration{ 500 };

		auto& anim_map = entity.Add<AnimationMap>(
			"down", player_animation, animation_count.x, animation_size, animation_duration
		);
		anim_map.Load(
			"right", player_animation, animation_count.x, animation_size, animation_duration,
			V2_float{ 0, animation_size.y }
		);
		anim_map.Load(
			"up", player_animation, animation_count.x, animation_size, animation_duration,
			V2_float{ 0, 2.0f * animation_size.y }
		);
		movement.on_move_start = [=]() {
			player.Get<AnimationMap>().GetActive().StartIfNotRunning();
		};
		movement.on_direction_change = [=](MoveDirection) {
			auto& a{ player.Get<AnimationMap>() };
			auto dir{ player.Get<TopDownMovement>().GetDirection() };
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
				prev_active.Reset();
			}
			a.GetActive().StartIfNotRunning();
		};
		movement.on_move_stop = [=]() {
			player.Get<AnimationMap>().GetActive().Reset();
		};
		return entity;
	}

	ecs::Entity player;

	void Exit() override {
		game.tween.Reset();
		manager.Clear();
	}

	V2_float camera_intro_offset{ -250, 0 };
	float camera_intro_start_zoom{ camera_zoom };

	Tooltip wasd_tooltip{ Text{ "'WASD' to move", color::Black, "text_font" }
							  .SetSize(tooltip_text_size)
							  .SetShadingColor(shading_color),
						  { 0, -15 } };

	Tooltip tooltip{ Text{ "NULL", color::Black, "text_font" }
						 .SetSize(tooltip_text_size)
						 .SetShadingColor(shading_color),
					 { 0, -5 } };

	void EnablePlayerInteraction(bool enable = true) {
		player.Get<BoxColliderGroup>().GetBox("interaction").enabled = enable;
	}

	void PlayIntro() {
		player.Get<TopDownMovement>().keys_enabled = false;

		game.tween.Load()
			.During(seconds{ 6 })
			.Reverse()
			.OnUpdate([&](float f) {
				auto& cam{ game.camera.GetPrimary() };
				// Tween is reversed so zoom is lerped backward.
				cam.SetZoom(Lerp(camera_zoom, camera_intro_start_zoom, f));
				cam.SetPosition(player.Get<Transform>().position + camera_intro_offset * f);
				player.Get<TopDownMovement>().Move(MoveDirection::Right);
			})
			.OnComplete([&]() {
				player.Get<TopDownMovement>().keys_enabled = true;
				EnablePlayerInteraction();
				ShowIntroTooltip();
			})
			.Start();
	}

	void ShowIntroTooltip() {
		auto func = [&]() {
			wasd_tooltip.FadeIn();
		};
		Tween tween = game.tween.Load("wasd_tooltip").During(seconds{ 8 }).OnStart(func);
		tween
			.OnUpdate([&, tween](float f) mutable {
				V2_float pos{
					player.Get<BoxColliderGroup>().GetBox("body").GetAbsoluteRect().GetPosition(
						Origin::CenterTop
					)
				};
				wasd_tooltip.SetPosition(pos);
				const auto& tdm = player.Get<TopDownMovement>();
				if (!tdm.IsMoving(MoveDirection::None) && f < 0.8f) {
					tween.Seek(0.8f);
				}
			})
			.OnComplete([&]() {
				wasd_tooltip.FadeOut();
				StartSequence(sequence_index);
				game.tween.Unload("wasd_tooltip");
			})
			.Start();
	}

	Rect house_rect;
	Rect house_perimeter;

	void SequenceSpawnDelay(seconds duration) {
		game.tween.Load().During(duration).OnComplete([&]() { StartSequence(++sequence_index); }
		).Start();
	}

	void SequenceSpawnPlayerText(std::string_view content, seconds duration, const Color& color) {
		CreateFloatingText(
			Text{ content, color, "text_font" }
				.SetSize(tooltip_text_size)
				.SetShadingColor(shading_color),
			duration, seconds{ 1 }, 10.0f / camera_zoom,
			[=]() {
				return player.Get<Transform>().position + V2_float{ 0, -13 };
			},
			[&]() { StartSequence(++sequence_index); }
		);
	}

	ecs::Entity GetItem(const std::string& name) {
		for (auto [e, n] : manager.EntitiesWith<ItemName>()) {
			if (n.name == name) {
				return e;
			}
		}
		PTGN_ERROR("Failed to find entity item with name ", name);
	}

	void SequenceAction(
		const std::string& name, const json& item, int interaction_type,
		std::string_view tooltip_text
	) {
		PTGN_ASSERT(item.contains("tile_position"));
		PTGN_ASSERT(item.contains("waypoint_offset"));
		V2_float tile_position{ item.at("tile_position") };
		V2_float waypoint_offset{ item.at("waypoint_offset") };
		V2_float position{ house_rect.GetPosition(Origin::TopLeft) + tile_position * tile_size +
						   waypoint_offset };
		auto entity													 = GetItem(name);
		entity.Get<BoxColliderGroup>().GetBox("interaction").enabled = true;
		waypoint.SetAnchorPosition(position);
		waypoint.FadeIn();
		tooltip_content = tooltip_text;
	}

	void StartSequence(std::size_t index) {
		PTGN_ASSERT(data.contains("sequence"));
		const auto& sequence{ data.at("sequence") };
		if (index >= sequence.size()) {
			waypoint.FadeOut();
			// PTGN_LOG("Reached end of sequence!");
			return;
		}

		const auto& e{ sequence.at(index) };
		PTGN_ASSERT(e.contains("name"));
		const auto& name{ e.at("name") };
		if (name == "timer") {
			waypoint.FadeOut();
			PTGN_ASSERT(e.contains("seconds_duration"));
			seconds time{ std::chrono::duration_cast<seconds>(duration<float>{
				e.at("seconds_duration") }) };
			if (e.contains("text")) {
				SequenceSpawnPlayerText(e.at("text"), time, color::Black);
			} else {
				SequenceSpawnDelay(time);
			}
		} else {
			PTGN_ASSERT(data.contains("items"));
			const auto& items{ data.at("items") };
			PTGN_ASSERT(items.contains(name), "Sequence item missing from json items");
			PTGN_ASSERT(e.contains("interaction_type"));
			PTGN_ASSERT(e.contains("tooltip_text"));
			const auto& item{ items.at(name) };
			SequenceAction(name, item, e.at("interaction_type"), e.at("tooltip_text"));
		}
	}

	V2_float GetWaypointDir() const {
		const auto& player_pos{ player.Get<Transform>().position };
		const auto& waypoint_pos{ waypoint.GetAnchorPosition() };
		V2_float dir{ waypoint_pos - player_pos };
		return dir;
	}

	bool WithinWaypointRadius(const V2_float& dir) const {
		return dir.MagnitudeSquared() <
			   waypoint_arrow_disappear_radius * waypoint_arrow_disappear_radius;
	}

	ecs::Entity CreateItem(
		const std::string& name, const Texture& texture, const Rect& rect,
		const V2_float& hitbox_offset, const V2_float& hitbox_size, int visibility
	) {
		auto entity = manager.CreateEntity();
		entity.Add<Transform>(rect.position);
		entity.Add<ItemName>(name);
		entity.Add<DrawColor>(color::Red);
		entity.Add<DrawLineWidth>(3.0f);
		entity.Add<RenderLayer>(1);
		entity.Add<Sprite>(texture, V2_float{}, Origin::TopLeft);
		auto& b = entity.Add<BoxColliderGroup>(entity, manager);
		b.AddBox(
			"body", hitbox_offset, 0.0f, hitbox_size, Origin::TopLeft, true, item_category,
			{ player_category }, nullptr, nullptr, nullptr, nullptr, false, true
		);
		return entity;
	}

	std::string tooltip_content;

	ecs::Entity CreateInteractableItem(
		const std::string& name, const Texture& texture, const Rect& rect,
		const V2_float& hitbox_offset, const V2_float& hitbox_size,
		const V2_float& interaction_offset, const V2_float& interaction_size, int visibility
	) {
		ecs::Entity entity{
			CreateItem(name, texture, rect, hitbox_offset, hitbox_size, visibility)
		};
		auto& b = entity.Get<BoxColliderGroup>();
		b.AddBox(
			"interaction", interaction_offset, 0.0f, interaction_size, Origin::TopLeft, false,
			interaction_category, { interaction_category },
			[&](Collision collision) {
				tooltip.text.SetContent(tooltip_content);
				tooltip.FadeIn();
				tooltip.SetPosition(collision.entity1.Get<BoxColliderGroup>()
										.GetBox("body")
										.GetAbsoluteRect()
										.GetPosition(Origin::CenterTop));
			},
			[&](Collision collision) {
				if (game.input.KeyDown(Key::E)) {
					collision.entity1.Get<BoxColliderGroup>().GetBox("interaction").enabled = false;
					tooltip.on_complete = [&]() {
						StartSequence(++sequence_index);
						tooltip.on_complete = nullptr;
					};
					tooltip.FadeOut();
				}
			},
			[&](Collision collision) { tooltip.FadeOut(); }, nullptr, true, false
		);
		return entity;
	}

	void GenerateHouse() {
		PTGN_ASSERT(data.contains("house_hitboxes"));
		const auto& house_hitboxes{ data.at("house_hitboxes") };
		V2_float house_pos{ house_rect.GetPosition(Origin::TopLeft) };
		for (const auto& obj : house_hitboxes) {
			PTGN_ASSERT(obj.contains("size"));
			PTGN_ASSERT(obj.contains("position"));
			Rect r{ house_pos + V2_float{ obj.at("position") }, V2_float{ obj.at("size") },
					Origin::TopLeft };
			CreateWall(r);
		}
		PTGN_ASSERT(data.contains("items"));
		const auto& items{ data.at("items") };
		for (const auto& [name, item] : items.items()) {
			PTGN_ASSERT(item.contains("sprite"));
			PTGN_ASSERT(item.contains("tile_position"));
			PTGN_ASSERT(item.contains("hitbox_size"));
			PTGN_ASSERT(item.contains("hitbox_offset"));
			PTGN_ASSERT(item.contains("visibility"));
			int visibility{ item.at("visibility") };
			Texture texture{ item.at("sprite") };
			Rect rect{ house_pos + V2_float{ item.at("tile_position") } * tile_size,
					   texture.GetSize(), Origin::TopLeft };
			if (item.contains("interaction_offset") && item.contains("interaction_size") &&
				item.contains("waypoint_offset")) {
				CreateInteractableItem(
					name, texture, rect, V2_float{ item.at("hitbox_offset") },
					V2_float{ item.at("hitbox_size") }, V2_float{ item.at("interaction_offset") },
					V2_float{ item.at("interaction_size") }, visibility
				);
			} else {
				CreateItem(
					name, texture, rect, V2_float{ item.at("hitbox_offset") },
					V2_float{ item.at("hitbox_size") }, visibility
				);
			}
		}
		manager.Refresh();
	}

	void Enter() override {
		game.renderer.SetClearColor(color::White);

		PTGN_ASSERT(manager.Size() == 0);

		V2_float ws{ window_size };

		fractal_noise.SetOctaves(2);
		fractal_noise.SetFrequency(0.055f);
		fractal_noise.SetLacunarity(5);
		fractal_noise.SetPersistence(3);

		house_rect			  = Rect{ { 0, 0 }, house_texture.GetSize(), Origin::Center };
		house_perimeter		  = house_rect;
		house_perimeter.size *= 1.5f;

		player = CreatePlayer();
		manager.Refresh();

		game.camera.GetPrimary().SetZoom(camera_zoom);

		auto draw_waypoint_arrow = [&](float alpha, float waypoint_scale) {
			const auto& player_pos{ player.Get<Transform>().position };

			V2_float dir{ GetWaypointDir() };

			const float arrow_pixels_from_player{ 18.0f };

			V2_float arrow_pos{ player_pos + dir.Normalized() * arrow_pixels_from_player };

			float arrow_rotation{ dir.Angle() };

			V2_float arrow_size{ arrow_texture.GetSize() * waypoint_scale };

			arrow_texture.Draw(
				Rect{ arrow_pos, arrow_size, Origin::Center, arrow_rotation },
				{ waypoint_arrow_color.SetAlpha(alpha) }
			);
		};

		waypoint_arrow_tween = CreateFadingTween(
			[=](float f) { std::invoke(draw_waypoint_arrow, f / 2.0f, 1.0f); },
			[=](float f) mutable {
				std::invoke(
					draw_waypoint_arrow, 0.5f + f / 2.0f,
					Lerp(waypoint_arrow_start_scale, waypoint_arrow_end_scale, f)
				);
			}
		);

		Light ambient{ game.window.GetCenter(), color::Orange };
		ambient.ambient_color_	   = color::DarkBlue;
		ambient.ambient_intensity_ = 0.3f;
		ambient.radius_			   = 400.0f;
		ambient.compression_	   = 50.0f;
		ambient.SetIntensity(0.6f);
		game.light.Load("ambient_light", ambient);

#ifdef MENU_SCENES
		data = game.json.Get("data");
#else
		game.music.Get("wind").Play();
		data = game.json.Load("data", json_path);
#endif
		GenerateHouse();

#ifdef START_SEQUENCE
		PlayIntro();
#else
		EnablePlayerInteraction();
		player.Get<Transform>().position = { -150.0f, 0 };
		ShowIntroTooltip();
#endif
		// StartSequence(sequence_index);
	}

	void Update() override {
		auto& player_pos = player.Get<Transform>().position;

		if (player.Get<TopDownMovement>().keys_enabled) {
			game.camera.GetPrimary().SetPosition(player_pos);
		}
		Draw();
	}

	// Minimum pixels of separation between tree trunk centers.
	float tree_separation_dist{ 100.0f };

	void GenerateTree(const Rect& rect) {
		auto trees{ manager.EntitiesWith<Tree, Transform, BoxCollider>() };
		V2_float center{ rect.Center() };
		if (rect.Overlaps(house_perimeter)) {
			return;
		}
		float tree_dist2{ tree_separation_dist * tree_separation_dist };
		for (auto [e, t, transform, b] : trees) {
			float dist2{ (center - transform.position).MagnitudeSquared() };
			if (dist2 < tree_dist2) {
				// Cannot generate tree, too close to another one.
				return;
			}
		}
		auto tree = manager.CreateEntity();
		tree.Add<Transform>(center);
		tree.Add<Tree>();
		V2_float tree_hitbox_size{ 2 * tile_size.x, 4 * tile_size.y };
		auto& box = tree.Add<BoxCollider>(tree, tree_hitbox_size, Origin::Center);
		box.SetCollisionCategory(tree_category);
		tree.Add<DrawColor>(color::Red);
		tree.Add<DrawLineWidth>(3.0f);
		tree.Add<Sprite>(tree_texture);
		manager.Refresh();
	}

	void GenerateTerrain() {
		const auto& cam{ game.camera.GetPrimary() };

		auto cam_rect{ cam.GetRect() };

		V2_int min{ cam_rect.Min() / tile_size - V2_int{ 1 } };
		V2_int max{ cam_rect.Max() / tile_size + V2_int{ 1 } };

		for (int i{ min.x }; i < max.x; i++) {
			for (int j{ min.y }; j < max.y; j++) {
				V2_int p{ i, j };
				float noise_value{ fractal_noise.Get((float)i, (float)j) };

				// Color color{ color::Black };
				Rect r{ p * tile_size, tile_size, Origin::TopLeft };
				int divisions{ 3 };

				float opacity_range{ 1.0f / static_cast<float>(divisions) };

				auto range{ static_cast<int>(noise_value / opacity_range) };

				// color.a = static_cast<std::uint8_t>(255.0f * static_cast<float>(range) *
				// opacity_range);

				if (range == 1) {
					GenerateTree(r);
				}
				snow_texture.Draw(r);
			}
		}

		game.renderer.Flush();

		manager.Refresh();
	}

	Tween waypoint_arrow_tween;
	Color waypoint_arrow_color{ color::Gold };
	float waypoint_arrow_disappear_radius{ 35.0f };
	float waypoint_arrow_start_scale{ 1.2f };
	float waypoint_arrow_end_scale{ 0.8f };

	float sky_opacity{ 128.0f };

	void Draw() {
		auto& player_pos{ player.Get<Transform>().position };

		GenerateTerrain();

		for (auto [e, t, s] : manager.EntitiesWith<Transform, Sprite>()) {
			s.Draw(e);
		}

		house_texture.Draw(house_rect);

		if (waypoint.IsShowing() && !WithinWaypointRadius(GetWaypointDir())) {
			waypoint_arrow_tween.StartIfNotRunning();
		} else {
			waypoint_arrow_tween.IncrementTweenPoint();
		}

		game.renderer.Flush();

		// Debug: Draw hitboxes.
		/*for (auto [e, b] : manager.EntitiesWith<BoxCollider>()) {
			DrawRect(e, b.GetAbsoluteRect());
		}*/

		Color dusk{ color::DarkBlue };

		if (game.input.KeyPressed(Key::UP)) {
			sky_opacity += 10.0f * game.dt();
		}
		if (game.input.KeyPressed(Key::DOWN)) {
			sky_opacity -= 10.0f * game.dt();
		}
		sky_opacity = std::clamp(sky_opacity, 0.0f, 255.0f);

		dusk.a = static_cast<std::uint8_t>(sky_opacity);
		// game.camera.GetPrimary().GetRect().Draw(dusk);

		// game.light.Get("ambient_light").SetPosition(player_pos);
		// game.light.Draw();

		for (const auto [e, anim_map] : manager.EntitiesWith<AnimationMap>()) {
			anim_map.Draw(e);
		}
	}
};

class TextScene : public Scene {
public:
	std::string_view content;
	Color text_color;
	Color bg_color{ color::Black };

	TextScene(std::string_view content, const Color& text_color) :
		content{ content }, text_color{ text_color } {}

	Text continue_text{ "Press any key to continue", color::Red.SetAlpha(0.0f), "text_font" };
	Text text;
	seconds reading_duration{ 1 };

	void Enter() override {
		text = Text{ content, text_color, "text_font" };
		text.SetWrapAfter(400);
		text.SetSize(30);

		game.tween.Load()
			.During(reading_duration)
			.OnComplete([&]() {
				game.event.key.Subscribe(
					KeyEvent::Down, this, std::function([&](const KeyDownEvent&) {
						game.event.key.Unsubscribe(this);
						game.scene.Enter<GameScene>(
							"game", SceneTransition{ TransitionType::FadeThroughColor,
													 milliseconds{ 1000 } }
										.SetFadeColorDuration(milliseconds{ 100 })
						);
					})
				);
				game.tween.Load()
					.During(seconds{ 1 })
					.OnUpdate([&](float f) {
						continue_text.SetColor(continue_text.GetColor().SetAlpha(f));
					})
					.Start();
			})
			.Start();
	}

	void Update() override {
		Rect::Fullscreen().Draw(bg_color);
		Rect text_rect{ game.window.GetCenter(), text.GetSize(), Origin::Center };
		text.Draw(text_rect);
		continue_text.Draw(
			{ { text_rect.Center().x, text_rect.Max().y + 30 }, {}, Origin::CenterTop }
		);
	}
};

class MainMenu : public Scene {
public:
	Button play;
	Texture background{ "resources/ui/background.png" };

	MainMenu() {
		game.json.Load("data", json_path);
		game.font.Load("text_font", text_font_path);
		game.music.Load("wind", wind_music_path);
	}

	void Enter() override {
		game.music.Get("wind").Play();
		play.Set<ButtonProperty::OnActivate>([]() {
			game.scene.Enter<TextScene>(
				"text_scene",
				SceneTransition{ TransitionType::FadeThroughColor,
								 milliseconds{ milliseconds{ 1000 } } }
					.SetFadeColorDuration(milliseconds{ 500 }),
				"In your busy life full of work and stress you make time once a year to get away "
				"from it all. Your cabin awaits you in the quiet wilderness of Alaska...",
				color::White
			);
		});
		play.Set<ButtonProperty::BackgroundColor>(color::DarkGray);
		play.Set<ButtonProperty::BackgroundColor>(color::Gray, ButtonState::Hover);
		Text text{ "Play", color::Black };
		play.Set<ButtonProperty::Text>(text);
		play.Set<ButtonProperty::TextSize>(V2_float{ 0.0f, 0.0f });
		play.SetRect({ game.window.GetCenter(), { 200, 100 }, Origin::CenterTop });
	}

	void Update() override {
		background.Draw();
		play.Draw();
	}
};

int main([[maybe_unused]] int c, [[maybe_unused]] char** v) {
	game.Init("Cozy Winter Jam", window_size, color::Transparent);
#ifdef MENU_SCENES
	game.Start<MainMenu>(
		"main_menu", SceneTransition{ TransitionType::FadeThroughColor, milliseconds{ 500 } }
	);
#else
	game.font.Load("text_font", text_font_path);
	game.music.Load("wind", wind_music_path);
	game.Start<GameScene>(
		"game", SceneTransition{ TransitionType::FadeThroughColor, milliseconds{ 1000 } }
	);
#endif
	return 0;
}