#include "protegon/protegon.h"

using namespace ptgn;

const V2_int resolution{ 1280, 720 };
const V2_int tile_size{ 16, 16 };

/*
 * Calculate visibility polygon vertices in clockwise order.
 * Endpoints of the line segments (obstacles) can be ordered arbitrarily.
 * Line segments collinear with the point are ignored.
 */
Polygon GetVisibilityPolygon(const V2_float& o, float radius, const std::vector<Line>& edges) {
	std::vector<std::tuple<float, V2_float>> vecVisibilityPolygonPoints;

	// For each edge in PolyMap
	for (auto& e1 : edges) {
		// Take the start point, then the end point (we could use a pool of
		// non-duplicated points here, it would be more optimal)
		// For each point, cast 3 rays, 1 directly at point
		// and 1 a little bit either side
		V2_float start_r{ e1.a - o };
		V2_float end_r{ e1.b - o };
		float start_ang{ start_r.Angle() };
		float end_ang{ end_r.Angle() };

		auto raycast = [&](auto ang) {
			// Create ray along angle for required distance
			V2_float rd{ radius * V2_float{ std::cosf(ang), std::sinf(ang) } };

			float min_t1 = INFINITY;
			V2_float min_p;
			float min_ang = 0;
			bool bValid	  = false;

			// Check for ray intersection with all edges
			for (auto& e2 : edges) {
				// Create line segment vector
				V2_float s{ e2.b - e2.a };

				V2_float srd{ s - rd };

				if (srd.IsZero()) {
					continue;
				}

				// t2 is normalised distance from line segment start to line segment end of
				// intersect point
				float t2 =
					(rd.x * (e2.a.y - o.y) + (rd.y * (o.x - e2.a.x))) / (s.x * rd.y - s.y * rd.x);
				// t1 is normalised distance from source along ray to ray length of
				// intersect point
				float t1 = (e2.a.x + s.x * t2 - o.x) / rd.x;

				// If intersect point exists along ray, and along line
				// segment then intersect point is valid
				if (t1 > 0 && t2 >= 0 && t2 <= 1.0f) {
					// Check if this intersect point is closest to source. If
					// it is, then store this point and reject others
					if (t1 < min_t1) {
						min_t1 = t1;
						min_p  = o + rd * t1;
						V2_float dir{ min_p - o };
						min_ang = dir.Angle();
						bValid	= true;
					}
				}
			}

			if (bValid) { // Add intersection point to visibility polygon perimeter
				vecVisibilityPolygonPoints.push_back({ min_ang, min_p });
			}
		};

		raycast(start_ang - 0.0001f);
		raycast(start_ang);
		raycast(start_ang + 0.0001f);
		raycast(end_ang - 0.0001f);
		raycast(end_ang);
		raycast(end_ang + 0.0001f);
	}

	// Sort perimeter points by angle from source. This will allow
	// us to draw a triangle fan.
	std::sort(
		vecVisibilityPolygonPoints.begin(), vecVisibilityPolygonPoints.end(),
		[&](const std::tuple<float, V2_float>& t1, const std::tuple<float, V2_float>& t2) {
			return std::get<0>(t1) < std::get<0>(t2);
		}
	);

	Polygon p;

	p.vertices.resize(vecVisibilityPolygonPoints.size());

	for (std::size_t i = 0; i < vecVisibilityPolygonPoints.size(); ++i) {
		auto [t, point] = vecVisibilityPolygonPoints[i];
		p.vertices[i]	= point;
	}

	// Remove duplicate (or simply similar) points from polygon
	auto it = std::unique(
		p.vertices.begin(), p.vertices.end(),
		[&](const V2_float& t1, const V2_float& t2) {
			return std::fabs(t1.x - t2.x) < 0.1f && std::fabs(t1.y - t2.y) < 0.1f;
		}
	);

	p.vertices.resize(std::distance(p.vertices.begin(), it));

	return p;
}

