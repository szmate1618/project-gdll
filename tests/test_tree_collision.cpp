#include "collision_world.hpp"
#include "fps_controller.hpp"

#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

viewer::Model flatTerrain() {
    viewer::Model model;
    viewer::Primitive primitive;
    for (const glm::vec3 point : {glm::vec3(-100, 0, -100), glm::vec3(-100, 0, 100),
                                 glm::vec3(100, 0, 100), glm::vec3(100, 0, -100)}) {
        viewer::Vertex vertex;
        vertex.position = point;
        primitive.vertices.push_back(vertex);
    }
    primitive.indices = {0, 1, 2, 0, 2, 3};
    model.primitives.push_back(std::move(primitive));
    model.draws.push_back({0, glm::mat4(1), "terrain"});
    return model;
}

void testSidesAndEndcaps() {
    const viewer::CollisionWorld world({}, {{{0, 0, 0}, 0.2f, 4.0f}});
    const auto side = world.contacts({0.4f, 1, 0}, 0.3f, 1.8f);
    require(side.size() == 1 && side[0].normal.x > 0.999f && std::abs(side[0].normal.y) < 1e-6f,
            "Vertical trunk contact must push the player radially outward");
    require(std::abs(side[0].depth - 0.1f) < 1e-5f && std::abs(side[0].point.x - 0.2f) < 1e-5f,
            "Capsule radii, penetration depth, and trunk surface contact disagree");
    const auto diagonal = world.contacts({-0.3f, 0.01f, 0.3f}, 0.3f, 1.8f);
    require(diagonal.size() == 1 && diagonal[0].normal.x < -0.70f && diagonal[0].normal.z > 0.70f,
            "Trunk sides must supply rounded sliding normals");
    const auto upper = world.contacts({0, 3.9f, 0}, 0.3f, 1.8f);
    require(upper.size() == 1 && upper[0].normal.y > 0.999f &&
                std::abs(upper[0].depth - 0.1f) < 1e-5f && std::abs(upper[0].point.y - 4) < 1e-5f,
            "Trunk upper endcap extents or contact normal are wrong");
    const auto lower = world.contacts({0, -1.7f, 0}, 0.3f, 1.8f);
    require(lower.size() == 1 && lower[0].normal.y < -0.999f &&
                std::abs(lower[0].depth - 0.1f) < 1e-5f && std::abs(lower[0].point.y) < 1e-5f,
            "Trunk lower endcap extents or contact normal are wrong");
    const auto coincident = world.contacts({0, 0.1f, 0}, 0.3f, 1.8f);
    require(coincident.size() == 1 && coincident[0].normal.x == 1 && coincident[0].normal.y == 0 &&
                std::abs(coincident[0].depth - 0.5f) < 1e-5f,
            "Coincident player/trunk axes must have finite deterministic depenetration");
    require(world.contacts({0.52f, 0, 0}, 0.3f, 1.8f).empty() &&
                world.contacts({0.52f, 0, 0}, 0.3f, 1.8f, 0.03f).size() == 1,
            "Trunk broad phase must respect the capsule skin distance");
    require(world.contacts({0, 4.02f, 0}, 0.3f, 1.8f).empty() &&
                world.contacts({0, -1.82f, 0}, 0.3f, 1.8f).empty(),
            "A trunk must not collide beyond its rounded vertical extents");
    const viewer::CollisionWorld sphere({}, {{{0, 0, 0}, 0.2f, 0.4f}});
    require(sphere.contacts({0, 0.1f, 0}, 0.3f, 1.8f).size() == 1,
            "Capsule with height twice its radius must support a zero-length center segment");
}

void testNoFoliageFloorsAndSpawn() {
    const auto model = flatTerrain();
    const viewer::CollisionWorld world(model, {{{0, 0, 0}, 0.2f, 8}});
    const auto ground = world.groundAt(0, 0, -1, 20);
    require(ground && std::abs(ground->height) < 1e-6f,
            "Ground height must remain terrain, never the trunk/canopy top");
    require(!world.groundAt(0, 0, 1, 20) && !world.insideBuilding({0, 0, 0}, 0.3f, 1.8f),
            "Vegetation must not become a support mesh or a building footprint");
    require(world.stats().triangles == 2 && world.stats().trunks == 1 && world.stats().trunkBvhNodes == 1,
            "Trunk collision counts must stay separate from triangle collision geometry");
    const auto spawn = world.findSpawn({0, 20, 0}, 1.7f, 0.3f, 1.8f, 45);
    require(spawn && spawn->y < 0.02f && glm::length(glm::vec2(spawn->x, spawn->z)) >= 0.5f &&
                world.contacts(*spawn, 0.3f, 1.8f).empty(),
            "Spawn below a tree must relocate onto nearby clear terrain");
    const viewer::CollisionWorld onlyTrees({}, {{{0, 0, 0}, 0.2f, 8}});
    require(!onlyTrees.groundAt(0, 0, -1, 20) &&
                !onlyTrees.findSpawn({0, 20, 0}, 1.7f, 0.3f, 1.8f, 45),
            "Trees alone must not create spawnable ground");
    const auto debug = onlyTrees.debugLines({0, 1, 0});
    require(!debug.empty() && debug.size() % 2 == 0, "Nearby trunk capsules must appear in collision debugging");
    float low = 100, high = -100;
    for (const auto p : debug) { low = std::min(low, p.y); high = std::max(high, p.y); }
    require(std::abs(low) < 1e-5f && std::abs(high - 8) < 1e-5f,
            "Trunk debug capsule must use the physical endcap extents");
    require(onlyTrees.debugLines({100, 0, 100}, 1).empty(), "Debug query must cull distant trunk colliders");
}

