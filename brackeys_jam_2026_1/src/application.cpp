#include "math/geometry/circle.h"
#include "protegon/protegon.h"
#include "renderer/api/origin.h"

using namespace ptgn;

constexpr V2_int resolution{ 320, 180 };
constexpr V2_int world_size{ 480, 360 };

constexpr CollisionCategory category_player{ 0 };
constexpr CollisionCategory category_wall{ 1 };
constexpr CollisionCategory category_enemy{ 2 };
constexpr CollisionCategory category_enemy_projectile{ 3 };
constexpr CollisionCategory category_player_projectile{ 4 };

void SetupWindow() {
	game.window.SetSize(resolution * 4);
	game.renderer.SetScalingMode(ScalingMode::IntegerScale);
	// game.window.SetSetting(WindowSetting::Maximized);
}

struct FollowMouseScript : public Script<FollowMouseScript> {
	void OnUpdate() override {
		SetPosition(entity, entity.GetScene().input.GetMousePosition());
	}
};

void CreateStraightBullet(Scene& scene, const V2_float& start_pos, const V2_float& dir_norm);

struct BulletDisappearScript : public Script<BulletDisappearScript, CollisionScript> {
	void OnCollision([[maybe_unused]] Collision collision) {
		entity.Destroy();
	}
};

struct ShootMouseBulletScript : public Script<ShootMouseBulletScript, GlobalMouseScript> {
	void OnMouseDown(Mouse mouse_button) {
		if (mouse_button == Mouse::Left) {
			auto mouse{ GetChild(entity, "mouse") };
			auto mouse_pos{ GetAbsolutePosition(mouse) };
			auto entity_pos{ GetAbsolutePosition(GetChild(entity, "body")) };
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

	void CreateHitbox(const V2_float& top_left, const V2_float& size) {
		auto hitbox{ CreateEntity() };
		SetPosition(hitbox, top_left - world_size / 2.0f);
		SetDrawOrigin(hitbox, Origin::TopLeft);
		auto& collider = hitbox.Add<Collider>(Rect{ size });
		collider.SetCollisionCategory(category_wall);
		collider.AddCollidesWith(category_player);
		collider.AddCollidesWith(category_player_projectile);
		collider.AddCollidesWith(category_enemy_projectile);
		collider.AddCollidesWith(category_enemy);
		hitbox.Add<RigidBody>().immovable = true;
	}

	void Enter() override {
		SetColliderVisibility(true);

		SetupWindow();
		SetBackgroundColor(color::LightBlue);

		LoadResources("resources/resources.json");

		camera.SetBounds(-world_size / 2.0f, world_size);
		physics.SetBounds(-world_size / 2.0f, world_size, BoundaryBehavior::StopVelocity);

		json walls = game.json.Get("walls_json");

		for (json hitbox : walls.at("hitboxes")) {
			CreateHitbox(hitbox.at("position").get<V2_float>(), hitbox.at("size").get<V2_float>());
		}
		CreateSprite(*this, "arena", { 0, 0 });

		CreateRect(*this, -world_size / 2.0f, { 30, 30 }, color::Blue, -1.0f, Origin::TopLeft);
		CreateRect(*this, { 0, 0 }, { 30, 30 }, color::Red, -1.0f, Origin::TopLeft);
		CreateRect(*this, { resolution.x, 0 }, { 30, 30 }, color::Red, -1.0f, Origin::TopRight);
		CreateRect(*this, resolution, { 30, 30 }, color::Red, -1.0f, Origin::BottomRight);
		CreateRect(*this, { 0, resolution.y }, { 30, 30 }, color::Red, -1.0f, Origin::BottomLeft);

		TopDownPlayerConfig player_config;
		player_config.animation_frame_count = { 4, 3 };
		player_config.animation_frame_size	= { 16, 17 };
		player_config.animation_duration	= milliseconds{ 500 };
		player_config.body_hitbox_offset	= { 0, 4 };

		mouse = CreateSprite(*this, "cursor", {});
		AddScript<FollowMouseScript>(mouse);

		player = CreateTopDownPlayer(*this, { 0, 0 }, player_config);
		auto& player_collider{ GetChild(player, "body").Get<Collider>() };
		player_collider.SetCollisionCategory(category_player);
		player_collider.AddCollidesWith(category_wall);
		player_collider.AddCollidesWith(category_enemy);
		// player.Get<RigidBody>().immovable = true;
		AddScript<ShootMouseBulletScript>(player);

		AddChild(player, mouse, "mouse");
		IgnoreParentTransform(mouse, true);

		StartFollow(camera, player, FollowConfig{ .teleport_on_start = true });
	}
};

void CreateStraightBullet(Scene& scene, const V2_float& start_pos, const V2_float& dir_norm) {
	Sprite bullet = CreateSprite(scene, "bullet1", start_pos);
	auto& rb	  = bullet.Add<RigidBody>();
	float bullet_speed{ 500.0f };
	rb.velocity = dir_norm * bullet_speed;
	bullet.Add<Lifetime>(milliseconds{ 1000 }, true);
	float heading{ dir_norm.Angle() + DegToRad(90.0f) };
	SetRotation(bullet, heading);
	game.sound.Play("bullet1_sound");
	auto& collider = bullet.Add<Collider>(Circle{ bullet.GetTextureSize().y / 2.0f });
	collider.AddCollidesWith(category_wall);
	collider.AddCollidesWith(category_enemy);
	collider.SetCollisionCategory(category_player_projectile);
	collider.response = CollisionResponse::Stick;
	auto light		  = CreatePointLight(
		   scene, {}, bullet.GetTextureSize().y / 2.0f * 2.0f, color::Red, 1.0f, 2.0f
	   );
	AddChild(bullet, light);
	AddScript<BulletDisappearScript>(bullet);
}

int main() {
	game.Init("Zombie Game", resolution);
	game.scene.Enter<GameScene>("game");
	return 0;
}