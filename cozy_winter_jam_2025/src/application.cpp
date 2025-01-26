#include "protegon/protegon.h"

using namespace ptgn;

// #define MENU_SCENES
// #define START_SEQUENCE

constexpr V2_int window_size{ 1280, 720 };
constexpr V2_int tile_size{ 8, 8 };
constexpr float camera_zoom{ 4.0f };
constexpr int tooltip_text_size{ 28 };
constexpr Color shading_color{ color::White.SetAlpha(0.5f) };

constexpr std::size_t sound_frequency{ 2 };

constexpr CollisionCategory wall_category{ 1 };
constexpr CollisionCategory item_category{ 2 };
constexpr CollisionCategory tree_category{ 3 };
constexpr CollisionCategory player_category{ 4 };
constexpr CollisionCategory interaction_category{ 5 };

constexpr int wind_channel{ 0 };
constexpr int snow_volume{ 40 };
constexpr int wood_volume{ 40 };
constexpr int music_volume{ 60 };
constexpr int wind_outside_volume{ 128 };
constexpr int wind_inside_volume{ 80 };

const path json_path{ "resources/data/data.json" };
const path wind_sound_path{ "resources/audio/breeze.ogg" };
const path music_path{ "resources/audio/music.ogg" };
const path snow_sound_path{ "resources/audio/snow.ogg" };
const path wood_sound_path{ "resources/audio/wood.ogg" };
const path text_font_path{ "resources/font/BubbleGum_Regular.ttf" };

struct Tree {};