void testWalkingStopsAndSlides() {
    const viewer::CollisionWorld world(flatTerrain(), {{{0, 0, 0}, 0.2f, 8}});
    viewer::FPSController stopped;
    require(stopped.spawn({-2, 1.7f, 0}, world), "Player spawn failed");
    viewer::FPSInput input;
    input.forward = 1;
    input.yawDegrees = 0;
    input.run = true;
    for (int frame = 0; frame < 240; ++frame) {
        stopped.update(1 / 120.0f, input, world);
        require(stopped.feet().x < -0.49f, "Sprinting player tunneled through a narrow trunk");
    }
    require(stopped.feet().x > -0.53f && stopped.grounded() && std::abs(stopped.feet().y) < 0.01f,
            "Player should stand against trunk without climbing or sinking");

    viewer::FPSController sliding;
    require(sliding.spawn({-2, 1.7f, -0.1f}, world), "Sliding player spawn failed");
    input.run = false;
    bool contacted = false;
    for (int frame = 0; frame < 360; ++frame) {
        // Approach slightly off center, then keep walking diagonally into the
        // trunk. Tangent motion must survive the radial collision correction.
        input.right = frame < 60 ? 0.0f : 0.3f;
        sliding.update(1 / 120.0f, input, world);
        const auto p = sliding.feet();
        const float distance = glm::length(glm::vec2(p.x, p.z));
        require(distance >= 0.497f && sliding.grounded(), "Diagonal movement penetrated the trunk or lost ground");
        contacted = contacted || distance < 0.52f;
    }
    require(contacted && sliding.feet().x > 2 && sliding.feet().z > 0.6f,
            "Player must slide around a trunk rather than freeze against it");
}

void testForestIndexAndInvalidInputs() {
    std::vector<viewer::TrunkCollider> trunks{{{0, 0, 0}, 0.2f, 6}};
    for (int x = 0; x < 220; ++x) for (int z = 0; z < 200; ++z) {
        trunks.push_back({{100 + 4.0f * x, 0, 100 + 4.0f * z}, 0.2f, 6});
    }
    const auto begin = std::chrono::steady_clock::now();
    const viewer::CollisionWorld forest({}, trunks);
    const auto built = std::chrono::steady_clock::now();
    require(forest.stats().trunks == 44001 && forest.stats().trunkBvhNodes > 1000,
            "Forest requires a static spatial acceleration structure");
    const auto isolated = forest.contacts({0.4f, 0, 0}, 0.3f, 1.8f);
    require(isolated.size() == 1 && isolated[0].normal.x > 0.999f,
            "Unrelated distant trees must not affect a local collision query");
    for (int i = 0; i < 20000; ++i) {
        const float x = 100.4f + 4 * (i % 220), z = 100.0f + 4 * ((i / 220) % 200);
        const auto hit = forest.contacts({x, 0, z}, 0.3f, 1.8f);
        require(hit.size() == 1 && hit[0].normal.x > 0.999f,
                "The forest index must isolate each nearby trunk throughout the scene");
    }
    require(forest.contacts({100.4f, 0, 100}, 0.3f, 1.8f).size() == 1 &&
                forest.contacts({102, 0, 102}, 0.3f, 1.8f).empty(),
            "Forest queries must find remote trunks and reject empty spaces between them");
    const auto finished = std::chrono::steady_clock::now();
    std::cout << "44,001 trunks: build " << std::chrono::duration<double, std::milli>(built - begin).count()
              << " ms; 20,000 local queries " << std::chrono::duration<double, std::milli>(finished - built).count()
              << " ms\n";

    for (const auto bad : {viewer::TrunkCollider{{0, 0, 0}, 0, 2},
                           viewer::TrunkCollider{{0, 0, 0}, 1, 1},
                           viewer::TrunkCollider{{0, std::numeric_limits<float>::infinity(), 0}, 0.2f, 2}}) {
        bool rejected = false;
        try { const viewer::CollisionWorld invalid({}, {bad}); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "Invalid trunk geometry must not silently omit collision");
    }
}

}  // namespace

int main() {
    try {
        testSidesAndEndcaps();
        testNoFoliageFloorsAndSpawn();
        testWalkingStopsAndSlides();
        testForestIndexAndInvalidInputs();
        std::cout << "Tree trunk collision tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Tree trunk collision test failed: " << error.what() << '\n';
        return 1;
    }
}
