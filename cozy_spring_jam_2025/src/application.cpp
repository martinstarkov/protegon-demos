#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <iterator>
#include <list>
#include <map>
#include <memory>
#include <new>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "common/assert.h"
#include "components/common.h"
#include "components/movement.h"
#include "components/transform.h"
#include "core/entity.h"
#include "core/game.h"
#include "core/manager.h"
#include "debug/log.h"
#include "math/rng.h"
#include "math/vector2.h"
#include "math/vector4.h"
#include "rendering/api/blend_mode.h"
#include "rendering/api/color.h"
#include "rendering/batching/vertex.h"
#include "rendering/buffers/buffer.h"
#include "rendering/buffers/buffer_layout.h"
#include "rendering/buffers/frame_buffer.h"
#include "rendering/buffers/vertex_array.h"
#include "rendering/gl/gl_renderer.h"
#include "rendering/gl/gl_types.h"
#include "rendering/resources/render_target.h"
#include "rendering/resources/shader.h"
#include "rendering/resources/texture.h"
#include "scene/camera.h"
#include "scene/scene.h"
#include "scene/scene_manager.h"

using namespace ptgn;

constexpr V2_int window_size{ 1000, 1000 }; //{ 1280, 720 };

struct AABB {
	V2_float min;
	V2_float max;

	bool intersects(const AABB& other) const {
		return !(
			max.x < other.min.x || min.x > other.max.x || max.y < other.min.y || min.y > other.max.y
		);
	}

	bool contains(const V2_float& point) const {
		return point.x >= min.x && point.x <= max.x && point.y >= min.y && point.y <= max.y;
	}

	bool contains(const AABB& other) const {
		return min.x <= other.min.x && max.x >= other.max.x && min.y <= other.min.y &&
			   max.y >= other.max.y;
	}
};

// --- Quadtree Node ---
class QuadtreeNode {
public:
	AABB bounds;
	std::vector<Entity> objects;
	QuadtreeNode* children[4] = { nullptr, nullptr, nullptr, nullptr };
	int maxObjects			  = 4;
	int maxLevels			  = 5;
	int level;

	QuadtreeNode(int lvl, const AABB& bounds_) : level(lvl), bounds(bounds_) {}

	~QuadtreeNode() {
		for (auto& c : children) {
			if (c) {
				delete c;
			}
		}
	}

	bool isLeaf() const {
		return children[0] == nullptr;
	}

	void subdivide() {
		float halfWidth	 = (bounds.max.x - bounds.min.x) / 2.0f;
		float halfHeight = (bounds.max.y - bounds.min.y) / 2.0f;
		V2_float mid	 = bounds.min + V2_float(halfWidth, halfHeight);

		children[0] = new QuadtreeNode(level + 1, AABB{ bounds.min, mid });
		children[1] = new QuadtreeNode(
			level + 1, AABB{ V2_float(mid.x, bounds.min.y), V2_float(bounds.max.x, mid.y) }
		);
		children[2] = new QuadtreeNode(
			level + 1, AABB{ V2_float(bounds.min.x, mid.y), V2_float(mid.x, bounds.max.y) }
		);
		children[3] = new QuadtreeNode(level + 1, AABB{ mid, bounds.max });
	}

	int getChildIndex(const AABB& box) const {
		for (int i = 0; i < 4; i++) {
			if (children[i] && children[i]->bounds.contains(box)) {
				return i;
			}
		}
		return -1;
	}

	void insert(const Entity& e, std::unordered_map<Entity, QuadtreeNode*>& entityNodeMap) {
		AABB box = e.Get<AABB>();

		if (!isLeaf()) {
			int idx = getChildIndex(box);
			if (idx != -1) {
				children[idx]->insert(e, entityNodeMap);
				return;
			}
		}

		objects.push_back(e);
		entityNodeMap[e] = this;

		if (isLeaf() && objects.size() > maxObjects && level < maxLevels) {
			subdivide();

			for (auto it = objects.begin(); it != objects.end();) {
				int idx = getChildIndex(it->Get<AABB>());
				if (idx != -1) {
					children[idx]->insert(*it, entityNodeMap);
					entityNodeMap.erase(*it);
					it = objects.erase(it);
				} else {
					++it;
				}
			}
		}
	}

	bool remove(const Entity& e, std::unordered_map<Entity, QuadtreeNode*>& entityNodeMap) {
		for (auto it = objects.begin(); it != objects.end(); ++it) {
			if (*it == e) {
				objects.erase(it);
				entityNodeMap.erase(e);
				return true;
			}
		}

		if (!isLeaf()) {
			for (auto c : children) {
				if (c->remove(e, entityNodeMap)) {
					return true;
				}
			}
		}
		return false;
	}

