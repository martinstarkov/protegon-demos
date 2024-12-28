#include "protegon/protegon.h"

using namespace ptgn;

const V2_int resolution{ 1280, 720 };
const V2_int tile_size{ 16, 16 };

/* Calculate visibility polygon vertices in clockwise order.
 * Endpoints of the line segments (obstacles) can be ordered arbitrarily.
 * Line segments collinear with the point are ignored.
 * @param point - position of the observer
 * @param begin iterator of the list of line segments (obstacles)
 * @param end iterator of the list of line segments (obstacles)
 * @return vector of vertices of the visibility polygon
 */
std::vector<V2_float> VisibilityPolygon(const V2_float& point, const std::vector<Line>& walls) {
	return {};
}

std::vector<std::tuple<float, float, float>> CalculateVisibilityPolygon(
	float ox, float oy, float radius, const std::vector<Line>& vecEdges
) {
	std::vector<std::tuple<float, float, float>> vecVisibilityPolygonPoints;

	// For each edge in PolyMap
	for (auto& e1 : vecEdges) {
		// Take the start point, then the end point (we could use a pool of
		// non-duplicated points here, it would be more optimal)
		for (int i = 0; i < 2; i++) {
			float rdx, rdy;
			rdx = (i == 0 ? e1.a.x : e1.b.x) - ox;
			rdy = (i == 0 ? e1.a.y : e1.b.y) - oy;

			float base_ang = std::atan2f(rdy, rdx);

			float ang = 0;
			// For each point, cast 3 rays, 1 directly at point
			// and 1 a little bit either side
			for (int j = 0; j < 3; j++) {
				if (j == 0) {
					ang = base_ang - 0.0001f;
				}
				if (j == 1) {
					ang = base_ang;
				}
				if (j == 2) {
					ang = base_ang + 0.0001f;
				}

				// Create ray along angle for required distance
				rdx = radius * std::cosf(ang);
				rdy = radius * std::sinf(ang);

				float min_t1 = INFINITY;
				float min_px = 0, min_py = 0, min_ang = 0;
				bool bValid = false;

				// Check for ray intersection with all edges
				for (auto& e2 : vecEdges) {
					// Create line segment vector
					float sdx = e2.b.x - e2.a.x;
					float sdy = e2.b.y - e2.a.y;

					if (std::fabs(sdx - rdx) > 0.0f && std::fabs(sdy - rdy) > 0.0f) {
						// t2 is normalised distance from line segment start to line segment end of
						// intersect point
						float t2 =
							(rdx * (e2.a.y - oy) + (rdy * (ox - e2.a.x))) / (sdx * rdy - sdy * rdx);
						// t1 is normalised distance from source along ray to ray length of
						// intersect point
						float t1 = (e2.a.x + sdx * t2 - ox) / rdx;

						// If intersect point exists along ray, and along line
						// segment then intersect point is valid
						if (t1 > 0 && t2 >= 0 && t2 <= 1.0f) {
							// Check if this intersect point is closest to source. If
							// it is, then store this point and reject others
							if (t1 < min_t1) {
								min_t1	= t1;
								min_px	= ox + rdx * t1;
								min_py	= oy + rdy * t1;
								min_ang = std::atan2f(min_py - oy, min_px - ox);
								bValid	= true;
							}
						}
					}
				}

				if (bValid) { // Add intersection point to visibility polygon perimeter
					vecVisibilityPolygonPoints.push_back({ min_ang, min_px, min_py });
				}
			}
		}
	}

	// Sort perimeter points by angle from source. This will allow
	// us to draw a triangle fan.
	std::sort(
		vecVisibilityPolygonPoints.begin(), vecVisibilityPolygonPoints.end(),
		[&](const std::tuple<float, float, float>& t1, const std::tuple<float, float, float>& t2) {
			return std::get<0>(t1) < std::get<0>(t2);
		}
	);

	return vecVisibilityPolygonPoints;
}

class GameScene : public Scene {
public:
	V2_float scale;

	Grid<ecs::Entity> grid;

	ecs::Manager manager;

	Rect boundary{ {}, resolution, Origin::TopLeft };

	ecs::Entity CreateWall(const V2_int& pos, const V2_int& size) {
		ecs::Entity e = manager.CreateEntity();
		e.Add<Transform>(pos, 0.0f, scale);
		e.Add<BoxCollider>(e, size, Origin::TopLeft);
		manager.Refresh();
		return e;
	}

	std::vector<Line> walls;

