#pragma once
#include "protocol.hpp"
#include "resources.hpp"
#include "box_layer.hpp"
#include "collision_world.hpp"
#include "door_layer.hpp"
#include "forest_layer.hpp"
#include "physics_world.hpp"
#include "tree_layer.hpp"
#include "zombie_layer.hpp"
#include <filesystem>
#include <memory>
#include <optional>

namespace multiplayer {
struct Options {
    std::filesystem::path map = "output/godollo.glb";
    std::filesystem::path zombieAssets = resourceRoot() / "assets/zombies/models";
    std::string address = "127.0.0.1:27020";
    std::uint16_t port = 27020;
    std::optional<glm::vec2> spawn;
    std::size_t zombieCount = 100;
    float zombieRadius = 100;
    bool trees = true, forests = true, headless = false, hidden = false, bot = false;
    glm::vec2 look{-90, 0}; // Client yaw/pitch in degrees; independent of world identity.
    double seconds = 0;
    float fakeLagMs = 0, fakeLossPercent = 0;
    std::filesystem::path screenshot;
};
Options parseOptions(int argc, char** argv, bool server);
// Identical CPU scene preparation on both peers. Only the server steps physics.
struct Scene {
    explicit Scene(const Options& options);
    viewer::Model model;
    std::shared_ptr<viewer::PhysicsWorld> physics;
    std::unique_ptr<viewer::DoorLayer> doors;
    std::unique_ptr<viewer::BoxLayer> boxes;
    std::optional<viewer::TreeLayer> trees;
    std::optional<viewer::ForestLayer> forests;
    std::unique_ptr<viewer::CollisionWorld> collision;
    viewer::ZombieLayer zombies;
    glm::vec3 spawnEye{0};
    std::string fingerprint;
};
} // namespace multiplayer