	void update(const Entity& e, std::unordered_map<Entity, QuadtreeNode*>& entityNodeMap) {
		AABB box				  = e.Get<AABB>();
		QuadtreeNode* currentNode = entityNodeMap[e];

		if (currentNode && currentNode->bounds.contains(box)) {
			// Just update the object in place (no stored AABB so nothing needed)
			// No action needed here since we pull AABB fresh on queries.
		} else {
			// Remove and re-insert to correct node
			if (currentNode) {
				currentNode->remove(e, entityNodeMap);
			}
			insert(e, entityNodeMap);
		}
	}

	void retrieve(const AABB& box, std::vector<Entity>& candidates) const {
		for (const auto& e : objects) {
			if (e.Get<AABB>().intersects(box)) {
				candidates.push_back(e);
			}
		}

		if (!isLeaf()) {
			for (auto c : children) {
				if (c->bounds.intersects(box)) {
					c->retrieve(box, candidates);
				}
			}
		}
	}
};

class Quadtree {
	QuadtreeNode* root;
	std::unordered_map<Entity, QuadtreeNode*> entityNodeMap;

public:
	Quadtree(const AABB& bounds) {
		root = new QuadtreeNode(0, bounds);
	}

	~Quadtree() {
		delete root;
	}

	void insert(const Entity& e) {
		root->insert(e, entityNodeMap);
	}

	void remove(const Entity& e) {
		if (entityNodeMap.find(e) != entityNodeMap.end()) {
			QuadtreeNode* node = entityNodeMap[e];
			node->remove(e, entityNodeMap);
		}
	}

	void update(const Entity& e) {
		if (entityNodeMap.find(e) != entityNodeMap.end()) {
			entityNodeMap[e]->update(e, entityNodeMap);
		} else {
			insert(e);
		}
	}

	std::vector<Entity> retrieve(const AABB& box) const {
		std::vector<Entity> candidates;
		root->retrieve(box, candidates);
		return candidates;
	}
};

void SpawnEnemies(
	Quadtree& tree, size_t count,
	RNG<float>& positionRNGX, // e.g., RNG<float> positionRNG{ 0.0f, 800.0f };
	RNG<float>& positionRNGY, // e.g., RNG<float> positionRNG{ 0.0f, 800.0f };
	RNG<float>& sizeRNG,	  // e.g., RNG<float> sizeRNG{ 10.0f, 40.0f };
	Scene& scene			  // Interface to create entities
) {
	for (size_t i = 0; i < count; ++i) {
		float width	 = sizeRNG();
		float height = sizeRNG();

		float x = positionRNGX();
		float y = positionRNGY();

		V2_float min = { x, y };
		V2_float max = { x + width, y + height };

		Entity enemy = scene.CreateEntity(); // Your ECS entity creation
		AABB box{ min, max };
		enemy.Add<AABB>(box);

		tree.insert(enemy);
	}
}

bool Overlaps(const AABB& a, const AABB& b) {
	return (a.min.x <= b.max.x && a.max.x >= b.min.x) && (a.min.y <= b.max.y && a.max.y >= b.min.y);
}

#define QUADTREE 1

struct QuadTreeScene : public Scene {
	Quadtree tree{ AABB{ V2_float{ 0, 0 }, V2_float{ 1000, 1000 } } };

	Entity player;
	V2_float playerSize{ 20, 20 };

	RNG<float> positionRNGX{ 0.0f, (float)window_size.x };
	RNG<float> positionRNGY{ 0.0f, (float)window_size.y };
	RNG<float> sizeRNG{ 5.0f, 30.0f };

	AABB computePlayerAABBFromPosition(Entity p) {
		auto pos = p.GetPosition();
		return AABB{ pos - playerSize / 2.0f, pos + playerSize / 2.0f };
	}

	void Enter() {
		player = CreateEntity();
		player.Add<Transform>();
		player.Add<AABB>(computePlayerAABBFromPosition(player));

		tree.insert(player);

		SpawnEnemies(tree, 100000, positionRNGX, positionRNGY, sizeRNG, *this);
	}

	void Update() {
		MoveWASD(player.Get<Transform>().position, V2_float{ 100.0f } * game.dt(), false);
		// Update player's AABB component before updating the tree
		player.Get<AABB>() = computePlayerAABBFromPosition(player);

#ifdef QUADTREE
		// Update player's position in the quadtree (will re-insert if needed)
		tree.update(player);

		auto candidates = tree.retrieve(player.Get<AABB>());
#endif

		for (auto [e, aabb] : EntitiesWith<AABB>()) {
#ifdef QUADTREE
			if (e != player &&
				std::find(candidates.begin(), candidates.end(), e) != candidates.end() &&
				Overlaps(player.Get<AABB>(), aabb)) {
				// DrawDebugRect((aabb.min + aabb.max) / 2.0f, aabb.max - aabb.min, color::Red);
			} else {
				// DrawDebugRect((aabb.min + aabb.max) / 2.0f, aabb.max - aabb.min, color::Green);
			}
#else
			if (e == player) {
				continue;
			} else if (Overlaps(player.Get<AABB>(), aabb)) {
				// DrawDebugRect((aabb.min + aabb.max) / 2.0f, aabb.max - aabb.min, color::Red);
			} else {
				// DrawDebugRect((aabb.min + aabb.max) / 2.0f, aabb.max - aabb.min, color::Green);
			}
#endif
			// DrawDebugRect((aabb.min + aabb.max) / 2.0f, aabb.max - aabb.min, color::Green);
		}

		// DrawDebugRect(player.GetPosition(), playerSize, color::Purple);
	}
};

