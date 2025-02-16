#include "protegon/protegon.h"

using namespace ptgn;

constexpr V2_int window_size{ 1280, 720 };
constexpr Color window_color{ color::Transparent };
constexpr const char* window_title{ "Organ Delivery" };
constexpr float zoom{ 4.0f };

ecs::Entity CreateCar(
	ecs::Manager& manager, std::string_view texture_key, const V2_float& position
) {
	auto entity{ CreateSprite(manager, texture_key) };
	auto& transform{ entity.Add<Transform>(position) };
	return entity;
}

class GameScene : public Scene {
public:
	ecs::Entity car;

	void Enter() override {
		camera.primary.SetZoom(zoom);

		game.texture.Load("resources/json/textures.json");

		car = CreateCar(manager, "car", { 0, 0 });

		camera.primary.StartFollow(car);
	}

	void Update() override {}
};

int main() {
	game.Init(window_title, window_size, window_color);
	game.scene.Enter<GameScene>("game");
	return 0;
}