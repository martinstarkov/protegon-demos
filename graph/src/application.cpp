#include "protegon/protegon.h"

using namespace ptgn;

struct DummySensor {
	[[nodiscard]] float GetValue() const {
		if (game.input.KeyPressed(Key::K_1)) {
			return 1.0f;
		} else if (game.input.KeyPressed(Key::K_2)) {
			return 2.0f;
		} else if (game.input.KeyPressed(Key::K_3)) {
			return 3.0f;
		} else if (game.input.KeyPressed(Key::K_4)) {
			return 4.0f;
		} else if (game.input.KeyPressed(Key::K_5)) {
			return 5.0f;
		} else if (game.input.KeyPressed(Key::K_6)) {
			return 6.0f;
		} else if (game.input.KeyPressed(Key::K_7)) {
			return 7.0f;
		} else if (game.input.KeyPressed(Key::K_8)) {
			return 8.0f;
		} else if (game.input.KeyPressed(Key::K_9)) {
			return 9.0f;
		} else if (game.input.KeyPressed(Key::K_0)) {
			return 0.0f;
		}
		// No key pressed.
		return 5.0f;
	}
};

struct DataPoints {
	std::vector<V2_float> points;

	// @return Minimum values along both axes.
	[[nodiscard]] V2_float GetMax() const {
		return { GetMaxX(), GetMaxY() };
	}

	// @return Minimum values along both axes.
	[[nodiscard]] V2_float GetMin() const {
		return { GetMinX(), GetMinY() };
	}

	// Sorts point vector by ascending x values (smallest to largest).
	void SortAscendingByX() {
		std::sort(points.begin(), points.end(), [](const V2_float& a, const V2_float& b)
		{
			return a.x < b.x;
		});
	}
	[[nodiscard]] float GetMaxX() const {
		return (*std::max_element(points.begin(), points.end(), [](const V2_float& a, const V2_float& b)
		{
			return a.x < b.x;
		})).x;
	}

	[[nodiscard]] float GetMaxY() const {
		return (*std::max_element(points.begin(), points.end(), [](const V2_float& a, const V2_float& b)
		{
			return a.y < b.y;
		})).y;
	}

	[[nodiscard]] float GetMinX() const {
		return (*std::min_element(points.begin(), points.end(), [](const V2_float& a, const V2_float& b)
		{
			return a.x < b.x;
		})).x;
	}

	[[nodiscard]] float GetMinY() const {
		return (*std::min_element(points.begin(), points.end(), [](const V2_float& a, const V2_float& b)
		{
			return a.y < b.y;
		})).y;
	}
};

// Plot Properties:

struct BackgroundColor : public ColorComponent {
	using ColorComponent::ColorComponent;
};
struct DataPointColor : public ColorComponent {
	using ColorComponent::ColorComponent;
};
struct DataPointRadius : public FloatComponent {
	using FloatComponent::FloatComponent;
};
struct LineColor : public ColorComponent {
	using ColorComponent::ColorComponent;
};
struct LineWidth : public FloatComponent {
	using FloatComponent::FloatComponent;
};

class Plot {
public:
	Plot() = default;

	// @param data Data to be graphed.
	// @param min Minimum axis values.
	// @param max Maximum axis values.
	void Init(const DataPoints& data, const V2_float& min, const V2_float& max) {
		entity_ = manager_.CreateEntity();
		manager_.Refresh();
		
		data_ = data;
		data_.SortAscendingByX();
		SetAxisLimits(min, max);

		// Default plot properties:
		AddProperty(BackgroundColor{ color::White });
		//AddProperty(DataPointColor{ color::Red });
		//AddProperty(DataPointRadius{ 1.0f });
		AddProperty(LineColor{ color::Blue });
		AddProperty(LineWidth{ 1.0f });
	}

	void SetAxisLimits(const V2_float& min, const V2_float& max) {
		PTGN_ASSERT(min.x < max.x);
		PTGN_ASSERT(min.y < max.y);
		// TODO: Move to entity components.
		min_axis_ = min;
		max_axis_ = max;
		axis_extents_ = max - min;
	}

	[[nodiscard]] V2_float GetAxisMax() const {
		return max_axis_;
	}
	[[nodiscard]] V2_float GetAxisMin() const {
		return min_axis_;
	}

	void AddDataPoint(const V2_float& point) {
		data_.points.push_back(point);
		data_.SortAscendingByX();
	}

	// @param destination Destination rectangle where to draw the plot. Default of {} results in fullscreen.
	void Draw(const Rect& destination = {}) const {
		PTGN_ASSERT(entity_ != ecs::null, "Cannot draw plot before it has been initialized");
		
		Rect dest{ destination };

		if (dest.IsZero()) {
			// TODO: Switch to using dest = Rect::Fullscreen(); after rework is done.
			dest.position = {};
			dest.size = game.window.GetSize();
			dest.origin = Origin::TopLeft;
		}

		DrawPlotArea(dest);
	}

	template <typename T>
	void AddProperty(const T& property) {
		PTGN_ASSERT(entity_ != ecs::null, "Cannot add plot property before plot has been initialized");
		static_assert(tt::is_any_of_v<T, BackgroundColor, DataPointRadius, DataPointColor, LineColor, LineWidth>, "Invalid type for plot property");
		entity_.Add<T>(property);
	}

	[[nodiscard]] DataPoints& GetData() {
		return data_;
	}
private:
	// @param destination Destination rectangle where to draw the plot area.
	void DrawPlotArea(const Rect& dest) const {
		PTGN_ASSERT((entity_.Has<BackgroundColor>()));

		PTGN_ASSERT((entity_.Has<DataPointColor, DataPointRadius>()) || (entity_.Has<LineColor, LineWidth>()));

		dest.Draw(entity_.Get<BackgroundColor>(), -1.0f);

		DrawPoints(dest);
	}

