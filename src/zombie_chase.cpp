#include "zombie_chase.hpp"
#include "zombie_layer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <glm/gtc/matrix_transform.hpp>

namespace viewer {
namespace {
constexpr double tau = 6.283185307179586;
}

bool turnZombieToward(ZombieInstance& instance, glm::vec2 direction, float seconds) {
    const float yaw = std::atan2(instance.restRoot[2].x, instance.restRoot[2].z);
    const float targetYaw = std::atan2(direction.x, direction.y);
    const float delta = std::remainder(targetYaw - yaw, static_cast<float>(tau));
    const float maxTurn = static_cast<float>(tau / 4) * seconds;
    const bool aligned = std::abs(delta) <= maxTurn + 1e-5f;
    const float nextYaw = aligned ? targetYaw : yaw + std::clamp(delta, -maxTurn, maxTurn);
    const auto feet = glm::vec3(instance.restRoot[3]);
    instance.restRoot = glm::rotate(glm::translate(glm::mat4(1), feet), nextYaw, {0, 1, 0});
    instance.transform = instance.restTransform = instance.restRoot * instance.chaseNormalization;
    instance.walking = false;
    return aligned;
}

bool moveZombieToward(ZombieInstance& instance, const AnimatedModel& /*asset*/,
                      glm::vec3 target, float speed, float seconds,
                      const CollisionWorld& world, double /*animationSeconds*/) {
    auto feet = glm::vec3(instance.restRoot[3]);
    const glm::vec2 horizontal(target.x - feet.x, target.z - feet.z);
    const float distance = glm::length(horizontal);
    instance.walking = distance > 1e-4f;
    if (!instance.walking) return true;
    const auto direction = horizontal / distance;
    const float step = std::min(speed * seconds, distance);
    feet.x += direction.x * step;
    feet.z += direction.y * step;
    // Terrain-only support follows the rendered triangle surface, below roofs.
    const auto ground = world.groundAt(feet.x, feet.z,
        std::numeric_limits<float>::lowest(), std::numeric_limits<float>::max(),
        world.stats().terrainTriangles != 0);
    if (ground) feet.y = ground->height;

    const float yaw = std::atan2(direction.x, direction.y);
    instance.restRoot = glm::rotate(glm::translate(glm::mat4(1), feet), yaw, {0, 1, 0});
    instance.transform = instance.restTransform = instance.restRoot * instance.chaseNormalization;
    instance.running = speed > 1.2f;
    // Advance the selected authored clip by actual travel time, excluding any
    // leftover step after arrival. No per-character skeleton is evaluated here.
    if (speed > 0) instance.walkSeconds += step / speed;
    return step >= distance;
}

} // namespace viewer
