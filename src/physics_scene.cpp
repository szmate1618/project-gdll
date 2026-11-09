#include "physics_scene.hpp"
#include "box_layer.hpp"
#include "collision_world.hpp"
#include "physics_world.hpp"
#include "physics_mesh_adjacency.hpp"

#include <box3d/box3d.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

namespace viewer {
namespace {
constexpr float cellSize = 16;
using Cell = std::pair<int, int>;

bool finite(glm::vec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
bool validBounds(glm::vec3 minimum, glm::vec3 maximum) {
    return finite(minimum) && finite(maximum) && !glm::any(glm::greaterThan(minimum, maximum));
}
struct CellRange { int left, right, first, last; };
bool cellIndex(double value, float size, int& result) {
    const double coordinate = std::floor(value / size);
    if (coordinate <= std::numeric_limits<int>::min() || coordinate >= std::numeric_limits<int>::max())
        return false;
    result = static_cast<int>(coordinate);
    return true;
}
bool horizontalRange(glm::vec3 minimum, glm::vec3 maximum, glm::vec3 sceneMinimum,
                     glm::vec3 sceneMaximum, float size, CellRange& result) {
    if (!validBounds(minimum, maximum)) return false;
    minimum.x = std::max(minimum.x, sceneMinimum.x);
    minimum.z = std::max(minimum.z, sceneMinimum.z);
    maximum.x = std::min(maximum.x, sceneMaximum.x);
    maximum.z = std::min(maximum.z, sceneMaximum.z);
    if (minimum.x > maximum.x || minimum.z > maximum.z) return false;
    // Divide in double so tiny negative coordinates stay on the negative
    // side of zero, and validate before narrowing to an integer.
    return cellIndex(minimum.x, size, result.left) && cellIndex(maximum.x, size, result.right) &&
        cellIndex(minimum.z, size, result.first) && cellIndex(maximum.z, size, result.last);
}
struct StaticBody {
    b3BodyId body{};
    b3MeshData* mesh = nullptr;
    ~StaticBody() {
        if (b3Body_IsValid(body)) b3DestroyBody(body);
        if (mesh) b3DestroyMesh(mesh);
    }
};
std::vector<std::unique_ptr<StaticBody>> meshBodies(const std::shared_ptr<PhysicsWorld>& simulation,
        std::vector<b3Vec3> vertices, std::vector<int32_t> indices, glm::vec3 origin, bool hard = true,
        const std::vector<StaticCollisionFace>* adjacencyContext = nullptr) {
    std::vector<std::unique_ptr<StaticBody>> result;
    if (indices.empty()) return result;
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
        if (adjacencyContext) applyMeshSourceAdjacency(mesh->mesh, *adjacencyContext, origin);
        auto body = b3DefaultBodyDef();
        body.position = {origin.x, origin.y, origin.z};
        mesh->body = b3CreateBody(simulation->id(), &body);
        if (!b3Body_IsValid(mesh->body)) throw std::runtime_error("Cannot create scene physics body");
        auto shape = b3DefaultShapeDef();
        shape.filter.categoryBits = ordinaryBodyCategory | (hard ? hardImpactCategory : 0);
        shape.baseMaterial.friction = .6f;
        // Door panels have finite collider thickness at a flush wall hinge.
        // Their existing hinge limits define the swing; importing the wall
        // as another door contact would jam them at the jamb. These meshes
        // support props, corpses, detached limbs and living physical attacks.
        // Door hinge behavior remains independent of these meshes.
        shape.filter.maskBits = interiorBoxCategory | grenadeBodyCategory | zombieContentsCategory | vehicleBodyCategory | playerBodyCategory | zombieAttackBodyCategory | detachedLimbCategory | ragdollBodyCategory;
        if (!b3Shape_IsValid(b3CreateMeshShape(mesh->body, &shape, mesh->mesh, {1, 1, 1})))
            throw std::runtime_error("Cannot create scene physics shape");
        result.push_back(std::move(mesh));
    }
    return result;
}
} // namespace
struct PhysicsScene::Impl {
    // Geometry keys do not depend on the runtime BVH's face order or indices.
    using RuntimeFace = std::array<float, 9>;
    struct RuntimeChunk {
        std::vector<RuntimeFace> faces, contextFaces;
        std::vector<std::unique_ptr<StaticBody>> bodies;
    };
    std::shared_ptr<PhysicsWorld> simulation;
    const CollisionWorld* collision;
    glm::vec3 minimum, maximum;
    std::set<Cell> cells, groundSections;
    std::set<std::size_t> triangles, groundTriangles, trunks;
    std::vector<std::unique_ptr<StaticBody>> bodies;
    std::map<Cell, RuntimeChunk> runtimeChunks;
    std::uint64_t runtimeRevision = 0;
    static RuntimeFace faceKey(const StaticCollisionFace& face) {
        RuntimeFace result;
        for (std::size_t i = 0; i < face.vertices.size(); ++i)
            for (std::size_t axis = 0; axis < 3; ++axis) result[i * 3 + axis] = face.vertices[i][axis];
        return result;
    }
    void syncRuntime() {
        if (runtimeRevision == collision->runtimeRevision()) return;
        std::map<Cell, std::vector<RuntimeFace>> geometry;
        const auto* layer = collision->runtimeGeometry();
        if (layer) {
            const auto faces = layer->staticFaces(glm::vec3(-1e8f), glm::vec3(1e8f));
            for (const auto& face : faces) {
                const auto center = (glm::dvec3(face.vertices[0]) + glm::dvec3(face.vertices[1]) +
                                     glm::dvec3(face.vertices[2])) / 3.0;
                Cell cell;
                if (!cellIndex(center.x, cellSize, cell.first) || !cellIndex(center.z, cellSize, cell.second))
                    throw std::runtime_error("Runtime scene exceeds the physics collision grid");
                // One centroid owner retains each whole triangle even when
                // the triangle extends across several runtime chunk borders.
                geometry[cell].push_back(faceKey(face));
            }
        }
        std::map<Cell, RuntimeChunk> replacements;
        for (auto& [cell, faces] : geometry) {
            std::sort(faces.begin(), faces.end());
            glm::vec3 minimum(std::numeric_limits<float>::max());
            glm::vec3 maximum(std::numeric_limits<float>::lowest());
            for (const auto& face : faces) for (std::size_t i = 0; i < 3; ++i) {
                const glm::vec3 point(face[i * 3], face[i * 3 + 1], face[i * 3 + 2]);
                minimum = glm::min(minimum, point);
                maximum = glm::max(maximum, point);
            }
            const glm::vec3 halo(.001f);
            const auto context = layer->staticFaces(minimum - halo, maximum + halo, false);
            std::vector<RuntimeFace> contextFaces;
            contextFaces.reserve(context.size());
            for (const auto& face : context) contextFaces.push_back(faceKey(face));
            std::sort(contextFaces.begin(), contextFaces.end());
            const auto old = runtimeChunks.find(cell);
            if (old != runtimeChunks.end() && old->second.faces == faces &&
                old->second.contextFaces == contextFaces) continue;
            std::vector<b3Vec3> vertices;
            std::vector<int32_t> indices;
            const glm::vec3 origin(cell.first * cellSize, 0, cell.second * cellSize);
            for (const auto& face : faces) for (std::size_t i = 0; i < 3; ++i) {
                const glm::vec3 point(face[i * 3], face[i * 3 + 1], face[i * 3 + 2]);
                const auto q = point - origin;
                indices.push_back(static_cast<int32_t>(vertices.size()));
                vertices.push_back({q.x, q.y, q.z});
            }
            auto meshes = meshBodies(simulation, std::move(vertices), std::move(indices), origin, true, &context);
            replacements.emplace(cell, RuntimeChunk{std::move(faces), std::move(contextFaces), std::move(meshes)});
        }
        // Build every changed chunk first. An allocation/mesh failure retains
        // all old support and leaves the revision pending for a later retry.
        for (auto chunk = runtimeChunks.begin(); chunk != runtimeChunks.end();) {
            if (!geometry.count(chunk->first) || replacements.count(chunk->first)) chunk = runtimeChunks.erase(chunk);
            else ++chunk;
        }
        runtimeChunks.merge(replacements);
        runtimeRevision = collision->runtimeRevision();
    }
};
PhysicsScene::PhysicsScene(std::shared_ptr<PhysicsWorld> simulation, const CollisionWorld& collision,
                           glm::vec3 minimum, glm::vec3 maximum) : impl_(std::make_unique<Impl>()) {
    if (!simulation) throw std::invalid_argument("Scene collision requires a physics world");
    if (!validBounds(minimum, maximum)) throw std::invalid_argument("Scene collision requires finite ordered bounds");
    impl_->simulation = std::move(simulation);
    impl_->collision = &collision;
    impl_->minimum = minimum; impl_->maximum = maximum;
}
PhysicsScene::~PhysicsScene() = default;
void PhysicsScene::ensureGroundNear(glm::vec3 minimum, glm::vec3 maximum) {
    CellRange range;
    if (!horizontalRange(minimum, maximum, impl_->minimum, impl_->maximum, groundSectionSize, range)) return;
    for (int x = range.left; x <= range.right; ++x) for (int z = range.first; z <= range.last; ++z) {
        const Cell section(x, z);
        if (impl_->groundSections.count(section)) continue;
        const auto faces = impl_->collision->staticFaces(
            {x * groundSectionSize, impl_->minimum.y - 1, z * groundSectionSize},
            {(x + 1) * groundSectionSize, impl_->maximum.y + 1, (z + 1) * groundSectionSize}, false);
        const glm::vec3 origin(x * groundSectionSize, 0, z * groundSectionSize);
        std::vector<b3Vec3> vertices;
        std::vector<int32_t> indices;
        std::set<std::size_t> newTriangles;
        glm::vec3 meshMinimum(std::numeric_limits<float>::max());
        glm::vec3 meshMaximum(std::numeric_limits<float>::lowest());
        for (const auto& face : faces) {
            if (!face.terrain || impl_->groundTriangles.count(face.index)) continue;
            newTriangles.insert(face.index);
            // Keep crossing triangles whole. Global IDs prevent a neighbor
            // from adding them again; resident sections retain their support.
            for (const auto p : face.vertices) {
                meshMinimum = glm::min(meshMinimum, p);
                meshMaximum = glm::max(meshMaximum, p);
                const auto q = p - origin;
                indices.push_back(static_cast<int32_t>(vertices.size()));
                vertices.push_back({q.x, q.y, q.z});
            }
        }
        std::vector<StaticCollisionFace> context;
        if (!indices.empty()) {
            // Include complete one-rings at every imported vertex, including
            // triangles whose collider belongs to a neighboring section.
            const glm::vec3 halo(.001f);
            context = impl_->collision->staticFaces(meshMinimum - halo, meshMaximum + halo, false);
            context.erase(std::remove_if(context.begin(), context.end(), [](const auto& face) { return !face.terrain; }),
                          context.end());
        }
        auto meshes = meshBodies(impl_->simulation, std::move(vertices), std::move(indices), origin, false, &context);
        std::set<Cell> newSections{section};
        // Allocate first, then publish by moving already-allocated nodes and
        // bodies. Failed construction leaves the section available for retry.
        impl_->bodies.reserve(impl_->bodies.size() + meshes.size());
        impl_->groundTriangles.merge(newTriangles);
        for (auto& mesh : meshes) impl_->bodies.push_back(std::move(mesh));
        impl_->groundSections.merge(newSections);
    }
}
void PhysicsScene::prewarmGround(glm::vec3 center, float range) {
    if (!finite(center) || !std::isfinite(range) || range < 0) return;
    ensureGroundNear(center - glm::vec3(range, 0, range), center + glm::vec3(range, 0, range));
}
void PhysicsScene::ensureNear(glm::vec3 minimum, glm::vec3 maximum) {
    if (!validBounds(minimum, maximum)) return;
    ensureGroundNear(minimum, maximum);
    impl_->syncRuntime();
    minimum = glm::max(minimum, impl_->minimum);
    maximum = glm::min(maximum, impl_->maximum);
    if (glm::any(glm::greaterThan(minimum, maximum))) return;
    CellRange range;
    if (!horizontalRange(minimum, maximum, impl_->minimum, impl_->maximum, cellSize, range)) return;
    for (int x = range.left; x <= range.right; ++x) for (int z = range.first; z <= range.last; ++z) {
        const Cell cell(x, z);
        if (impl_->cells.count(cell)) continue;
        const auto faces = impl_->collision->staticFaces({x * cellSize, impl_->minimum.y - 1, z * cellSize},
            {(x + 1) * cellSize, impl_->maximum.y + 1, (z + 1) * cellSize}, false);
        const glm::vec3 origin(x * cellSize, 0, z * cellSize);
        std::array<std::vector<b3Vec3>,2> vertices;
        std::array<std::vector<int32_t>,2> indices;
        std::set<std::size_t> newTriangles, newTrunks;
        std::vector<std::unique_ptr<StaticBody>> newBodies;
        glm::vec3 meshMinimum(std::numeric_limits<float>::max());
        glm::vec3 meshMaximum(std::numeric_limits<float>::lowest());
        for (const auto& face : faces) {
            if (face.terrain || impl_->triangles.count(face.index)) continue;
            newTriangles.insert(face.index);
            const auto surface = face.hardSurface ? 1u : 0u;
            for (const auto p : face.vertices) {
                meshMinimum = glm::min(meshMinimum, p);
                meshMaximum = glm::max(meshMaximum, p);
                indices[surface].push_back(static_cast<int32_t>(vertices[surface].size()));
                const auto local = p - origin;
                vertices[surface].push_back({local.x, local.y, local.z});
            }
        }
        // Tree capsules stay independent of foliage and camera visibility.
        for (const auto& item : impl_->collision->staticTrunks(
                 {x * cellSize, impl_->minimum.y - 1, z * cellSize},
                 {(x + 1) * cellSize, impl_->maximum.y + 20, (z + 1) * cellSize})) {
            if (impl_->trunks.count(item.first)) continue;
            newTrunks.insert(item.first);
            const auto& t = item.second;
            auto trunk = std::make_unique<StaticBody>();
            auto def = b3DefaultBodyDef(); def.position = {t.base.x, t.base.y, t.base.z};
            trunk->body = b3CreateBody(impl_->simulation->id(), &def);
            if (!b3Body_IsValid(trunk->body)) throw std::runtime_error("Cannot create tree physics body");
            auto shape = b3DefaultShapeDef();
            shape.filter.categoryBits = ordinaryBodyCategory | hardImpactCategory;
            shape.filter.maskBits = vehicleBodyCategory | playerBodyCategory | interiorBoxCategory | grenadeBodyCategory;
            const b3Capsule capsule{{0, t.radius, 0}, {0, t.height - t.radius, 0}, t.radius};
            if (!b3Shape_IsValid(b3CreateCapsuleShape(trunk->body, &shape, &capsule)))
                throw std::runtime_error("Cannot create tree physics shape");
            newBodies.push_back(std::move(trunk));
        }
        std::vector<StaticCollisionFace> context;
        if (!newTriangles.empty()) {
            const glm::vec3 halo(.001f);
            context = impl_->collision->staticFaces(meshMinimum - halo, meshMaximum + halo, false);
            context.erase(std::remove_if(context.begin(), context.end(),
                [](const StaticCollisionFace& face) { return face.terrain; }), context.end());
        }
        for (unsigned surface = 0; surface < 2; ++surface) {
            auto meshes = meshBodies(impl_->simulation, std::move(vertices[surface]), std::move(indices[surface]), origin,
                                     surface == 1, &context);
            for (auto& mesh : meshes) newBodies.push_back(std::move(mesh));
        }
        std::set<Cell> newCells{cell};
        impl_->bodies.reserve(impl_->bodies.size() + newBodies.size());
        impl_->triangles.merge(newTriangles);
        impl_->trunks.merge(newTrunks);
        for (auto& body : newBodies) impl_->bodies.push_back(std::move(body));
        impl_->cells.merge(newCells);
    }
}
void PhysicsScene::prepare(const BoxLayer& boxes, float padding) {
    impl_->syncRuntime();
    for (std::size_t i = 0; i < boxes.count(); ++i) {
        if (!boxes.active(i)) continue;
        const auto box = boxes.pose(i);
        // Cover the next frame and neighboring tiles before bodies reach them.
        const auto reach = glm::vec3(glm::length(box.halfExtents) + padding);
        const auto travel = boxes.velocity(i) * .2f;
        ensureNear(box.center + glm::min(travel, glm::vec3(0)) - reach,
                   box.center + glm::max(travel, glm::vec3(0)) + reach);
    }
}
} // namespace viewer