namespace ptgn {

namespace impl {

class RenderDataThing;

class RenderCommand {
public:
	virtual ~RenderCommand()					 = default;
	virtual void Execute(RenderDataThing& queue) = 0;
};

class FlushCall : public RenderCommand {
public:
	void Execute(RenderDataThing& queue) final;
};

class BlendModeBind : public RenderCommand {
public:
	BlendModeBind() = delete;

	BlendModeBind(BlendMode blend_mode) : blend_mode_{ blend_mode } {}

	void Execute(RenderDataThing& queue) final {
		GLRenderer::SetBlendMode(blend_mode_);
	}

private:
	BlendMode blend_mode_{ BlendMode::None };
};

class ViewportBind : public RenderCommand {
public:
	ViewportBind() = delete;

	ViewportBind(const V2_float& position, const V2_float& size) :
		position_{ position }, size_{ size } {}

	void Execute(RenderDataThing& queue) final {
		GLRenderer::SetViewport(position_, size_);
	}

private:
	V2_float position_;
	V2_float size_;
};

class ShaderBind : public RenderCommand {
public:
	ShaderBind() = delete;

	ShaderBind(const Shader* shader) : shader_{ shader } {}

	void Execute(RenderDataThing& queue) final {
		PTGN_ASSERT(shader_ != nullptr);
		shader_->Bind();
	}

private:
	const Shader* shader_{ nullptr };
};

class UniformBind : public RenderCommand {
public:
	UniformBind() = delete;

	UniformBind(const Shader* shader, const std::function<void(const Shader&)>& callback) :
		shader_{ shader }, callback_{ callback } {}

	void Execute(RenderDataThing& queue) final {
		PTGN_ASSERT(shader_ != nullptr);
		PTGN_ASSERT(callback_ != nullptr);
		shader_->Bind();
		std::invoke(callback_, *shader_);
	}

private:
	const Shader* shader_{ nullptr };
	std::function<void(const Shader&)> callback_;
};

using Index = std::uint32_t;

/*

Things that trigger Batch flush:

Frame Buffer / Render Target Bind
Shader Bind
Blend Mode Change
Uniform Change
Viewport Change
Camera Change -> Uniform Change
view_projection_dirty
shader_dirty (uniform has changed so flush previous batch)

TODO: Think of what a typical Quad batch looks like, then consider what happens when a Circle batch
is added. Then consider what happens if a custom render target shader is used such as with lighting.

Hmm: ?

Frame Buffer / Render Target Clear.
Shader Uniform Set.

const Shader* shader_{ nullptr };
BlendMode blend_mode_{ BlendMode::None };
Camera camera_;
std::function<void(const Shader& shader)> uniform_callback_;
bool view_projection_dirty_{ true };

*/

struct Batch {
	std::vector<Vertex> vertices;
	std::vector<Index> indices;
	std::vector<TextureId> textures;
	Index index_offset{ 0 };
};

struct Batches {
	std::vector<Batch> batches;
};

constexpr std::array<V2_float, 4> default_texture_coordinates{
	V2_float{ 0.0f, 0.0f }, V2_float{ 1.0f, 0.0f }, V2_float{ 1.0f, 1.0f }, V2_float{ 0.0f, 1.0f }
};

constexpr inline const BufferLayout<glsl::vec3, glsl::vec4, glsl::vec2, glsl::float_>
	quad_vertex_layout;

constexpr std::size_t batch_capacity{ 4000 };
constexpr std::size_t vertex_capacity{ batch_capacity * 4 };
constexpr std::size_t index_capacity{ batch_capacity * 6 };

class RenderQueue {
public:
};

class RenderState {
public:
	RenderState() = default;

	RenderState(
		const RenderTarget& render_target, const Shader* shader, BlendMode blend_mode,
		const Camera& camera, const std::function<void(const Shader& shader)>& uniform_callback = {}
	) :
		render_target_{ render_target },
		shader_{ shader },
		blend_mode_{ blend_mode },
		camera_{ camera },
		uniform_callback_{ uniform_callback } {}

	friend bool operator==(const RenderState& a, const RenderState& b) {
		return a.shader_ == b.shader_ && a.camera_ == b.camera_ &&
			   a.render_target_ == b.render_target_ && a.blend_mode_ == b.blend_mode_ &&
			   a.view_projection_dirty_ == b.view_projection_dirty_;
	}

