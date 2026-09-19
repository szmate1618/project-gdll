#include "zombie_placement.hpp"
#include "zombie_layer.hpp"
#include "collision_world.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

void addQuad(viewer::Model& model, const glm::vec3& a, const glm::vec3& b,
             const glm::vec3& c, const glm::vec3& d, const std::string& name,
             const glm::mat4& transform = glm::mat4(1)) {
    viewer::Primitive primitive;
    for (const auto point : {a, b, c, d}) {
        viewer::Vertex vertex;
        vertex.position = point;
        primitive.vertices.push_back(vertex);
        const glm::vec3 worldPoint(transform * glm::vec4(point, 1));
        if (model.primitives.empty() && primitive.vertices.size() == 1) {
            model.boundsMin = model.boundsMax = worldPoint;
        } else {
            model.boundsMin = glm::min(model.boundsMin, worldPoint);
            model.boundsMax = glm::max(model.boundsMax, worldPoint);
        }
    }
    primitive.indices = {0, 1, 2, 0, 2, 3};
    model.draws.push_back({model.primitives.size(), transform, name});
    model.primitives.push_back(std::move(primitive));
}

viewer::Model flatTerrain(float extent, const glm::vec3 offset = glm::vec3(0),
                          const std::string& name = "terrain") {
    viewer::Model model;
    addQuad(model, {-extent, 0, -extent}, {-extent, 0, extent},
            {extent, 0, extent}, {extent, 0, -extent}, name,
            glm::translate(glm::mat4(1), offset));
    return model;
}

void thousandInstancesAreSpacedAndRepeatable() {
    const glm::vec3 offset(420, 17, -320);
    const auto model = flatTerrain(100, offset);
    const viewer::CollisionWorld world(model);
    const glm::vec2 center(offset.x, offset.z);
    const auto placements = viewer::placeZombies(world, model, center, 1000);
    const auto repeated = viewer::placeZombies(world, model, center, 1000);
    require(placements.size() == 1000, "Full requested population must be placed");
    float previousDistance = 0;
    bool differingYaw = false;
    for (std::size_t i = 0; i < placements.size(); ++i) {
        const glm::vec3 position(placements[i][3]);
        require(std::abs(position.y - 17) < 1e-5f, "Feet must lie on translated scene ground");
        const float distance = glm::length(glm::vec2(position.x, position.z) - center);
        require(distance >= 2 && distance <= 65, "Respect radius and player clearing");
        require(distance + 1e-5f >= previousDistance, "Place nearest to the player first");
        previousDistance = distance;
        for (int column = 0; column < 4; ++column) {
            require(placements[i][column] == repeated[i][column], "Placement must be repeatable");
        }
        require(std::abs(glm::determinant(glm::mat3(placements[i])) - 1) < 1e-5f &&
                    glm::length(placements[i][1] - glm::vec4(0, 1, 0, 0)) < 1e-6f,
                    "Transforms must preserve upright unit scale");
        differingYaw = differingYaw || placements[i][0] != placements[0][0];
        for (std::size_t j = 0; j < i; ++j) {
            const glm::vec3 other(placements[j][3]);
            require(glm::length(glm::vec2(position.x - other.x, position.z - other.z)) >= 2,
                    "Zombie centers must remain at least two meters apart");
        }
    }
    require(differingYaw, "Crowd must have varied idle facing directions");
    const auto fallback = viewer::placeZombies(world, model, {0, 0}, 1);
    require(glm::length(glm::vec2(fallback[0][3].x, fallback[0][3].z) - center) < 4,
            "Out-of-bounds preference must fall back to this scene, not the origin");
}

void triangleGroundBuildingsAndTrunks() {
    viewer::Model model;
    // This folded quad is two planar triangles, not a bilinear height patch.
    addQuad(model, {-40, 0, -40}, {-40, 0, 40}, {40, 12, 40}, {40, 0, -40},
            "terrain", glm::translate(glm::mat4(1), {100, 25, -150}));
    addQuad(model, {92, 45, -158}, {92, 45, -142}, {108, 45, -142}, {108, 45, -158},
            "building_test_roof");
    addQuad(model, {92, 20, -158}, {108, 20, -158}, {108, 20, -142}, {92, 20, -142},
            "building_test_walls");
    const glm::vec3 tree(114.4f, 28, -150);
    const float trunkRadius = 2.0f;
    const viewer::CollisionWorld world(model, {{tree, trunkRadius, 15}});
    const auto placements = viewer::placeZombies(world, model, {100, -150}, 300, 38);
    require(placements.size() == 300, "Obstacles must not silently reduce population");
    for (const auto& matrix : placements) {
        const glm::vec3 p(matrix[3]);
        const float expectedY = 25 + 0.15f * std::min(p.x - 60, p.z + 190);
        require(std::abs(p.y - expectedY) < 0.0001f,
                "Feet must follow the rendered transformed triangles, not averaged or bilinear heights");
        require(p.x <= 92 || p.x >= 108 || p.z <= -158 || p.z >= -142,
                "Building interiors and their roofs must not become crowd ground");
        require(glm::length(glm::vec2(p.x - tree.x, p.z - tree.z)) > trunkRadius + 0.44f,
                "Ground placement must respect trunk capsules");
    }
}

