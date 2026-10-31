#pragma once

#include "zombie_layer.hpp"
#include "zombie_cavity_physics.hpp"
#include "physics_world.hpp"

#include <unordered_set>

namespace viewer {
// Shared by the corpse and living attack owners; neither steps the world itself.
struct ZombieLayer::Physics {
    Physics() : Physics(std::make_shared<PhysicsWorld>()) {}
    explicit Physics(std::shared_ptr<PhysicsWorld> shared)
        : simulation(std::move(shared)), world(simulation), cavities(simulation) {}
    std::shared_ptr<PhysicsWorld> simulation;
    ZombieRagdollWorld world;
    ZombieCavityPhysics cavities;
    std::unordered_set<std::size_t> attacks;
};
} // namespace viewer
