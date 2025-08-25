#include "protegon/protegon.h"

using namespace ptgn;

constexpr V2_int resolution{ 320, 180 };

void SetupWindow() {
	game.renderer.SetLogicalResolutionMode(LogicalResolutionMode::IntegerScale);
	game.window.SetSetting(WindowSetting::Maximized);
}

struct FollowMouseScript : public Script<FollowMouseScript> {
	void OnUpdate() override {
		SetPosition(entity, entity.GetScene().input.GetMousePosition());
	}
};

void CreateStraightBullet(Scene& scene, const V2_float& start_pos, const V2_float& dir_norm) {
	auto bullet = CreateSprite(scene, "bullet1", start_pos);
	auto& rb	= bullet.Add<RigidBody>();
	float bullet_speed{ 500.0f };
	rb.velocity = dir_norm * bullet_speed;
	bullet.Add<Lifetime>(milliseconds{ 1000 }, true);
	float heading{ dir_norm.Angle() + DegToRad(90.0f) };
	SetRotation(bullet, heading);
	game.sound.Play("bullet1_sound");
}

struct ShootMouseBulletScript : public Script<ShootMouseBulletScript, GlobalMouseScript> {
	void OnMouseDown(Mouse mouse_button) {
		if (mouse_button == Mouse::Left) {
			auto mouse{ GetChild(entity, "mouse") };
			auto mouse_pos{ GetAbsolutePosition(mouse) };
			auto entity_pos{ GetAbsolutePosition(entity) };
			auto dir{ mouse_pos - entity_pos };
			auto dir_norm{ dir.Normalized() };
			CreateStraightBullet(entity.GetScene(), entity_pos, dir_norm);
		}
	}
};

class GameScene : public Scene {
public:
	Entity player;
	Entity mouse;

	void Enter() override {
		SetupWindow();
		SetBackgroundColor(color::LightBlue);

		LoadResources("resources/resources.json");

		CreateRect(*this, { 0, 0 }, { 30, 30 }, color::Red, -1.0f, Origin::TopLeft);
		CreateRect(*this, { resolution.x, 0 }, { 30, 30 }, color::Red, -1.0f, Origin::TopRight);
		CreateRect(*this, resolution, { 30, 30 }, color::Red, -1.0f, Origin::BottomRight);
		CreateRect(*this, { 0, resolution.y }, { 30, 30 }, color::Red, -1.0f, Origin::BottomLeft);

		TopDownPlayerConfig player_config;
		player_config.animation_frame_count = { 4, 3 };
		player_config.animation_frame_size	= { 16, 17 };
		player_config.animation_duration	= milliseconds{ 500 };

		mouse = CreateSprite(*this, "cursor", {});
		AddScript<FollowMouseScript>(mouse);

		player = CreateTopDownPlayer(*this, { 0, 0 }, player_config);
		AddScript<ShootMouseBulletScript>(player);

		AddChild(player, mouse, "mouse");
		IgnoreParentTransform(mouse, true);

		StartFollow(camera, player, FollowConfig{ .teleport_on_start = true });
	}

	void Update() override {
		PTGN_LOG(GetTransform(player));
	}

	void Exit() override {}
};

int main() {
	game.Init("Zombie Game", resolution);
	game.scene.Enter<GameScene>("game");
	return 0;
}