template<typename F> void requiresFailure(F operation, const std::string& detail) {
    bool failed = false;
    try { operation(); }
    catch (const std::exception& error) { failed = std::string(error.what()).find(detail) != std::string::npos; }
    require(failed, "Invalid or insufficient placement must fail with a descriptive reason");
}

void insufficientGroundAndGenericFallback() {
    const auto small = flatTerrain(5);
    const viewer::CollisionWorld smallWorld(small);
    requiresFailure([&] { viewer::placeZombies(smallWorld, small, {0, 0}, 1000); }, "only");
    const auto generic = flatTerrain(40, {10, 7, 20}, "unnamed_mesh");
    const viewer::CollisionWorld genericWorld(generic);
    const auto placements = viewer::placeZombies(genericWorld, generic, {10, 20}, 10);
    for (const auto& matrix : placements) require(std::abs(matrix[3].y - 7) < 1e-5f,
        "A scene without classified terrain must support generic surface placement");

    auto patch = flatTerrain(1);
    addQuad(patch, {-40, 5, -40}, {-40, 5, 40}, {40, 5, 40}, {40, 5, -40}, "building_roof");
    const viewer::CollisionWorld patchWorld(patch);
    requiresFailure([&] { viewer::placeZombies(patchWorld, patch, {0, 0}, 10); }, "only 0");

    viewer::Model slope;
    addQuad(slope, {-30, -30, -30}, {-30, -30, 30}, {30, 30, 30}, {30, 30, -30}, "terrain");
    const viewer::CollisionWorld slopeWorld(slope);
    requiresFailure([&] { viewer::placeZombies(slopeWorld, slope, {0, 0}, 1); }, "only 0");
    requiresFailure([&] { viewer::placeZombies(smallWorld, small, {0, 0}, 1, -1); }, "positive radius");
    requiresFailure([&] { viewer::placeZombies(smallWorld, small,
        {std::numeric_limits<float>::quiet_NaN(), 0}, 1); }, "finite center");
    require(viewer::placeZombies(smallWorld, small, {0, 0}, 0).empty(), "Zero requested zombies need no placement");
}

void cameraRayActivatesNearestZombieRagdoll() {
    viewer::ZombieLayer layer;
    layer.assets.emplace_back();
    viewer::ZombieInstance far;
    far.asset = 0;
    far.restRoot = glm::translate(glm::mat4(1), {0, 0, -8});
    far.restTransform = far.restRoot;
    far.transform = far.restTransform;
    layer.instances.push_back(far);
    viewer::ZombieInstance near = far;
    near.restRoot = glm::translate(glm::mat4(1), {0, 0, -3});
    near.restTransform = near.restRoot;
    near.transform = near.restTransform;
    layer.instances.push_back(near);

    require(layer.shoot({0, 0.9f, 3}, {0, 0, -1}), "Camera ray must activate a zombie");
    require(layer.ragdollCount() == 1, "A camera ray must activate only one zombie");
    require(!layer.instances[0].ragdoll && layer.instances[1].ragdoll,
            "A camera ray must choose the nearest zombie");
    const auto before = layer.instances[1].transform;
    layer.update(1.0f / 30.0f);
    require(glm::length(glm::vec3(layer.instances[1].transform[3]) - glm::vec3(before[3])) > 1e-4f,
            "Box3D ragdoll simulation must update the zombie transform");
}

}  // namespace

int main() {
    try {
        thousandInstancesAreSpacedAndRepeatable();
        triangleGroundBuildingsAndTrunks();
        insufficientGroundAndGenericFallback();
        cameraRayActivatesNearestZombieRagdoll();
        std::cout << "Zombie placement tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Zombie placement test failed: " << error.what() << '\n';
        return 1;
    }
}