std::vector<Triangle> GetVisibilityTriangles(const V2_float& o, const Polygon& visibility_polygon) {
	std::vector<Triangle> triangles;

	const auto& p{ visibility_polygon.vertices };

	PTGN_ASSERT(p.size() >= 3, "Cannot get visibility triangles for incomplete visibility polygon");

	triangles.reserve(p.size() / 2);

	for (std::size_t i = 0; i < p.size() - 1; i++) {
		triangles.emplace_back(o, p[i], p[i + 1]);
	}

	triangles.emplace_back(o, p[p.size() - 1], p[0]);

	return triangles;
}

std::vector<Triangle> GetVisibilityTriangles(
	const V2_float& o, float radius, const std::vector<Line>& edges
) {
	return GetVisibilityTriangles(o, GetVisibilityPolygon(o, radius, edges));
}

struct Blank {
	bool visited{ false };
	V2_int grid_coord;
};

class GameScene : public Scene {
public:
	V2_float scale;

	Grid<ecs::Entity> grid;

	ecs::Manager manager;

	Rect boundary{ {}, resolution, Origin::TopLeft };

	ecs::Entity CreateBlank(const V2_int& grid_coord) {
		ecs::Entity e = manager.CreateEntity();
		auto& b		  = e.Add<Blank>();
		b.grid_coord  = grid_coord;
		manager.Refresh();
		return e;
	}

	ecs::Entity CreateWall(const V2_int& pos, const V2_int& size) {
		ecs::Entity e = manager.CreateEntity();
		e.Add<Transform>(pos, 0.0f, scale);
		e.Add<BoxCollider>(e, size, Origin::TopLeft);
		manager.Refresh();
		return e;
	}

	std::vector<Line> walls;

	ecs::EntitiesWith<Blank> blanks;

	GameScene(const path& level_path) {
		Surface level{ level_path };
		grid  = Grid<ecs::Entity>{ level.GetSize() };
		scale = V2_float{ resolution } / V2_float{ tile_size } / grid.GetSize();
		std::unordered_set<V2_int> visited;
		level.ForEachPixel([&](auto start, auto c) {
			if (c == color::White) {
				grid.Set(start, CreateBlank(start));
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
		for (auto [e, b] : manager.EntitiesWith<BoxCollider>()) {
			Rect r{ e.Get<BoxCollider>().GetAbsoluteRect() };
			walls = ConcatenateVectors(walls, ToVector(r.GetWalls()));
		}
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
		blanks = manager.EntitiesWith<Blank>();
	}

	void Init() override {}

	void Update() override {
		V2_float mouse_pos{ game.input.GetMousePosition() };

		for (auto wall : walls) {
			wall.Draw(color::Red, 5.0f);
		}

		auto vis_poly	   = GetVisibilityPolygon(mouse_pos, 3000.0f, walls);
		auto vis_triangles = GetVisibilityTriangles(mouse_pos, vis_poly);

		/*if (game.input.MousePressed(Mouse::Left)) {
			for (const auto& t : vis_triangles) {
				for (auto [e, b] : blanks) {
					if (b.visited) {
						continue;
					}
					Rect r{ b.grid_coord * tile_size * scale, tile_size * scale, Origin::TopLeft };
					if (r.Overlaps(t.a) || r.Overlaps(t.b) || r.Overlaps(t.c) || r.Overlaps(t)) {
						b.visited = true;
					}
				}
			}
		}*/

		for (const auto& t : vis_triangles) {
			t.Draw(color::Orange, 2.0f);
		}

		/*for (auto [e, b] : blanks) {
			game.draw.Rect(
				Rect{ b.grid_coord * tile_size * scale, tile_size * scale, Origin::TopLeft },
				b.visited ? color::Orange : color::Black
			);
		}*/
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
		game.scene.Load<GameScene>("game", "resources/level/3.png");
		game.scene.AddActive("game");
	}

	void Update() override {}
};

int main() {
	game.Start<SetupScene>();
	return 0;
}