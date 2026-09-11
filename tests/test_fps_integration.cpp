#include "collision_world.hpp"
#include "fps_controller.hpp"
#include "model.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>

namespace {
using Clock = std::chrono::steady_clock;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

float horizontalDistance(glm::vec3 a, glm::vec3 b) {
    const glm::vec2 delta(a.x - b.x, a.z - b.z);
    return glm::length(delta);
}

void checkPlayer(const viewer::FPSController& player, const viewer::CollisionWorld& world,
                 float minGround, float maxGround) {
    const glm::vec3 feet = player.feet();
    const auto& config = player.config();
    require(std::isfinite(feet.x) && std::isfinite(feet.y) && std::isfinite(feet.z),
            "Player position became non-finite");
    require(std::abs(player.eyePosition().y - feet.y - config.eyeHeight) < 0.0001f,
            "Camera eye offset changed during simulation");
    require(!world.insideBuilding(feet, config.radius, config.height),
            "Player entered a generated building");
    for (const auto& contact : world.contacts(feet, config.radius, config.height)) {
        require(contact.depth < 0.035f,
                "Player deeply penetrated scene geometry: " + std::to_string(contact.depth) + " m");
    }
    if (const auto terrain = world.groundAt(feet.x, feet.z, minGround, maxGround, true))
        require(feet.y >= terrain->height - 0.03f, "Player fell below the generated terrain");
    if (player.grounded()) {
        require(std::abs(glm::length(player.groundNormal()) - 1.0f) < 0.001f,
                "Ground contact normal is not normalized");
        require(player.groundNormal().y >= std::cos(glm::radians(config.maxSlopeDegrees)) - 0.01f,
                "Player is grounded on a non-walkable slope");
    }
}

struct WallCandidate {
    glm::vec3 point;
    glm::vec3 outward;
    glm::vec3 start;
};

// Pick a real, long, vertical exterior wall with clear terrain on one side and
// a solid building interior on the other. No scene-specific coordinates needed.
std::optional<WallCandidate> findWall(const viewer::Model& model,
                                     const viewer::CollisionWorld& world,
                                     const viewer::FPSConfig& config) {
    for (const auto& draw : model.draws) {
        if (draw.name.find("building") == std::string::npos) continue;
        const auto& primitive = model.primitives.at(draw.primitive);
        for (std::size_t i = 0; i + 2 < primitive.indices.size(); i += 3) {
            glm::vec3 vertices[3];
            for (std::size_t j = 0; j < 3; ++j)
                vertices[j] = glm::vec3(draw.transform * glm::vec4(
                    primitive.vertices.at(primitive.indices[i + j]).position, 1.0f));
            const glm::vec3 cross = glm::cross(vertices[1] - vertices[0], vertices[2] - vertices[0]);
            if (glm::length(cross) < 0.01f) continue;
            const glm::vec3 normal = glm::normalize(cross);
            if (std::abs(normal.y) > 0.01f) continue;
            const float wallLength = std::max({horizontalDistance(vertices[0], vertices[1]),
                                              horizontalDistance(vertices[1], vertices[2]),
                                              horizontalDistance(vertices[2], vertices[0])});
            const float top = std::max({vertices[0].y, vertices[1].y, vertices[2].y});
            if (wallLength < 12.0f) continue;
            const glm::vec3 center = (vertices[0] + vertices[1] + vertices[2]) / 3.0f;
            for (const float sign : {1.0f, -1.0f}) {
                const glm::vec3 outward = glm::normalize(glm::vec3(normal.x, 0, normal.z)) * sign;
                glm::vec3 outside = center + outward * 0.9f;
                const auto ground = world.groundAt(outside.x, outside.z,
                    model.boundsMin.y - 1, model.boundsMax.y + 1, true);
                if (!ground || ground->normal.y < 0.9f || top - ground->height < config.height + 0.5f) continue;
                outside.y = ground->height + config.skin;
                if (world.insideBuilding(outside, config.radius, config.height)) continue;
                const glm::vec3 inside = glm::vec3(center.x, outside.y, center.z) - outward * 0.9f;
                if (!world.insideBuilding(inside, config.radius, config.height)) continue;
                viewer::FPSController player;
                if (!player.spawn(outside + glm::vec3(0, config.eyeHeight, 0), world) ||
                    horizontalDistance(outside, player.feet()) > 0.1f) continue;
                bool blocked = false;
                for (const auto& contact : world.contacts(player.feet(), config.radius, config.height))
                    blocked = blocked || (contact.depth > 0.02f && contact.normal.y < 0.7f);
                if (!blocked) return WallCandidate{center, outward, player.feet()};
            }
        }
    }
    return std::nullopt;
}

void testActualBuilding(const viewer::Model& model, const viewer::CollisionWorld& world) {
    viewer::FPSController player;
    const auto& config = player.config();
    const auto wall = findWall(model, world, config);
    require(wall.has_value(), "Could not locate a suitable generated building wall for integration testing");
    require(player.spawn(wall->start + glm::vec3(0, config.eyeHeight, 0), world), "Wall-side spawn failed");
    viewer::FPSInput input;
    input.forward = 1;
    input.yawDegrees = glm::degrees(std::atan2(-wall->outward.z, -wall->outward.x));
    for (int frame = 0; frame < 120; ++frame) {
        player.update(1.0f / 120.0f, input, world);
        checkPlayer(player, world, model.boundsMin.y - 1, model.boundsMax.y + 1);
    }
    float clearance = glm::dot(player.feet() - wall->point, wall->outward);
    require(clearance >= config.radius - 0.035f && clearance < 0.5f,
            "Player did not stop at the real building wall: clearance " + std::to_string(clearance));

    const glm::vec3 beforeSlide = player.feet();
    input.right = 1;
    for (int frame = 0; frame < 120; ++frame) {
        player.update(1.0f / 120.0f, input, world);
        checkPlayer(player, world, model.boundsMin.y - 1, model.boundsMax.y + 1);
    }
    const glm::vec3 tangent(-wall->outward.z, 0, wall->outward.x);
    require(std::abs(glm::dot(player.feet() - beforeSlide, tangent)) > 0.5f,
            "Diagonal input failed to slide along the real building wall");

    glm::vec3 requestedInside = wall->point - wall->outward * 0.9f;
    requestedInside.y = wall->start.y + config.eyeHeight;
    require(player.spawn(requestedInside, world), "Spawn recovery from a building interior failed");
    require(horizontalDistance(player.feet(), requestedInside) > 0.4f,
            "Spawn did not move away from a generated building interior");
    checkPlayer(player, world, model.boundsMin.y - 1, model.boundsMax.y + 1);
    std::cout << "Actual building wall stop, diagonal slide, and interior spawn recovery passed\n";
}

void simulateRoutes(const viewer::Model& model, const viewer::CollisionWorld& world) {
    const glm::vec3 extent = model.boundsMax - model.boundsMin;
    double updateMilliseconds = 0;
    std::size_t frames = 0, groundedFrames = 0, airborneFrames = 0;
    for (const glm::vec2 fraction : {glm::vec2(0.5f), glm::vec2(0.3f, 0.7f), glm::vec2(0.7f, 0.3f)}) {
        viewer::FPSController player;
        const glm::vec3 preferred(model.boundsMin.x + extent.x * fraction.x,
                                  model.boundsMax.y + 30.0f,
                                  model.boundsMin.z + extent.z * fraction.y);
        require(player.spawn(preferred, world), "Could not spawn on generated terrain");
        checkPlayer(player, world, model.boundsMin.y - 1, model.boundsMax.y + 1);
        const glm::vec3 start = player.feet();
        float maximumDistance = 0;
        for (int frame = 0; frame < 1600; ++frame) {
            viewer::FPSInput input;
            input.forward = 1;
            input.right = frame % 240 < 120 ? 0.5f : -0.5f;
            input.yawDegrees = -90.0f + static_cast<float>(frame / 160) * 37.0f;
            input.run = frame % 320 < 160;
            input.jump = frame % 180 == 0;
            // Alternate short/long frames while preserving a 120 Hz average.
            const float dt = frame % 2 == 0 ? 1.0f / 240.0f : 1.0f / 80.0f;
            const auto before = Clock::now();
            player.update(dt, input, world);
            updateMilliseconds += std::chrono::duration<double, std::milli>(Clock::now() - before).count();
            ++frames;
            if (player.grounded()) ++groundedFrames; else ++airborneFrames;
            maximumDistance = std::max(maximumDistance, horizontalDistance(player.feet(), start));
            checkPlayer(player, world, model.boundsMin.y - 1, model.boundsMax.y + 1);
        }
        require(maximumDistance > 2.0f, "FPS route remained stuck near spawn");
    }
    require(groundedFrames > 100 && airborneFrames > 100,
            "Town routes did not exercise grounded walking and airborne motion");
    std::cout << frames << " town updates: " << groundedFrames << " grounded, " << airborneFrames
              << " airborne; controller " << updateMilliseconds / static_cast<double>(frames)
              << " ms/update (average update and physics rate: 120 Hz; includes collision queries)\n";
}

void testSmallAsset(const viewer::Model& model, const viewer::CollisionWorld& world) {
    viewer::FPSController player;
    glm::vec3 preferred = (model.boundsMin + model.boundsMax) * 0.5f;
    preferred.y = model.boundsMax.y + 5;
    require(player.spawn(preferred, world), "Could not spawn in the transformed test GLB");
    checkPlayer(player, world, model.boundsMin.y - 1, model.boundsMax.y + 1);
    require(player.grounded(), "Test GLB spawn was not grounded");
    const float standingY = player.feet().y;
    float apex = standingY;
    viewer::FPSInput input;
    for (int frame = 0; frame < 240; ++frame) {
        input.jump = frame < 120;
        player.update(1.0f / 120.0f, input, world);
        apex = std::max(apex, player.feet().y);
        checkPlayer(player, world, model.boundsMin.y - 1, model.boundsMax.y + 1);
    }
    require(apex - standingY > 0.65f && apex - standingY < 0.8f,
            "Jump apex in the transformed test GLB is outside human-sized range");
    require(player.grounded() && std::abs(player.feet().y - standingY) < 0.01f,
            "Test GLB jump did not land at the original surface");
    std::cout << "Transformed test GLB safe spawn, capsule clearance, jump, and landing passed\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "Usage: test_fps_integration assets/test.glb|output/godollo.glb");
        const auto model = viewer::loadModel(argv[1]);
        const auto before = Clock::now();
        const viewer::CollisionWorld world(model);
        const auto& stats = world.stats();
        std::cout << "Scene collision: " << stats.triangles << " triangles, " << stats.terrainTriangles
                  << " terrain triangles, " << stats.buildingTriangles << " building triangles, "
                  << stats.buildings << " buildings, " << stats.bvhNodes << " BVH nodes; build "
                  << std::chrono::duration<double, std::milli>(Clock::now() - before).count() << " ms\n";
        require(stats.bvhNodes > 1, "Scene collision acceleration structure was not built");
        if (stats.buildings > 100 && stats.terrainTriangles > 100) {
            testActualBuilding(model, world);
            simulateRoutes(model, world);
        } else {
            testSmallAsset(model, world);
        }
        std::cout << "GLB FPS integration checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FPS integration test failed: " << error.what() << '\n';
        return 1;
    }
}