	friend bool operator!=(const RenderState& a, const RenderState& b) {
		return !(a == b);
	}

private:
	RenderTarget render_target_;
	const Shader* shader_{ nullptr };
	BlendMode blend_mode_{ BlendMode::None };
	Camera camera_;
	std::function<void(const Shader& shader)> uniform_callback_;
	bool view_projection_dirty_{ true };
};

class RenderDataThing {
public:
	void Init() {
		max_texture_slots = GLRenderer::GetMaxTextureSlots();

		const auto& quad_shader{ game.shader.Get<ShapeShader::Quad>() };

		PTGN_ASSERT(quad_shader.IsValid());
		PTGN_ASSERT(game.shader.Get<ShapeShader::Circle>().IsValid());
		PTGN_ASSERT(game.shader.Get<ScreenShader::Default>().IsValid());
		PTGN_ASSERT(game.shader.Get<OtherShader::Light>().IsValid());

		std::vector<std::int32_t> samplers(max_texture_slots);
		std::iota(samplers.begin(), samplers.end(), 0);

		quad_shader.Bind();
		quad_shader.SetUniform(
			"u_Texture", samplers.data(), static_cast<std::int32_t>(samplers.size())
		);

		IndexBuffer quad_ib{ nullptr, index_capacity, static_cast<std::uint32_t>(sizeof(Index)),
							 BufferUsage::DynamicDraw };
		VertexBuffer quad_vb{ nullptr, vertex_capacity, static_cast<std::uint32_t>(sizeof(Vertex)),
							  BufferUsage::DynamicDraw };

		triangle_vao = VertexArray(
			PrimitiveMode::Triangles, std::move(quad_vb), quad_vertex_layout, std::move(quad_ib)
		);

		white_texture = Texture(static_cast<const void*>(&color::White), { 1, 1 });

#ifdef PTGN_PLATFORM_MACOS
		// Prevents MacOS warning: "UNSUPPORTED (log once): POSSIBLE ISSUE: unit X
		// GLD_TEXTURE_INDEX_2D is unloadable and bound to sampler type (Float) - using zero
		// texture because texture unloadable."
		for (std::uint32_t slot{ 0 }; slot < max_texture_slots; slot++) {
			Texture::Bind(white_texture.GetId(), slot);
		}
#endif
	}

	void BindShader(const Shader* shader) {
		PTGN_ASSERT(shader != nullptr);
		if (render_states.empty()) {
			auto& state	 = render_states.emplace_back(*this);
			state.shader = shader;
		}
		auto render_state{ &render_states.back() };
		if (render_state->shader != shader) {
			auto& state	 = render_states.emplace_back(*this);
			state.shader = shader;
		}
		render_state = &render_states.back();
		PTGN_ASSERT(render_state->shader == shader);
		render_state->commands.emplace_back(std::make_unique<ShaderBind>(shader));
	}

	void SetUniform(
		const Shader* shader, const std::function<void(const Shader& shader)>& callback
	) {
		PTGN_ASSERT(shader != nullptr);
		commands.emplace_back(std::make_unique<UniformBind>(shader, callback));
	}

	void SetBlendMode(BlendMode blend_mode) {
		// TODO: Check if blend_mode is bound, if it is, return early.
		commands.emplace_back(std::make_unique<BlendModeBind>(blend_mode));
	}

	void SetViewport(const V2_float& position, const V2_float& size) {
		// TODO: Check if viewport is bound, if it is, return early.
		commands.emplace_back(std::make_unique<ViewportBind>(position, size));
	}

	void Draw() {
		commands.emplace_back(std::make_unique<FlushCall>());
	}

	// TODO: Add texture bind.
	// TODO: Add frame buffer bind.
	// TODO: Add frame buffer clear.

	template <typename T>
	void AddVertices(const T& vertices) {
		PTGN_ASSERT(vertices.size() >= 1);

		Depth depth{ static_cast<std::int32_t>(vertices[0].position[2]) };

		auto [it, _] = depths.try_emplace(depth);

		auto& batches{ it->second.batches };

		if (batches.empty()) {
			batches.emplace_back();
		}

		Batch* batch{ &batches.back() };

		if (batch->vertices.size() + vertices.size() > vertex_capacity) {
			batch = &batches.emplace_back();
		}

		std::copy(vertices.begin(), vertices.end(), std::back_inserter(batch->vertices));

		if constexpr (std::is_same_v<T, std::array<Vertex, 3>>) {
			batch->indices.push_back(batch->index_offset + 0);
			batch->indices.push_back(batch->index_offset + 1);
			batch->indices.push_back(batch->index_offset + 2);
			batch->index_offset += 3;
		} else if constexpr (std::is_same_v<T, std::array<Vertex, 4>>) {
			batch->indices.push_back(batch->index_offset + 0);
			batch->indices.push_back(batch->index_offset + 1);
			batch->indices.push_back(batch->index_offset + 2);
			batch->indices.push_back(batch->index_offset + 2);
			batch->indices.push_back(batch->index_offset + 3);
			batch->indices.push_back(batch->index_offset + 0);
			batch->index_offset += 4;
		} else {
			static_assert(false, "Should not be here");
			PTGN_ERROR("Error type");
		}
	}