	GameScene(const path& level_path) {
		Surface level{ level_path };
		grid  = Grid<ecs::Entity>{ level.GetSize() };
		scale = V2_float{ resolution } / V2_float{ tile_size } / grid.GetSize();
		std::unordered_set<V2_int> visited;
		level.ForEachPixel([&](auto start, auto c) {
			if (c == color::White) {
				// Do nothing.
			} else if (c == color::Black) {
				if (visited.count(start) > 0) {
					return;
				}
				// Make wall.
				V2_int count{ 1, 1 };
				// - count because we dont want to loop over the current pixel
				V2_int remaining{ grid.GetSize() - start - count };
				bool found{ false };
				visited.insert(start);
				auto loop_for = [&](int length, V2_int dir) {
					for (int i{ 1 }; i < length; i++) {
						V2_int pixel{ start + V2_int{ i, i } * dir };
						if (level.GetPixel(pixel) == color::Black) {
							if (visited.count(pixel) > 0) {
								return;
							}
							visited.insert(pixel);
							found  = true;
							count += dir;
						} else {
							return;
						}
					}
				};
				loop_for(remaining.x, { 1, 0 });
				if (!found) {
					loop_for(remaining.y, { 0, 1 });
				}
				PTGN_ASSERT(
					count.x >= 1 && count.y >= 1, "Algorithm failed because count became negative"
				);
				PTGN_ASSERT(
					count.x == 1 || count.y == 1,
					"Algorithm failed because it looped in both directions"
				);
				grid.Set(start, CreateWall(start * tile_size, tile_size * count));
			}
		});
		walls = ConcatenateVectors(walls, ToVector(boundary.GetWalls()));
		grid.ForEachElement([&](auto e) {
			if (e == ecs::null) {
				return;
			}
			Rect r{ e.Get<BoxCollider>().GetAbsoluteRect() };
			walls = ConcatenateVectors(walls, ToVector(r.GetWalls()));
		});
		walls.erase(
			std::remove_if(
				walls.begin(), walls.end(),
				[&](const auto& line) {
					for (const auto& v : walls) {
						if (v == line) {
							continue;
						}
						if (v.Contains(line)) {
							return true;
						}
					}
					return false;
				}
			),
			walls.end()
		);
	}

	void Init() override {}

	void Update() override {
		V2_float mouse_pos{ game.input.GetMousePosition() };

		for (auto wall : walls) {
			wall.Draw(color::Red, 5.0f);
		}

		auto vecVisibilityPolygonPoints =
			CalculateVisibilityPolygon(mouse_pos.x, mouse_pos.y, 1000.0f, walls);

		if (!mouse_pos.IsZero()) {
			int nRaysCast = vecVisibilityPolygonPoints.size();

			// Remove duplicate (or simply similar) points from polygon
			auto it = std::unique(
				vecVisibilityPolygonPoints.begin(), vecVisibilityPolygonPoints.end(),
				[&](const std::tuple<float, float, float>& t1,
					const std::tuple<float, float, float>& t2) {
					return fabs(std::get<1>(t1) - std::get<1>(t2)) < 0.1f &&
						   fabs(std::get<2>(t1) - std::get<2>(t2)) < 0.1f;
				}
			);

			vecVisibilityPolygonPoints.resize(std::distance(vecVisibilityPolygonPoints.begin(), it)
			);

			int nRaysCast2 = vecVisibilityPolygonPoints.size();

			// Draw each triangle in fan
			for (int i = 0; i < vecVisibilityPolygonPoints.size() - 1; i++) {
				game.draw.Triangle(
					mouse_pos,
					{ std::get<1>(vecVisibilityPolygonPoints[i]),
					  std::get<2>(vecVisibilityPolygonPoints[i]) },

					{ std::get<1>(vecVisibilityPolygonPoints[i + 1]),
					  std::get<2>(vecVisibilityPolygonPoints[i + 1]) },
					color::Orange
				);
			}

			// Fan will have one open edge, so draw last point of fan to first
			game.draw.Triangle(
				mouse_pos,

				{ std::get<1>(vecVisibilityPolygonPoints[vecVisibilityPolygonPoints.size() - 1]),
				  std::get<2>(vecVisibilityPolygonPoints[vecVisibilityPolygonPoints.size() - 1]) },

				{ std::get<1>(vecVisibilityPolygonPoints[0]),
				  std::get<2>(vecVisibilityPolygonPoints[0]) },
				color::Orange
			);
		}

		/*grid.ForEachElement([](auto e) {
			if (e == ecs::null) {
				return;
			}
			Rect r{ e.Get<BoxCollider>().GetAbsoluteRect() };
			r.Draw(color::Red, 5.0f);
		});*/
	}
};

class SetupScene : public Scene {
public:
	SetupScene() {}

	void Init() override {
		game.window.SetSize(resolution);
		game.window.SetTitle("Light Trap");
		game.draw.SetClearColor(color::Black);
		game.scene.Load<GameScene>("game", "resources/level/1.png");
		game.scene.AddActive("game");
	}

	void Update() override {}
};

int main() {
	game.Start<SetupScene>();
	return 0;
}