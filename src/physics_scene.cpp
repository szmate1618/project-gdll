#include "physics_scene.hpp"
#include "box_layer.hpp"
#include "collision_world.hpp"
#include "physics_world.hpp"

#include <box3d/box3d.h>
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace viewer {
namespace {
constexpr float cellSize = 16;
struct StaticBody {
    b3BodyId body{};
    b3MeshData* mesh = nullptr;
    ~StaticBody() {
        if (b3Body_IsValid(body)) b3DestroyBody(body);
        if (mesh) b3DestroyMesh(mesh);
    }
};
} // namespace
struct PhysicsScene::Impl {
    std::shared_ptr<PhysicsWorld> simulation;
    const CollisionWorld* collision;
    glm::vec3 minimum, maximum;
    std::set<std::pair<int, int>> cells;
    std::set<std::size_t> triangles;
    std::vector<std::unique_ptr<StaticBody>> bodies;
};
PhysicsScene::PhysicsScene(std::shared_ptr<PhysicsWorld> simulation, const CollisionWorld& collision,
                           glm::vec3 minimum, glm::vec3 maximum) : impl_(std::make_unique<Impl>()) {
    if (!simulation) throw std::invalid_argument("Scene collision requires a physics world");
    impl_->simulation = std::move(simulation);
    impl_->collision = &collision;
    impl_->minimum = minimum; impl_->maximum = maximum;
}
PhysicsScene::~PhysicsScene() = default;
void PhysicsScene::ensureNear(glm::vec3 minimum, glm::vec3 maximum) {
    minimum = glm::max(minimum, impl_->minimum);
    maximum = glm::min(maximum, impl_->maximum);
    if (glm::any(glm::greaterThan(minimum, maximum))) return;
    const int left = static_cast<int>(std::floor(minimum.x / cellSize));
    const int right = static_cast<int>(std::floor(maximum.x / cellSize));
    const int first = static_cast<int>(std::floor(minimum.z / cellSize));
    const int last = static_cast<int>(std::floor(maximum.z / cellSize));
    for (int x = left; x <= right; ++x) for (int z = first; z <= last; ++z) {
        if (!impl_->cells.emplace(x, z).second) continue;
        const auto faces = impl_->collision->staticFaces({x * cellSize, impl_->minimum.y - 1, z * cellSize},
            {(x + 1) * cellSize, impl_->maximum.y + 1, (z + 1) * cellSize});
        const glm::vec3 origin(x * cellSize, 0, z * cellSize);
        std::vector<b3Vec3> vertices;
        std::vector<int32_t> indices;
        for (const auto& face : faces) {
            if (!impl_->triangles.insert(face.index).second) continue;
            for (const auto p : face.vertices) {
                indices.push_back(static_cast<int32_t>(vertices.size()));
                const auto local = p - origin;
                vertices.push_back({local.x, local.y, local.z});
            }
        }
        if (indices.empty()) continue;
        // Mesh contacts are one-sided. Two separately built windings preserve
        // both indoor and outdoor collision without changing the scene winding.
        for (int side = 0; side < 2; ++side) {
            if (side) for (std::size_t i = 0; i < indices.size(); i += 3) std::swap(indices[i + 1], indices[i + 2]);
            auto mesh = std::make_unique<StaticBody>();
            b3MeshDef definition{};
            definition.vertices = vertices.data(); definition.indices = indices.data();
            definition.vertexCount = static_cast<int>(vertices.size());
            definition.triangleCount = static_cast<int>(indices.size() / 3);
            definition.weldVertices = true; definition.weldTolerance = .0001f;
            definition.identifyEdges = true;
            mesh->mesh = b3CreateMesh(&definition, nullptr, 0);
            if (!mesh->mesh) throw std::runtime_error("Cannot build scene physics mesh");
            auto body = b3DefaultBodyDef();
            body.position = {origin.x, origin.y, origin.z};
            mesh->body = b3CreateBody(impl_->simulation->id(), &body);
            auto shape = b3DefaultShapeDef();
            shape.filter.categoryBits = ordinaryBodyCategory;
            shape.baseMaterial.friction = .6f;
            // Door panels have finite collider thickness at a flush wall hinge.
            // Their existing hinge limits define the swing; importing the wall
            // as another door contact would jam them at the jamb. These meshes
            // support props, leaving door and ragdoll behavior unchanged.
            shape.filter.maskBits = interiorBoxCategory | grenadeBodyCategory | zombieContentsCategory;
            b3CreateMeshShape(mesh->body, &shape, mesh->mesh, {1, 1, 1});
            impl_->bodies.push_back(std::move(mesh));
        }
    }
}
void PhysicsScene::prepare(const BoxLayer& boxes, float padding) {
    for (std::size_t i = 0; i < boxes.count(); ++i) {
        const auto box = boxes.pose(i);
        // Cover the next frame and neighboring tiles before bodies reach them.
        const auto reach = glm::vec3(glm::length(box.halfExtents) + padding);
        const auto travel = boxes.velocity(i) * .2f;
        ensureNear(box.center + glm::min(travel, glm::vec3(0)) - reach,
                   box.center + glm::max(travel, glm::vec3(0)) + reach);
    }
}
} // namespace viewer