	Batch& GetBatch(const Depth& depth, std::size_t batch_index) {
		auto it{ depths.find(depth) };
		PTGN_ASSERT(it != depths.end());
		auto& batches{ it->second.batches };
		PTGN_ASSERT(batch_index < batches.size());
		return batches[batch_index];
	}

	void Clear() {
		depths.clear();
		render_states.clear();
	}

	void Flush() {
		for (auto& state : render_states) {
			state.Flush();
		}
	}

	std::size_t max_texture_slots{ 0 };
	Texture white_texture;
	VertexArray triangle_vao;

	std::map<Depth, Batches> depths;

	std::vector<RenderState> render_states;
};

void FlushCall::Execute(RenderDataThing& queue) {
	for (auto& [depth, batches] : queue.depths) {
		for (auto& batch : batches.batches) {
			queue.triangle_vao.Bind();

			queue.triangle_vao.GetVertexBuffer().SetSubData(
				batch.vertices.data(), 0, static_cast<std::uint32_t>(batch.vertices.size()),
				sizeof(Vertex), false
			);

			queue.triangle_vao.GetIndexBuffer().SetSubData(
				batch.indices.data(), 0, static_cast<std::uint32_t>(batch.indices.size()),
				sizeof(Index), false
			);

			GLRenderer::DrawElements(queue.triangle_vao, batch.indices.size(), false);
		}
	}
}

[[nodiscard]] static std::array<Vertex, 4> GetQuadVertices(
	const std::array<V2_float, 4>& quad_points, const Color& color, const Depth& depth
) {
	std::array<Vertex, 4> vertices{};

	V4_float c{ color.Normalized() };

	PTGN_ASSERT(vertices.size() == default_texture_coordinates.size());

	for (std::size_t i{ 0 }; i < vertices.size(); ++i) {
		vertices[i].position  = { quad_points[i].x, quad_points[i].y, static_cast<float>(depth) };
		vertices[i].color	  = { c.x, c.y, c.z, c.w };
		vertices[i].tex_coord = { default_texture_coordinates[i].x,
								  default_texture_coordinates[i].y };
		vertices[i].tex_index = { 0.0f };
	}

	return vertices;
}

} // namespace impl

} // namespace ptgn

struct RenderDataThingScene : public Scene {
	std::array<V2_float, 4> points{ V2_float{ 50.0f, 50.0f }, V2_float{ 200.0f, 50.0f },
									V2_float{ 200.0f, 200.0f }, V2_float{ 50.0f, 200.0f } };

	std::array<impl::Vertex, 4> vertices;

	impl::RenderDataThing queue;

	void Enter() {
		vertices = impl::GetQuadVertices(points, color::Red, Depth{ 1 });

		queue.Init();

		queue.Clear();

		queue.SetViewport({}, window_size);
		queue.SetBlendMode(BlendMode::Blend);
		queue.SetUniform(
			&game.shader.Get<ShapeShader::Quad>(), [cam = camera.primary](const Shader& shader
												   ) { shader.SetUniform("u_ViewProjection", cam); }
		);

		queue.Flush();
	}

	void Render() {
		queue.Clear();

		queue.white_texture.Bind(0);

		queue.BindShader(&game.shader.Get<ShapeShader::Quad>());
		queue.AddVertices(vertices);
		// TODO: Current approach requires order of data addition to be in depth order. i.e.
		// furthest objects from camera first.
		queue.Draw();

		queue.Flush();
	}
};

