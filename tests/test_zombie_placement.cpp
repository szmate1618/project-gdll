#include "zombie_placement.hpp"
#include "zombie_layer.hpp"
#include "zombie_park.hpp"
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

void parkPopulationCoversConcaveRegion() {
    auto model = flatTerrain(120, {200, 7, -300});
    addQuad(model, {165, 15, -360}, {165, 15, -340}, {185, 15, -340}, {185, 15, -360},
            "building_roof");
    addQuad(model, {165, 0, -360}, {185, 0, -360}, {185, 0, -340}, {165, 0, -340},
            "building_floor");
    const viewer::CollisionWorld world(model, {{{240, 7, -350}, 3, 15}});
    const std::vector<glm::vec2> boundary = {
        {120, -380}, {280, -380}, {280, -300}, {200, -300}, {200, -220}, {120, -220}};
    const auto placements = viewer::placeZombiesInRegion(world, model, boundary, 300);
    const auto repeat = viewer::placeZombiesInRegion(world, model, boundary, 300);
    require(placements.size() == 300, "Park must contain the full 300 zombies");
    int regions[3]{};
    for (std::size_t i = 0; i < placements.size(); ++i) {
        const glm::vec3 p(placements[i][3]);
        require(p.x > 120 && p.x < 280 && p.z > -380 && p.z < -220 &&
                    (p.x < 200 || p.z < -300), "Respect concave park outline, not just its bounds");
        require(std::abs(p.y - 7) < 1e-5f, "Park feet must rest on terrain, not building roofs");
        require(!world.insideBuilding(p, .45f, 2), "Park placement must avoid buildings");
        require(glm::length(glm::vec2(p.x - 240, p.z + 350)) > 3.44f, "Park placement must avoid trunks");
        ++regions[p.z >= -300 ? 2 : p.x >= 200 ? 1 : 0];
        for (int c = 0; c < 4; ++c) require(placements[i][c] == repeat[i][c], "Park placement must repeat");
        for (std::size_t j = 0; j < i; ++j)
            require(glm::length(glm::vec2(p.x - placements[j][3].x, p.z - placements[j][3].z)) >= 2,
                    "Park zombies must be separated");
    }
    for (const auto population : regions) require(population >= 75 && population <= 125,
        "Population must be spread approximately equally across three equal-area park sections");
    // Check coverage independently: no large empty gaps in accessible areas.
    for (float z = -375; z < -225; z += 10) for (float x = 125; x < 275; x += 10) {
        if (x >= 200 && z >= -300) continue;
        if (x > 160 && x < 190 && z > -365 && z < -335) continue;
        float nearest = std::numeric_limits<float>::max();
        for (const auto& m : placements) nearest = std::min(nearest, glm::length(glm::vec2(m[3].x - x, m[3].z - z)));
        require(nearest < 13, "Evenly spread crowd must cover the whole accessible park");
    }
}

void parkBoundaryUsesPublishedFrame() {
    auto model = flatTerrain(3000);
    require(viewer::zombieParkBoundary(model, VIEWER_ZOMBIE_PARK).empty(),
            "Generic scenes must not receive geographic park placement");
    model.geographicFrame = viewer::Model::GeographicFrame{"EPSG:32634", {376000, 5272000}};
    const auto first = viewer::zombieParkBoundary(model, VIEWER_ZOMBIE_PARK);
    require(first.size() >= 3, "Sourced palace park must load in its projected CRS");
    model.geographicFrame->origin += glm::dvec2(100, 200);
    const auto moved = viewer::zombieParkBoundary(model, VIEWER_ZOMBIE_PARK);
    require(moved.size() == first.size(), "Changing the map origin must retain the outline");
    for (std::size_t i = 0; i < first.size(); ++i)
        require(glm::length(moved[i] - first[i] - glm::vec2(-100, 200)) < .001f,
                "Map origin must shift east and reverse north into world south exactly once");
    model.geographicFrame->crs = "EPSG:23700";
    require(viewer::zombieParkBoundary(model, VIEWER_ZOMBIE_PARK).empty(), "Mismatched CRS must not be misprojected");
    model.geographicFrame->crs = "EPSG:32634";
    model.geographicFrame->origin += glm::dvec2(100000, 100000);
    require(viewer::zombieParkBoundary(model, VIEWER_ZOMBIE_PARK).empty(), "Maps outside the park must use other placement");
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
    requiresFailure([&] { viewer::placeZombiesInRegion(smallWorld, small,
        {{-4, -4}, {4, -4}, {4, 4}, {-4, 4}}, 300); }, "only");
    requiresFailure([&] { viewer::placeZombiesInRegion(smallWorld, small, {}, 1); }, "three vertices");
    require(viewer::placeZombiesInRegion(smallWorld, small, {}, 0).empty(), "Zero park population needs no search");
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
    require(layer.shoot({0, 1.78f, 3}, {0, 0, -1}),
            "The upper head must be hittable even on an unmapped rig");
    require(layer.ragdollCount() == 2, "Headshots must activate a ragdoll");
    require(!layer.shoot({0, 1.78f, 3}, {0, 0, -1}),
            "Already activated zombies must not consume another shot");
}

}  // namespace

int main() {
    try {
        thousandInstancesAreSpacedAndRepeatable();
        triangleGroundBuildingsAndTrunks();
        parkPopulationCoversConcaveRegion();
        parkBoundaryUsesPublishedFrame();
        insufficientGroundAndGenericFallback();
        cameraRayActivatesNearestZombieRagdoll();
        std::cout << "Zombie placement tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Zombie placement test failed: " << error.what() << '\n';
        return 1;
    }
}