void GoToMainMenu();

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
	Texture letter_texture{ "resources/ui/letter.png" };
	Texture letter_text_texture{ "resources/ui/letter_text.png" };
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

	std::vector<Rect> house_area;

	V2_float player_size;

	bool PlayerInHouse() {
		const auto& sprite_size{ player_size };
		const auto& pos{ player.Get<Transform>().position };
		Rect player_rec{ pos, sprite_size, Origin::Center };
		for (const auto& r : house_area) {
			if (player_rec.Overlaps(r)) {
				return true;
			}
		}
		return false;
	}

	std::size_t anim_repeats{ 0 };

	ecs::Entity CreatePlayer() {
		ecs::Entity entity = manager.CreateEntity();

		V2_float player_starting_position{ -400.0f, 0.0f };
		entity.Add<Transform>(player_starting_position);
		auto& rb = entity.Add<RigidBody>();
		entity.Add<RenderLayer>(2);

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
		player_size				= { 16, 17 };
		V2_float animation_size = player_size;
		milliseconds animation_duration{ 1000 };

		auto& anim_map = entity.Add<AnimationMap>(
			"down", player_animation, animation_count.x, animation_size, animation_duration
		);
		auto& a0 = anim_map.GetActive();
		auto& a1 = anim_map.Load(
			"right", player_animation, animation_count.x, animation_size, animation_duration,
			V2_float{ 0, animation_size.y }
		);
		auto& a2 = anim_map.Load(
			"up", player_animation, animation_count.x, animation_size, animation_duration,
			V2_float{ 0, 2.0f * animation_size.y }
		);

		auto on_repeat = [&]() {
			++anim_repeats;
			bool repeat{ anim_repeats % sound_frequency == 0 };
			if (!repeat) {
				return;
			}
			if (!PlayerInHouse()) {
				game.sound.Get("snow").SetVolume(snow_volume);
				game.sound.Get("snow").Play();
			} else {
				game.sound.Get("wood").SetVolume(wood_volume);
				game.sound.Get("wood").Play();
			}
		};

		a0.on_repeat = on_repeat;
		a1.on_repeat = on_repeat;
		a2.on_repeat = on_repeat;

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
		house_area.clear();
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

	void SequenceKeyDelay() {
		auto tween = game.tween.Load().During(milliseconds{ 30 }).Repeat(-1);
		tween
			.OnUpdate([=]() mutable {
				if (game.input.KeyDown(Key::E)) {
					switch (current_interaction_type) {
						case InteractionType::None: break;
						case InteractionType::Letter:
							player.Get<TopDownMovement>().keys_enabled = true;
							show_letter								   = false;
							break;
						default: break;
					}
					StartSequence(++sequence_index);
					tween.Stop();
				}
			})
			.Start();
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
		current_interaction_type = static_cast<InteractionType>(interaction_type);
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
		} else if (name == "keypress") {
			waypoint.FadeOut();
			SequenceKeyDelay();
		} else {
			PTGN_ASSERT(data.contains("items"));
			const auto& items{ data.at("items") };
			PTGN_ASSERT(items.contains(name), "Sequence item missing from json items: ", name);
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
		if (visibility != 0) {
			entity.Add<Sprite>(texture, V2_float{}, Origin::TopLeft);
		}
		auto& b = entity.Add<BoxColliderGroup>(entity, manager);
		if (!hitbox_size.IsZero()) {
			b.AddBox(
				"body", hitbox_offset, 0.0f, hitbox_size, Origin::TopLeft, true, item_category,
				{ player_category }, nullptr, nullptr, nullptr, nullptr, false, true
			);
		}
		return entity;
	}

	std::string tooltip_content;

	enum class InteractionType {
		None		 = -1,
		Letter		 = 0,
		Tree		 = 1,
		Fireplace	 = 2,
		RecordPlayer = 3,
		Dirt1		 = 4,
		Dirt2		 = 5,
		Pot1		 = 6,
		Pantry1		 = 13,
		Pot2		 = 8,
		Mushroom	 = 9,
		Pot3		 = 10,
		Bed1		 = 11,
		Bed2		 = 12,
		Pantry2		 = 7,
		Pot4		 = 14
	};

	InteractionType current_interaction_type{ InteractionType::None };
	bool show_letter = false;

	void GameEndSequence();

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
										.GetBox("interaction")
										.GetAbsoluteRect()
										.GetPosition(Origin::Center));
			},
			[&](Collision collision) {
				if (game.input.KeyDown(Key::E)) {
					collision.entity1.Get<BoxColliderGroup>().GetBox("interaction").enabled = false;
					tooltip.on_complete = [=]() mutable {
						switch (current_interaction_type) {
							case InteractionType::None: break;
							case InteractionType::Letter:
								player.Get<TopDownMovement>().keys_enabled = false;
								show_letter								   = true;
								break;
							case InteractionType::Tree:		 collision.entity1.Destroy(); break;
							case InteractionType::Fireplace: {
								V2_float fireplace_size{ 26, 40 };
								auto& anim{ collision.entity1.Add<Animation>(
									Texture{ "resources/tile/fireplace_anim.png" }, 3,
									fireplace_size, milliseconds{ 300 }, V2_float{}, V2_float{},
									Origin::TopLeft
								) };
								anim.Start();
								const Camera& c{ game.camera.GetPrimary() };
								Light firelight{ collision.entity1.Get<Transform>().position +
													 fireplace_size / 2.0f,
												 color::Orange };
								firelight.ambient_color_	 = color::Gold;
								firelight.ambient_intensity_ = 0.1f;
								firelight.radius_			 = 600.0f;
								firelight.compression_		 = 30.0f;
								firelight.SetIntensity(0.8f);
								game.light.Load("fireplace", firelight);
								break;
							}
							case InteractionType::RecordPlayer:
								game.music.Get("music").FadeIn(seconds{ 3 });
								break;
							case InteractionType::Dirt1: GetItem("dirt1").Destroy(); break;
							case InteractionType::Dirt2: GetItem("dirt2").Destroy(); break;
							case InteractionType::Pot1:
								GetItem("pot1").Add<Sprite>(
									Texture{ "resources/tile/pot_water.png" }, V2_float{},
									Origin::TopLeft
								);
								break;
							case InteractionType::Pantry1:
								GetItem("pantry1").Add<Sprite>(
									Texture{ "resources/tile/pantry_open.png" }, V2_float{},
									Origin::TopLeft
								);
								break;
							case InteractionType::Pantry2:
								GetItem("pantry2").Add<Sprite>(
									Texture{ "resources/tile/pantry.png" }, V2_float{},
									Origin::TopLeft
								);
								break;
							case InteractionType::Pot2:
								GetItem("pot2").Add<Sprite>(
									Texture{ "resources/tile/pot_soup.png" }, V2_float{},
									Origin::TopLeft

								);
								break;
							case InteractionType::Mushroom: collision.entity1.Destroy(); break;
							case InteractionType::Pot3:
								GetItem("pot3").Add<Sprite>(
									Texture{ "resources/tile/pot_soup.png" }, V2_float{},
									Origin::TopLeft

								);
								break;
							case InteractionType::Pot4:
								GetItem("pot4").Add<Sprite>(
									Texture{ "resources/tile/pot.png" }, V2_float{}, Origin::TopLeft

								);
								break;
							case InteractionType::Bed1:
								GetItem("bed1").Add<Sprite>(
									Texture{ "resources/tile/bed_made.png" }, V2_float{},
									Origin::TopLeft

								);
								break;
							case InteractionType::Bed2:
								GetItem("bed2").Add<Sprite>(
									Texture{ "resources/tile/bed_sleep.png" }, V2_float{},
									Origin::TopLeft

								);
								player.Get<TopDownMovement>().keys_enabled = false;
								player.Remove<AnimationMap>();
								GameEndSequence();
								break;
							default: break;
						}
						manager.Refresh();
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
		house_area.clear();
		PTGN_ASSERT(data.contains("house_hitboxes"));
		PTGN_ASSERT(data.contains("house_overlaps"));
		V2_float house_pos{ house_rect.GetPosition(Origin::TopLeft) };
		const auto& house_overlaps{ data.at("house_overlaps") };
		for (const auto& obj : house_overlaps) {
			PTGN_ASSERT(obj.contains("size"));
			PTGN_ASSERT(obj.contains("position"));
			Rect r{ house_pos + V2_float{ obj.at("position") }, V2_float{ obj.at("size") },
					Origin::TopLeft };
			house_area.push_back(r);
		}
		const auto& house_hitboxes{ data.at("house_hitboxes") };
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
			PTGN_ASSERT(item.contains("visibility"));
			int visibility{ item.at("visibility") };
			Texture texture{ item.at("sprite") };
			Rect rect{ house_pos + V2_float{ item.at("tile_position") } * tile_size,
					   texture.GetSize(), Origin::TopLeft };
			if (item.contains("interaction_offset") && item.contains("interaction_size") &&
				item.contains("waypoint_offset")) {
				CreateInteractableItem(
					name, texture, rect,
					(item.contains("hitbox_offset") ? V2_float{ item.at("hitbox_offset") }
													: V2_float{}),
					(item.contains("hitbox_size") ? V2_float{ item.at("hitbox_size") } : V2_float{}
					),
					V2_float{ item.at("interaction_offset") },
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

		/*seconds day_duration{ 10 };
		game.tween.Load()
			.During(day_duration)
			.Yoyo()
			.Repeat(-1)
			.OnUpdate([&](float f) {
				Color dusk{ color::DarkBlue.SetAlpha(f / 2.0f) };
				game.camera.GetPrimary().GetRect().Draw(dusk, -1, 10);
			})
			.Start();*/

		/*	Light ambient{ game.window.GetCenter(), color::Orange };
			ambient.ambient_color_	   = color::DarkBlue;
			ambient.ambient_intensity_ = 0.3f;
			ambient.radius_			   = 400.0f;
			ambient.compression_	   = 50.0f;
			ambient.SetIntensity(0.6f);
			game.light.Load("ambient_light", ambient);*/

#ifdef MENU_SCENES
		data = game.json.Get("data");
#else
		game.sound.Get("wind").SetVolume(wind_outside_volume);
		game.sound.Get("wind").Play(wind_channel, -1);
		data = game.json.Load("data", json_path);
#endif
		game.sound.Get("snow").SetVolume(snow_volume);
		game.sound.Get("wood").SetVolume(wood_volume);
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

		if (PlayerInHouse()) {
			game.sound.Get("wind").SetVolume(wind_inside_volume);
		} else {
			game.sound.Get("wind").SetVolume(wind_outside_volume);
		}

		Draw();
	}

	// Minimum pixels of separation between tree trunk centers.
	float tree_separation_dist{ 30.0f };

	void GenerateTree(const Rect& rect) {
		auto trees{ manager.EntitiesWith<Tree, Transform, BoxCollider>() };
		V2_float center{ rect.Center() };
		if (rect.Overlaps(house_perimeter) ||
			rect.Overlaps(Rect{ { -87, -49 }, { 300, 100 }, Origin::TopRight })) {
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

		V2_float padding{ 40, 40 };

		V2_int min{ (cam_rect.Min() - padding) / tile_size - V2_int{ 1 } };
		V2_int max{ (cam_rect.Max() + padding) / tile_size + V2_int{ 1 } };

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
		// game.light.Get("ambient_light").SetPosition(player_pos);

		for (const auto [e, anim] : manager.EntitiesWith<Animation>()) {
			anim.Draw(e);
		}

		for (const auto [e, anim_map] : manager.EntitiesWith<AnimationMap>()) {
			anim_map.Draw(e);
		}

		if (show_letter) {
			RenderTarget ui{ color::Transparent };
			ui.SetCamera({});
			game.renderer.SetRenderTarget(ui);
			letter_texture.Draw({ game.window.GetCenter(), {}, Origin::Center });
			letter_text_texture.Draw({ game.window.GetCenter(), {}, Origin::Center });
			game.renderer.SetRenderTarget({});
			ui.Draw();
		}
		game.camera.GetPrimary().GetRect().Draw(color::DarkBlue.SetAlpha(0.5f), -1, 10);
	}
};

class TextScene : public Scene {
public:
	std::string_view content;
	Color text_color;
	Color bg_color{ color::Black };

	std::string_view transition_to_scene;

	TextScene(
		std::string_view transition_to_scene, std::string_view continue_text_content,
		std::string_view content, const Color& text_color
	) :
		content{ content },
		text_color{ text_color },
		transition_to_scene{ transition_to_scene },
		continue_text{ continue_text_content, color::Red.SetAlpha(0.0f), "text_font" } {}

	Text continue_text;
	Text text;
	seconds reading_duration{ 4 };

	void Enter() override {
		game.camera.SetPrimary({});
		text = Text{ content, text_color, "text_font" };
		text.SetWrapAfter(400);
		text.SetSize(30);

		game.tween.Load()
			.During(reading_duration)
			.OnComplete([&]() {
				game.event.key.Subscribe(
					KeyEvent::Down, this, std::function([&](const KeyDownEvent&) {
						game.event.key.Unsubscribe(this);
						if (transition_to_scene == "game") {
							game.scene.Enter<GameScene>(
								"game", SceneTransition{ TransitionType::FadeThroughColor,
														 milliseconds{ 1000 } }
											.SetFadeColorDuration(milliseconds{ 100 })
							);
						} else if (transition_to_scene == "main_menu") {
							GoToMainMenu();
						}
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
		game.music.Load("music", music_path);
		game.music.SetVolume(music_volume);
		game.sound.Load("wind", wind_sound_path);
		game.sound.Load("snow", snow_sound_path);
	}

	void Enter() override {
		game.sound.Get("wind").Play(wind_channel, -1);
		play.Set<ButtonProperty::OnActivate>([]() {
			game.scene.Enter<TextScene>(
				"text_scene",
				SceneTransition{ TransitionType::FadeThroughColor,
								 milliseconds{ milliseconds{ 1000 } } }
					.SetFadeColorDuration(milliseconds{ 500 }),
				"game", "Press any key to continue...",
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

void GameScene::GameEndSequence() {
	game.scene.Enter<TextScene>(
		"text_scene",
		SceneTransition{ TransitionType::FadeThroughColor, milliseconds{ milliseconds{ 4000 } } }
			.SetFadeColorDuration(milliseconds{ 1000 }),
		"main_menu", "Press any key to go to main menu...",
		"In your cozy cabin, filled with fresh mountain air, you enter a soft slumber...",
		color::Silver
	);
}

void GoToMainMenu() {
	game.Start<MainMenu>(
		"main_menu", SceneTransition{ TransitionType::FadeThroughColor, milliseconds{ 1000 } }
						 .SetFadeColorDuration(milliseconds{ 200 })
	);
}

int main([[maybe_unused]] int c, [[maybe_unused]] char** v) {
	game.Init("Cozy Winter Jam", window_size, color::Transparent);
#ifdef MENU_SCENES
	game.Start<MainMenu>(
		"main_menu", SceneTransition{ TransitionType::FadeThroughColor, milliseconds{ 500 } }
	);
#else
	game.font.Load("text_font", text_font_path);
	game.sound.Load("wind", wind_sound_path);
	game.music.Load("music", music_path);
	game.music.SetVolume(music_volume);
	game.sound.Load("snow", snow_sound_path);
	game.sound.Load("wood", wood_sound_path);
	game.Start<GameScene>(
		"game", SceneTransition{ TransitionType::FadeThroughColor, milliseconds{ 1000 } }
	);
#endif
	return 0;
}