/*

struct Box;

struct EndPoint {
	float value{ 0.0f };
	bool isMin{ false };
	Box* box{ nullptr };
	EndPoint(float value_, bool isMin_);
};

struct AABB;

struct Box {
	std::array<EndPoint*, 2> minEndPoints;
	std::array<EndPoint*, 2> maxEndPoints;
	AABB* userData{ nullptr };

	Box(EndPoint* minX, EndPoint* minY, EndPoint* maxX, EndPoint* maxY);

	bool overlaps(Box* box) const {
		float l1X = minEndPoints[0]->value;
		float u1X = maxEndPoints[0]->value;
		float l1Y = minEndPoints[1]->value;
		float u1Y = maxEndPoints[1]->value;
		float l2X = box->minEndPoints[0]->value;
		float u2X = box->maxEndPoints[0]->value;
		float l2Y = box->minEndPoints[1]->value;
		float u2Y = box->maxEndPoints[1]->value;
		return !(l2X > u1X || u2X < l1X || u2Y < l1Y || l2Y > u1Y);
	}
};

EndPoint::EndPoint(float value_, bool isMin_) {
	box	  = nullptr;
	value = value_;
	isMin = isMin_;
}

Box::Box(EndPoint* minX, EndPoint* minY, EndPoint* maxX, EndPoint* maxY) {
	minEndPoints = { minX, minY };
	maxEndPoints = { maxX, maxY };
	userData	 = nullptr;
}

struct AABB {
	V2_float min;
	V2_float max;
	V2_float velocity;
	std::int64_t index = -1;
	Box* sapBox{ nullptr };

	AABB(V2_float min_, V2_float max_) : min(min_), max(max_), velocity(randomVelocity()) {}

private:
	static V2_float randomVelocity() {
		V2_float dir{ V2_float::Random(-0.5f, 0.5f) };
		float speed = 60.0f;

		if (dir.x != 0 || dir.y != 0) {
			return dir.Normalized() * speed;
		} else {
			return V2_float{ speed, 0.0f };
		}
	}
};

class SweepAndPrune {
public:
	std::vector<std::unique_ptr<Box>> boxes;
	std::array<std::vector<EndPoint*>, 2> endPoints;

	std::function<void(Box*, Box*)> onAdd;
	std::function<void(Box*, Box*)> onRemove;

	SweepAndPrune() {
		boxes.clear();
		endPoints = {};
		onAdd	  = [](Box*, Box*) {
		};
		onRemove = [](Box*, Box*) {
		};
	};

	Box* addObject(const V2_float& v0, const V2_float& v1, AABB* userData) {
		auto minX = new EndPoint(v0.x, true);
		auto maxX = new EndPoint(v1.x, false);
		auto minY = new EndPoint(v0.y, true);
		auto maxY = new EndPoint(v1.y, false);

		auto box	  = std::make_unique<Box>(minX, minY, maxX, maxY);
		box->userData = userData;
		minX->box = maxX->box = minY->box = maxY->box = box.get();

		Box* boxPtr = box.get();
		boxes.push_back(std::move(box));

		insertSorted(endPoints[0], minX, true);
		insertSorted(endPoints[0], maxX, false);
		endPoints[1].push_back(minY);
		endPoints[1].push_back(maxY);

		for (int axis = 0; axis < 2; ++axis) {
			sortFull(endPoints[axis]);
		}

		return boxPtr;
	}

	void updateObject(Box* box, V2_float v0, V2_float v1) {
		float newPos[2][2] = { { v0.x, v1.x }, { v0.y, v1.y } };
		for (int axis = 0; axis < 2; ++axis) {
			auto& axisVec = endPoints[axis];

			box->minEndPoints[axis]->value = newPos[axis][0];
			sortMinDown(axisVec, indexOf(axisVec, box->minEndPoints[axis]));

			box->maxEndPoints[axis]->value = newPos[axis][1];
			sortMaxUp(axisVec, indexOf(axisVec, box->maxEndPoints[axis]));

			sortMinUp(axisVec, indexOf(axisVec, box->minEndPoints[axis]));
			sortMaxDown(axisVec, indexOf(axisVec, box->maxEndPoints[axis]));
		}
	}

	void removeObject(Box* box) {
		box->minEndPoints[1]->value = std::numeric_limits<float>::max() - 1;
		box->maxEndPoints[1]->value = std::numeric_limits<float>::max();
		sortFull(endPoints[1]);

		boxes.erase(
			std::remove_if(
				boxes.begin(), boxes.end(),
				[box](const std::unique_ptr<Box>& b) { return b.get() == box; }
			),
			boxes.end()
		);

		for (int axis = 0; axis < 2; ++axis) {
			auto& axisVec = endPoints[axis];
			remove(axisVec, box->minEndPoints[axis]);
			remove(axisVec, box->maxEndPoints[axis]);
		}
	}

private:
	void insertSorted(std::vector<EndPoint*>& vec, EndPoint* ep, bool isMin) {
		auto it = vec.begin();
		while (it != vec.end() && (*it)->value < ep->value) {
			++it;
		}
		vec.insert(it, ep);
	}

	int indexOf(const std::vector<EndPoint*>& vec, EndPoint* value) {
		auto it = std::find(vec.begin(), vec.end(), value);
		return (it != vec.end()) ? (int)std::distance(vec.begin(), it) : -1;
	}

	void remove(std::vector<EndPoint*>& vec, EndPoint* value) {
		vec.erase(std::remove(vec.begin(), vec.end(), value), vec.end());
	}

	void sortFull(std::vector<EndPoint*>& axis) {
		for (auto j = 1; j < axis.size(); ++j) {
			EndPoint* keyElement = axis[j];
			float key			 = keyElement->value;
			int i				 = j - 1;
			while (i >= 0 && axis[i]->value > key) {
				EndPoint* swapper = axis[i];
				if (keyElement->isMin && !swapper->isMin &&
					swapper->box->overlaps(keyElement->box)) {
					onAdd(swapper->box, keyElement->box);
				} else if (!keyElement->isMin && swapper->isMin) {
					onRemove(swapper->box, keyElement->box);
				}
				axis[i + 1] = swapper;
				--i;
			}
			axis[i + 1] = keyElement;
		}
	}

	void sortMinDown(std::vector<EndPoint*>& axis, int j) {
		auto keyElement = axis[j];
		float key		= keyElement->value;
		int i			= j - 1;
		while (i >= 0 && axis[i]->value > key) {
			auto swapper = axis[i];
			if (keyElement->isMin && !swapper->isMin && swapper->box->overlaps(keyElement->box)) {
				onAdd(swapper->box, keyElement->box);
			}
			axis[i + 1] = swapper;
			--i;
		}
		axis[i + 1] = keyElement;
	}

	void sortMinUp(std::vector<EndPoint*>& axis, int j) {
		auto keyElement = axis[j];
		float key		= keyElement->value;
		int i			= j + 1;
		while (i < static_cast<int>(axis.size()) && axis[i]->value < key) {
			auto swapper = axis[i];
			if (keyElement->isMin && !swapper->isMin) {
				onRemove(swapper->box, keyElement->box);
			}
			axis[i - 1] = swapper;
			++i;
		}
		axis[i - 1] = keyElement;
	}

	void sortMaxDown(std::vector<EndPoint*>& axis, int j) {
		auto keyElement = axis[j];
		float key		= keyElement->value;
		int i			= j - 1;
		while (i >= 0 && axis[i]->value > key) {
			auto swapper = axis[i];
			if (!keyElement->isMin && swapper->isMin) {
				onRemove(swapper->box, keyElement->box);
			}
			axis[i + 1] = swapper;
			--i;
		}
		axis[i + 1] = keyElement;
	}

	void sortMaxUp(std::vector<EndPoint*>& axis, int j) {
		auto keyElement = axis[j];
		float key		= keyElement->value;
		int i			= j + 1;
		while (i < static_cast<int>(axis.size()) && axis[i]->value < key) {
			auto swapper = axis[i];
			if (!keyElement->isMin && swapper->isMin && swapper->box->overlaps(keyElement->box)) {
				onAdd(swapper->box, keyElement->box);
			}
			axis[i - 1] = swapper;
			++i;
		}
		axis[i - 1] = keyElement;
	}
};

void moveAABBs(
	std::vector<std::unique_ptr<AABB>>& aabbs, float deltaTimeSeconds, float canvasWidth,
	float movingPercent
) {
	auto movingCount = static_cast<int>(aabbs.size() * movingPercent / 100.0f);
	float boundary	 = canvasWidth;

	for (auto i = 0; i < movingCount; ++i) {
		auto aabb	   = aabbs[i].get();
		V2_float delta = aabb->velocity * deltaTimeSeconds;

		aabb->min = aabb->min + delta;
		aabb->max = aabb->max + delta;

		for (int dim = 0; dim < 2; ++dim) {
			float minVal = (dim == 0 ? aabb->min.x : aabb->min.y);
			float maxVal = (dim == 0 ? aabb->max.x : aabb->max.y);

			if (minVal < 0.0f || maxVal > boundary) {
				if (dim == 0) {
					aabb->velocity.x *= -1.0f;
				} else {
					aabb->velocity.y *= -1.0f;
				}
			}
		}
	}
}

void updateSAP(std::vector<std::unique_ptr<AABB>>& aabbs, SweepAndPrune& sap, float movingPercent) {
	auto movingCount = static_cast<int>(aabbs.size() * movingPercent / 100.0f);

	for (auto i = 0; i < movingCount; ++i) {
		auto aabb = aabbs[i].get();
		sap.updateObject(aabb->sapBox, aabb->min, aabb->max);
	}
}

void addAABB(
	std::vector<std::unique_ptr<AABB>>& aabbs, SweepAndPrune& sap, float size, float canvasWidth
) {
	auto getRandomPosition = [size, canvasWidth]() -> float {
		static std::random_device rd;
		static std::mt19937 gen(rd());
		std::uniform_real_distribution<float> dist(size, canvasWidth - size);
		return dist(gen);
	};

	float x0 = getRandomPosition() - size;
	float y0 = getRandomPosition() - size;
	float x1 = x0 + size;
	float y1 = y0 + size;

	auto aabb	= std::make_unique<AABB>(V2_float{ x0, y0 }, V2_float{ x1, y1 });
	aabb->index = static_cast<int>(aabbs.size());

	// Register with SAP
	aabb->sapBox = sap.addObject(aabb->min, aabb->max, aabb.get());

	aabbs.push_back(std::move(aabb));
}

void removeAABB(std::vector<std::unique_ptr<AABB>>& aabbs, SweepAndPrune& sap) {
	auto& aabb = aabbs[aabbs.size() - 1];
	sap.removeObject(aabb->sapBox);
	aabbs.pop_back();
}

class SceneTest : public Scene {
public:
	float movingPercent = 50.0f;

	float size = 20.0f;

	std::unordered_set<std::int64_t> pairs;

	std::vector<std::unique_ptr<AABB>> aabbs;

	SweepAndPrune sap;

	void Enter() {
		sap.onAdd = [&](Box* boxA, Box* boxB) {
			auto i = boxA->userData->index;
			auto j = boxB->userData->index;
			PTGN_ASSERT(i != j);
			if (i > j) {
				auto tmp = j;
				j		 = i;
				i		 = tmp;
			}
			pairs.insert((i << 16) | j);
		};
		sap.onRemove = [&](Box* boxA, Box* boxB) {
			auto i = boxA->userData->index;
			auto j = boxB->userData->index;
			if (i > j) {
				auto tmp = j;
				j		 = i;
				i		 = tmp;
			}
			pairs.erase((i << 16) | j);
		};
		for (auto i = 0; i < 128; i++) {
			addAABB(aabbs, sap, size, (float)window_size.x);
		}
	}

	void Update() {
		moveAABBs(aabbs, game.dt(), (float)window_size.x, movingPercent);
		updateSAP(aabbs, sap, movingPercent);

		for (auto& aabb : aabbs) {
			DrawDebugRect(aabb->min, aabb->max - aabb->min, color::Green, Origin::TopLeft, 1.0f);
		}
		for (auto& key : pairs) {
			auto i		= (key >> 16) & 0xffff;
			auto j		= key & 0xffff;
			auto& aabbi = aabbs[i];
			auto& aabbj = aabbs[j];
			DrawDebugLine(
				(aabbi->min + aabbi->max) / 2.0f, (aabbj->min + aabbj->max) / 2.0f, color::DarkRed,
				1.0f
			);
		}
	}
};

*/