	enum class PointYLocation {
		Within,
		Above,
		Below
	};

	void DrawPoints(const Rect& dest) const {
		auto get_frac = [&](std::size_t index) {
			V2_float frac{ (data_.points[index] - min_axis_) / axis_extents_ };
			frac.y = 1.0f - frac.y;
			PTGN_ASSERT(frac.x >= 0.0f && frac.x <= 1.0f);
			return frac;
		};

		auto get_local_pixel = [&](const V2_float& frac) {
			return V2_float{ dest.size * frac };
		};

		auto draw_marker = [&](const V2_float& frac){
			if (!entity_.Has<DataPointColor>() || !entity_.Has<DataPointRadius>()) {
				return;
			}
			if (frac.y < 0.0f || frac.y > 1.0f) {
				return;
			}
			V2_float dest_pixel{ dest.position + get_local_pixel(frac) };
			dest_pixel.Draw(entity_.Get<DataPointColor>(), entity_.Get<DataPointRadius>());
		};

		auto get_intersection_point = [&](const std::array<Line, 4>& edges, const V2_float& start, const V2_float& end) {
			Line l{ start, end };
			Raycast ray;
			for (const auto& edge : edges) {
				auto raycast = l.Raycast(edge);
				if (raycast.Occurred() && raycast.t < ray.t) {
					ray = raycast;
				}
			}
			V2_float point{ l.a + l.Direction() * ray.t };
			return point;
		};

		auto draw_line = [&](const V2_float& frac_current, const V2_float& frac_next) {
			if (!entity_.Has<LineColor>() || !entity_.Has<LineWidth>()) {
				return;
			}
			V2_float start{ get_local_pixel(frac_current) };
			V2_float end{ get_local_pixel(frac_next) };

			Rect boundary{ {}, dest.size, Origin::TopLeft };
			auto edges{ boundary.GetWalls() };
			
			V2_float p1{ get_intersection_point(edges, start, end) };

			Line l{ start, p1 };

			if (p1 != end) {
				V2_float p2{ get_intersection_point(edges, end, start) };
				if (p2 != p1) {
					l.a = p2;
					l.b = p1;
				}
			}

			// Offset line onto actual plot area.
			l.a += dest.position;
			l.b += dest.position;

			l.Draw(entity_.Get<LineColor>(), entity_.Get<LineWidth>());
		};
		
		// Note: Data must be sorted for this loop to draw lines correctly.
		for (std::size_t i = 0; i < data_.points.size(); ++i) {
			const auto& point{ data_.points[i] };
			if (point.x < min_axis_.x) {
				// data point has been passed on the x axis.
				continue;
			}
			if (point.x > max_axis_.x) {
				// data point is passed the x axis. Given that data_.points is sorted, this means graphing can stop.
				break;
			}
			V2_float frac_current{ get_frac(i) };
			if (i + 1 == data_.points.size()) {
				draw_marker(frac_current);
			} else {
				draw_line(frac_current, get_frac(i + 1));

				draw_marker(frac_current);
			}
		}
	}

	DataPoints data_;

	V2_float min_axis_; // min axis values
	V2_float max_axis_; // max axis values
	V2_float axis_extents_; // max_axis_ - min_axis_

	ecs::Entity entity_;
	ecs::Manager manager_;
};

enum class AxisExpansionType {
	IntervalShift,
	XDataMinMax,
	ContinuousShift,
	None
};

class PlotExample : public Scene {
public:
	DummySensor sensor;
	Plot plot;

	Timer sampling;
	Timer clock;

	using x_axis_unit = duration<float, seconds::period>;
	x_axis_unit x_axis_length{ 10.0f };

	milliseconds samping_rate{ 250 };

	AxisExpansionType axis_type{ AxisExpansionType::ContinuousShift };

	PlotExample() {
		game.window.SetTitle("Plot");
		game.window.SetSize({ 800, 800 });
	}

	void Init() override {
		game.draw.SetClearColor(color::Transparent);

		plot.Init({}, { 0, 0 }, { x_axis_length.count(), 10.0f });

		clock.Start();
		sampling.Start();
		RecordSample();
	}

	void RecordSample() {
		float sampling_time{ clock.Elapsed<x_axis_unit>().count() };
		float sensor_value{ sensor.GetValue() };
		plot.AddDataPoint({ sampling_time, sensor_value });
		PTGN_LOG("Sensor value: ", sensor_value);
	}

	void Update() override {
		if (sampling.Completed(samping_rate)) {
			RecordSample();
		}

		switch (axis_type) {
			case AxisExpansionType::IntervalShift: {
				V2_float min{plot.GetAxisMin()};
				V2_float max{plot.GetAxisMax()};
				auto& points{ plot.GetData().points };
				if (!points.empty() && points.back().x > max.x) {
					plot.SetAxisLimits({ min.x + x_axis_length.count(), min.y }, { max.x + x_axis_length.count(), max.y });
				}
				break;
			}
			case AxisExpansionType::XDataMinMax: {
				plot.SetAxisLimits(plot.GetAxisMin(), { plot.GetData().GetMaxX(), plot.GetAxisMax().y });
				break;
			}
			case AxisExpansionType::ContinuousShift: {
				auto& points{ plot.GetData().points };
				if (!points.empty()) {
					auto final_point{ points.back() };
					plot.SetAxisLimits({ final_point.x - x_axis_length.count(), plot.GetAxisMin().y }, { final_point.x, plot.GetAxisMax().y });
				}
				break;
			}
			case AxisExpansionType::None:
				break;
			default:
				break;
		}

		plot.Draw();
	}
};

int main(int c, char** v) {
	game.Start<PlotExample>();
	return 0;
}
