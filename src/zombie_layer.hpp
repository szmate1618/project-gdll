#pragma once

#include "animated_model.hpp"
#include "collision_world.hpp"
#include "zombie_behavior.hpp"

#include <memory>

namespace viewer {

struct ZombieInstance {
    std::size_t asset = 0;
    glm::mat4 transform{1};
    // Live placement until shot; then frozen as the ragdoll handoff basis.
    glm::mat4 restTransform{1};
    glm::mat4 restRoot{1};
    float phase = 0; // Fraction of the idle cycle, independent of frame rate.
    bool walking = false; // Live locomotion skinning, for walking and running.
    glm::mat4 chaseNormalization{1}; // Captured on activation; preserve asset scale/offset.
    double walkSeconds = 0;
    std::vector<glm::mat4> walkPose;
    bool ragdoll = false;
    std::size_t ragdollHandle = 0;
    // Skeleton world pose captured from the displayed frame, in asset space.
    std::vector<glm::mat4> activationPose;
    // Asset-space bone matrices uploaded for walking or ragdoll skinning.
    std::vector<glm::mat4> ragdollBones;
};

struct ZombieLayer {
    ZombieLayer();
    ~ZombieLayer();
    ZombieLayer(ZombieLayer&&) noexcept;
    ZombieLayer& operator=(ZombieLayer&&) noexcept;
    ZombieLayer(const ZombieLayer&) = delete;
    ZombieLayer& operator=(const ZombieLayer&) = delete;

    std::vector<AnimatedModel> assets;
    std::vector<ZombieInstance> instances;
    ZombieBehaviorPool behaviors;
    glm::vec3 boundsMin{0}, boundsMax{0};

    // Returns true when the nearest living zombie under the ray was activated.
    // animationSeconds is the time of the last displayed frame (before phase).
    bool shoot(glm::vec3 origin, glm::vec3 direction, double animationSeconds = 0.0);
    // Perception/state update, followed by terrain-following locomotion.
    // The app advances SimulationClock before calling this once per step.
    void updateBehavior(float seconds, const ZombiePlayer& player, const CollisionWorld& world,
                        double animationSeconds = 0.0);
    // Convenience entry point for a standard 1.8 m player; target is feet.
    void chase(float seconds, glm::vec3 target, const CollisionWorld& world,
               double animationSeconds = 0.0);
    void update(float seconds);
    [[nodiscard]] std::size_t ragdollCount() const;

private:
    struct Physics;
    std::unique_ptr<Physics> physics_;
};

bool hasZombieModels(const std::filesystem::path& source);

// Source is one animated glTF/GLB or a directory of individual characters.
// Character height is normalized to 1.8 m; feet use the rendered town surface.
ZombieLayer loadZombieLayer(const std::filesystem::path& source,
                           const CollisionWorld& world, const Model& scene,
                           glm::vec2 center, std::size_t count = 1000,
                           float radius = 100.0f,
                           const std::vector<glm::vec2>& boundary = {});

} // namespace viewer