/*
constexpr float camera_zoom{ 4.0f };
constexpr Color bg_color{ 126, 206, 120, 255 };

constexpr V2_int grid_size{ 30, 30 };
constexpr V2_int tile_size{ 15, 15 };
constexpr V2_int world_size{ grid_size * tile_size };

class Inventory {
public:
	struct OnPickup : public Script<OnPickup> {
		OnPickup() = default;

		OnPickup(Entity o) : other{ o } {}

		Entity other;

		void OnCollisionStart(Collision collision) override {
			if (collision.entity == other) {
				other.SetTint(color::Red);
			}
		}

		void OnCollisionStop(Collision collision) override {
			if (collision.entity == other) {
				other.SetTint(color::White);
			}
		}
	};

	static void MakePickupable(Entity player, Entity entity) {
		entity.Add<BoxCollider>(Sprite{ entity }.GetDisplaySize()).SetOverlapOnly();
		entity.Enable();
		// player.GetChild("interaction").AddScript<OnPickup>(entity);
	}
};

class GameScene : public Scene {
public:
	GameScene() {
		LoadResources("resources/data/resources.json");
	}

	FractalNoise fractal_noise;

	Grid<Entity> flowers{ grid_size };

	Entity flower;
	Entity player;

	Entity CreateFlower(const V2_int& tile, const V2_int& position, const TextureHandle& key) {
		Sprite s{ CreateSprite(*this, key) };
		s.SetPosition(position);
		Inventory::MakePickupable(player, s);
		return s;
	}

	void Enter() final {
		camera.window.SetZoom(camera_zoom);
		camera.window.SetPosition(window_size / 2.0f / camera_zoom);

		camera.primary.SetZoom(camera_zoom);
		camera.primary.SetBounds({ 0, 0 }, world_size);
		physics.SetBounds({ 0, 0 }, world_size);
		// SetColliderVisibility(true);

		fractal_noise.SetOctaves(2);
		fractal_noise.SetFrequency(0.055f);
		fractal_noise.SetLacunarity(5);
		fractal_noise.SetPersistence(3);

		RNG<int> flower_type_rng{ 0, 9 };
		RNG<int> flower_rng{ 0, 1 };

		player = CreateTopDownPlayer(*this, world_size / 2.0f);

		flowers.ForEachCoordinate([&](auto coordinate) {
			if (flower_rng()) {
				flowers.Set(
					coordinate, CreateFlower(
									coordinate, coordinate * tile_size + tile_size / 2,
									"flower_" + std::to_string(flower_type_rng())
								)
				);
			}
		});

		camera.primary.StartFollow(player);
	}
};
*/

/*
class MainMenu : public Scene {
public:
	Button play;

	MainMenu() {}

	void Enter() override {}

	void Update() override {}
};
*/

int main([[maybe_unused]] int c, [[maybe_unused]] char** v) {
	game.Init("Test Jam", window_size);
	game.scene.Enter<RenderDataThingScene>("game");
	return 0;
}