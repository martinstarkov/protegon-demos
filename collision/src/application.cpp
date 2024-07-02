#include "protegon/protegon.h"

using namespace ptgn;

class CollisionTest : public Scene {
public:
	V2_float position1{ 200, 200 };
	V2_float position3{ 300, 300 };
	V2_float position4{ 200, 300 };

	V2_float size1{ 60, 60 };
	V2_float size2{ 30, 30 };

	float radius1{ 30 };
	float radius2{ 20 };

	Color color1{ color::Green };
	Color color2{ color::Blue };

	int options{ 9 };
	int option{ 0 };

	int type{ 2 };
	int types{ 3 };

	CollisionTest() {
		window::SetTitle("'t'=shape type, 'g'=mode, 'r'=line origin");
		window::SetSize({ 600, 600 });
	}

	void Update(float dt) final {
		auto mouse = input::GetMousePosition();

		if (input::KeyDown(Key::T)) {
			option++;
			option = option++ % options;
		}

		if (input::KeyDown(Key::G)) {
			type++;
			type = type++ % types;
		}

		if (input::KeyDown(Key::R)) {
			position4 = mouse;
		}

		V2_float position2 = mouse;

		auto acolor1 = color1;
		auto acolor2 = color2;

		Rectangle<float> aabb1{ position1, size1 };
		Rectangle<float> aabb2{ position2, size2 };
		
		Circle<float> circle1{ position1, radius1 };
		Circle<float> circle2{ position2, radius2 };
		
		Segment<float> line1{ position1, position3 };
		Segment<float> line2{ position2, position4 };

		if (type == 0) { // overlap
			options = 9;
			if (option == 0) {
				if (overlap::PointSegment(position2, line1)) {
					acolor1 = color::Red;
					acolor2 = color::Red;
				}
				line1.Draw(acolor1);
				position2.Draw(acolor2);
			} else if (option == 1) {
				if (overlap::PointCircle(position2, circle1)) {
					bool test = overlap::PointCircle(position2, circle1);
					acolor1 = color::Red;
					acolor2 = color::Red;
				}
				circle1.Draw(acolor1);
				position2.Draw(acolor2);
			} else if (option == 2) {
				if (overlap::PointRectangle(position2, aabb1)) {
					acolor1 = color::Red;
					acolor2 = color::Red;
				}
				aabb1.Draw(acolor1);
				position2.Draw(acolor2);
			} else if (option == 3) {
				if (overlap::SegmentSegment(line2, line1)) {
					acolor1 = color::Red;
					acolor2 = color::Red;
				}
				line1.Draw(acolor1);
				line2.Draw(acolor2);
			} else if (option == 4) {
				if (overlap::SegmentCircle(line2, circle1)) {
					acolor1 = color::Red;
					acolor2 = color::Red;
				}
				line2.Draw(acolor2);
				circle1.Draw(acolor1);
			} else if (option == 5) {
				if (overlap::SegmentRectangle(line2, aabb1)) {
					acolor1 = color::Red;
					acolor2 = color::Red;
				}
				line2.Draw(acolor2);
				aabb1.Draw(acolor1);
			} else if (option == 6) {
				if (overlap::CircleCircle(circle2, circle1)) {
					acolor1 = color::Red;
					acolor2 = color::Red;
				}
				circle2.Draw(acolor2);
				circle1.Draw(acolor1);
			} else if (option == 7) {
				if (overlap::CircleRectangle(circle2, aabb1)) {
					acolor1 = color::Red;
					acolor2 = color::Red;
				}
				aabb1.Draw(acolor1);
				circle2.Draw(acolor2);
			} else if (option == 8) {
				aabb2.pos = mouse - aabb2.size / 2;
				if (overlap::RectangleRectangle(aabb1, aabb2)) {
					acolor1 = color::Red;
					acolor2 = color::Red;
				}
				aabb2.Draw(acolor2);
				aabb1.Draw(acolor1);
			}
		} else if (type == 1) { // intersect
			options = 3;
			const float slop{ 0.005f };
			intersect::Collision c;
			if (option == 0) {
				//circle2.center = circle1.center;
				bool occured{ intersect::CircleCircle(circle2, circle1, c) };
				if (occured) {
					acolor1 = color::Red;
					acolor2 = color::Red;
				}
				circle2.Draw(acolor2);
				circle1.Draw(acolor1);
				if (occured) {
					Circle<float> new_circle{ circle2.center + c.normal * (c.depth + slop), circle2.radius };
					new_circle.Draw(color2);
					Segment<float> l{ circle2.center, new_circle.center };
					l.Draw(color::Gold);
					if (overlap::CircleCircle(new_circle, circle1)) {
						occured = intersect::CircleCircle(new_circle, circle1, c);
						bool overlap{ overlap::CircleCircle(new_circle, circle1) };
						if (overlap) PrintLine("Slop insufficient, overlap reoccurs");
						if (occured) PrintLine("Slop insufficient, intersect reoccurs");
					}
				}
			} else if (option == 1) {
				//circle2.center = aabb1.position;
			    //circle2.center = aabb1.Center();
				bool occured{ intersect::CircleRectangle(circle2, aabb1, c) };
				if (occured) {
					acolor1 = color::Red;
					acolor2 = color::Red;
				}
				aabb1.Draw(acolor1);
				circle2.Draw(acolor2);
				if (occured) {
					Circle<float> new_circle{ circle2.center + c.normal * (c.depth + slop), circle2.radius };
					new_circle.Draw(color2);
					Segment<float> l{ circle2.center, new_circle.center };
					l.Draw(color::Gold);
					if (overlap::CircleRectangle(new_circle, aabb1)) {
						occured = intersect::CircleRectangle(new_circle, aabb1, c);
						bool overlap{ overlap::CircleRectangle(new_circle, aabb1) };
						if (overlap) PrintLine("Slop insufficient, overlap reoccurs");
						if (occured) PrintLine("Slop insufficient, intersect reoccurs");
					}
				}
			} else if (option == 2) {
				aabb2.pos = mouse - aabb2.Half();
				//aabb2.position = aabb1.Center() - aabb2.Half();
				bool occured{ intersect::RectangleRectangle(aabb2, aabb1, c) };
				if (occured) {
					acolor1 = color::Red;
					acolor2 = color::Red;
				}
				aabb2.Draw(acolor2);
				aabb1.Draw(acolor1);
				if (occured) {
					Rectangle<float> new_aabb{ aabb2.pos + c.normal * (c.depth + slop), aabb2.size };
					new_aabb.Draw(color2);
					Segment<float> l{ aabb2.Center(), new_aabb.Center() };
					l.Draw(color::Gold);
					if (overlap::RectangleRectangle(new_aabb, aabb1)) {
						occured = intersect::RectangleRectangle(new_aabb, aabb1, c);
						bool overlap{ overlap::RectangleRectangle(new_aabb, aabb1) };
						if (overlap) PrintLine("Slop insufficient, overlap reoccurs");
						if (occured) PrintLine("Slop insufficient, intersect reoccurs");
					}
				}
			}
		} else if (type == 2) { // dynamic
			options = 3;
			const float slop{ 0.005f };
			dynamic::Collision c;
			if (option == 0) {
				circle2.center = position4;
				V2_float vel{ mouse - circle2.center };
				Circle<float> potential{ circle2.center + vel, circle2.radius };
				potential.Draw(color::Grey);
				Segment<float>{ circle2.center, potential.center }.Draw(color::Grey);
				bool occured{ dynamic::CircleRectangle(circle2, vel, aabb1, c) };
				if (occured) {
					Circle<float> swept{ circle2.center + vel * c.t, circle2.radius };
					Segment<float> normal{ swept.center, swept.center + 50 * c.normal };
					normal.Draw(color::Orange);
					swept.Draw(color::Green);
					acolor1 = color::Red;
					acolor2 = color::Red;
				}
				circle2.Draw(acolor1);
				aabb1.Draw(acolor1);
			} else if (option == 1) {
				circle2.center = position4;
				V2_float vel{ mouse - circle2.center };
				Circle<float> potential{ circle2.center + vel, circle2.radius };
				potential.Draw(color::Grey);
				Segment<float>{ circle2.center, potential.center }.Draw(color::Grey);
				bool occured{ dynamic::CircleCircle(circle2, vel, circle1, c) };
				if (occured) {
					Circle<float> swept{ circle2.center + vel * c.t, circle2.radius };
					Segment<float> normal{ swept.center, swept.center + 50 * c.normal };
					normal.Draw(color::Orange);
					swept.Draw(color::Green);
					acolor1 = color::Red;
					acolor2 = color::Red;
				}
				circle2.Draw(acolor1);
				circle1.Draw(acolor1);
			} else if (option == 2) {
				aabb2.pos = position4 - aabb2.size / 2;
				V2_float vel{ mouse - aabb2.size / 2 - aabb2.pos };
				Rectangle<float> potential{ aabb2.pos + vel, aabb2.size };
				potential.Draw(color::Grey);
				Segment<float>{ aabb2.Center(), potential.Center() }.Draw(color::Grey);
				bool occured{ dynamic::RectangleRectangle(aabb2, vel, aabb1, c) };
				if (occured) {
					Rectangle<float> swept{ aabb2.pos + vel * c.t, aabb2.size };
					Segment<float> normal{ swept.Center(), swept.Center() + 50 * c.normal };
					normal.Draw(color::Orange);
					swept.Draw(color::Green);
					acolor1 = color::Red;
					acolor2 = color::Red;
				}
				aabb2.Draw(acolor1);
				aabb1.Draw(acolor1);
			}

			/*
			if (option == 0) {
				//circle2.center = circle1.center;
				int occured{ dynamic::SegmentCircle(line2, circle1, c) };
				if (occured) {
					acolor1 = color::Red;
					acolor2 = color::Red;
				}
				line2.Draw(acolor2);
				circle2.Draw(acolor2);
				circle1.Draw(acolor1);
				if (occured) {
					Circle<float> new_circle{ circle2.center + line2.Direction() * c.t, circle2.radius };
					new_circle.Draw(acolor2);
				}
			}
			*/
			/*
			if (option == 0) {
				//circle2.center = circle1.center;
				bool occured{ dynamic::CircleCircle(circle2, circle1, c) };
				if (occured) {
					acolor1 = color::Red;
					acolor2 = color::Red;
				}
				circle2.Draw(acolor2);
				circle1.Draw(acolor1);
				if (occured) {
					Circle<float> new_circle{ circle2.center + c.normal * (c.depth + slop), circle2.radius };
					new_circle.Draw(color2);
					Segment<float> l{ circle2.center, new_circle.center };
					l.Draw(color::Gold);
					if (overlap::CircleCircle(new_circle, circle1)) {
						occured = intersect::CircleCircle(new_circle, circle1, c);
						bool overlap{ overlap::CircleCircle(new_circle, circle1) };
						if (overlap) PrintLine("Slop insufficient, overlap reoccurs");
						if (occured) PrintLine("Slop insufficient, intersect reoccurs");
					}
				}
			} else if (option == 1) {
				//circle2.center = aabb1.position;
				//circle2.center = aabb1.Center();
				bool occured{ intersect::CircleRectangle(circle2, aabb1, c) };
				if (occured) {
					acolor1 = color::Red;
					acolor2 = color::Red;
				}
				aabb1.Draw(acolor1);
				circle2.Draw(acolor2);
				if (occured) {
					Circle<float> new_circle{ circle2.center + c.normal * (c.depth + slop), circle2.radius };
					new_circle.Draw(color2);
					Segment<float> l{ circle2.center, new_circle.center };
					l.Draw(color::Gold);
					if (overlap::CircleRectangle(new_circle, aabb1)) {
						occured = intersect::CircleRectangle(new_circle, aabb1, c);
						bool overlap{ overlap::CircleRectangle(new_circle, aabb1) };
						if (overlap) PrintLine("Slop insufficient, overlap reoccurs");
						if (occured) PrintLine("Slop insufficient, intersect reoccurs");
					}
				}
			} else if (option == 2) {
				aabb2.pos = mouse - aabb2.Half();
				//aabb2.position = aabb1.Center() - aabb2.Half();
				bool occured{ intersect::RectangleRectangle(aabb2, aabb1, c) };
				if (occured) {
					acolor1 = color::Red;
					acolor2 = color::Red;
				}
				aabb2.Draw(acolor2);
				aabb1.Draw(acolor1);
				if (occured) {
					Rectangle<float> new_aabb{ aabb2.pos + c.normal * (c.depth + slop), aabb2.size };
					new_aabb.Draw(color2);
					Segment<float> l{ aabb2.Center(), new_aabb.Center() };
					l.Draw(color::Gold);
					if (overlap::RectangleRectangle(new_aabb, aabb1)) {
						occured = intersect::RectangleRectangle(new_aabb, aabb1, c);
						bool overlap{ overlap::RectangleRectangle(new_aabb, aabb1) };
						if (overlap) PrintLine("Slop insufficient, overlap reoccurs");
						if (occured) PrintLine("Slop insufficient, intersect reoccurs");
					}
				}
			}
			*/
		}
	}
};

int main(int c, char** v) {
	ptgn::game::Start<CollisionTest>();
	return 0;
}