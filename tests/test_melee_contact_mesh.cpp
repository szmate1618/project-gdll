#include "melee_contact_mesh.hpp"
#include "physics_world.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
std::vector<b3Vec3> seamQuad() {
    return {{-1, -1, 0}, {1, -1, 0}, {1, 1, 0}, {-1, -1, 0}, {1, 1, 0}, {-1, 1, 0}};
}
const std::vector<int32_t> indices{0, 1, 2, 3, 4, 5};
b3RayResult ray(viewer::PhysicsWorld& world, float x, float y, float z) {
    auto filter = b3DefaultQueryFilter();
    filter.categoryBits = viewer::playerBodyCategory;
    return b3World_CastRayClosest(world.id(), {x, y, z}, {0, 0, -4}, filter);
}
void movingSurfaceKeepsItsBodyAndRefreshesQueries() {
    viewer::PhysicsWorld world;
    auto vertices = seamQuad();
    const auto empty = b3World_GetCounters(world.id());
    {
        viewer::MeleeContactMesh surface(world.id(), vertices, indices, {20, 3, -8});
        const auto body = surface.body();
        require(ray(world, 20.3f, 3, -6).hit, "Initial triangle mesh must be queryable");
        for (auto& v : vertices) { v.x += 8; v.z += .75f; }
        // Bend the shared corner, preserving UV-seam welding.
        vertices[2].z += .4f; vertices[4].z += .4f;
        require(surface.update(vertices, {20, 3, -8}), "A deforming mesh must reuse valid topology");
        require(B3_ID_EQUALS(surface.body(), body), "Pose updates must retain the physics body");
        require(!ray(world, 20.3f, 3, -6).hit, "Old broadphase bounds must not retain the moved surface");
        const auto hit = ray(world, 28.3f, 3, -6);
        require(hit.hit && hit.fraction < .32f, "New bounds and bent triangle planes must be queryable");
        const auto counters = b3World_GetCounters(world.id());
        require(counters.bodyCount == empty.bodyCount + 1 && counters.shapeCount == empty.shapeCount + 1,
                "Deformation must not allocate extra physics actors");
    }
    const auto removed = b3World_GetCounters(world.id());
    require(removed.bodyCount == empty.bodyCount && removed.shapeCount == empty.shapeCount,
            "Contact mesh destruction must release its body and shape");
}
void rejectedUpdatesKeepTheOldSurface() {
    viewer::PhysicsWorld world;
    const auto original = seamQuad();
    viewer::MeleeContactMesh surface(world.id(), original, indices, {0, 0, 0});
    auto vertices = original;
    vertices[3].x += .05f;
    require(!surface.update(vertices, {9, 0, 0}), "Splitting a welded seam must request fresh topology");
    vertices = original;
    vertices[1] = vertices[0];
    require(!surface.update(vertices, {9, 0, 0}), "Collapsed triangles must request fresh topology");
    vertices = original;
    vertices[2].z = vertices[4].z = std::numeric_limits<float>::quiet_NaN();
    require(!surface.update(vertices, {9, 0, 0}), "Invalid coordinates must be rejected before mutation");
    require(ray(world, .3f, 0, 2).hit && !ray(world, 9.3f, 0, 2).hit,
            "Rejected updates must retain the old surface and placement");
    vertices = original;
    vertices[1] = vertices[0];
    viewer::MeleeContactMesh missingFace(world.id(), vertices, indices, {20, 0, 0});
    require(!missingFace.update(original, {20, 0, 0}),
            "A face dropped during construction must be rebuilt when it reappears");
}
using EdgeFlags = std::map<std::array<int32_t, 3>, uint8_t>;
EdgeFlags flags(const b3MeshData* mesh) {
    EdgeFlags result;
    const auto* triangles = b3GetMeshTriangles(mesh);
    const auto* edgeFlags = b3GetMeshFlags(mesh);
    for (int i = 0; i < mesh->triangleCount; ++i)
        result[{triangles[i].index1, triangles[i].index2, triangles[i].index3}] = edgeFlags[i];
    return result;
}
void refitMatchesFreshGeometryAndEdges() {
    auto vertices = seamQuad();
    auto definition = b3MeshDef{};
    std::vector<int32_t> vertexMap(vertices.size());
    definition.vertices = vertices.data();
    definition.indices = const_cast<int32_t*>(indices.data());
    definition.vertexCount = static_cast<int>(vertices.size());
    definition.triangleCount = 2;
    definition.identifyEdges = true;
    definition.weldVertices = true;
    definition.weldTolerance = .0001f;
    definition.vertexMap = vertexMap.data();
    std::unique_ptr<b3MeshData, decltype(&b3DestroyMesh)> mesh(b3CreateMesh(&definition, nullptr, 0), b3DestroyMesh);
    require(mesh && mesh->vertexCount == 4 && vertexMap[0] == vertexMap[3] && vertexMap[2] == vertexMap[4],
            "Mesh creation must export the collision vertex mapping across seams");
    const auto flat = flags(mesh.get());
    vertices[2].z = vertices[4].z = 1;
    std::vector<b3Vec3> compact(mesh->vertexCount);
    for (std::size_t i = 0; i < vertices.size(); ++i) compact[vertexMap[i]] = vertices[i];
    require(b3RefitMesh(mesh.get(), compact.data(), static_cast<int>(compact.size()), true), "Refit must succeed");
    definition.vertexMap = nullptr;
    std::unique_ptr<b3MeshData, decltype(&b3DestroyMesh)> fresh(b3CreateMesh(&definition, nullptr, 0), b3DestroyMesh);
    require(fresh && flags(mesh.get()) == flags(fresh.get()) && flags(mesh.get()) != flat,
            "Bent edge classifications must match a fresh mesh, not the old flat flags");
    require(std::abs(mesh->surfaceArea - fresh->surfaceArea) < .00001f &&
            mesh->bounds.lowerBound.x == fresh->bounds.lowerBound.x &&
            mesh->bounds.upperBound.z == fresh->bounds.upperBound.z,
            "Refit must retain valid geometry and update surface metadata");
}
void refitUpdatesEveryBranchOfTheTree() {
    std::unique_ptr<b3MeshData, decltype(&b3DestroyMesh)> grid(b3CreateGridMesh(12, 12, .25f, 1, true), b3DestroyMesh);
    require(grid && grid->nodeCount > 1, "Grid fixture must exercise internal BVH nodes");
    std::vector<b3Vec3> vertices(b3GetMeshVertices(grid.get()), b3GetMeshVertices(grid.get()) + grid->vertexCount);
    std::vector<int32_t> triangles;
    for (int i = 0; i < grid->triangleCount; ++i) {
        const auto& t = b3GetMeshTriangles(grid.get())[i];
        triangles.insert(triangles.end(), {t.index1, t.index3, t.index2});
    }
    viewer::PhysicsWorld world, reference;
    viewer::MeleeContactMesh surface(world.id(), vertices, triangles, {0, 0, 0});
    // Turn the grid to face our -Z queries and move it outside the old bounds.
    for (auto& v : vertices) v = {v.x + 8, v.z, .3f * std::sin(v.x * 2)};
    require(surface.update(vertices, {0, 0, 0}), "A multi-node grid must support deformation");
    viewer::MeleeContactMesh rebuilt(reference.id(), vertices, triangles, {0, 0, 0});
    int hits = 0;
    for (float x = 6.6f; x < 9.5f; x += .17f)
        for (float y = -1.4f; y < 1.5f; y += .19f) {
            const auto a = ray(world, x, y, 2), b = ray(reference, x, y, 2);
            require(a.hit == b.hit && (!a.hit || std::abs(a.fraction - b.fraction) < .00001f),
                    "All refitted BVH branches must give the same ray intersections as a fresh mesh");
            hits += a.hit;
        }
    require(hits > 30, "Ray comparison must exercise actual grid triangles");
}
}

int main() {
    try {
        movingSurfaceKeepsItsBodyAndRefreshesQueries();
        rejectedUpdatesKeepTheOldSurface();
        refitMatchesFreshGeometryAndEdges();
        refitUpdatesEveryBranchOfTheTree();
        std::cout << "Melee contact mesh refit checks passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n'; return 1;
    }
}
