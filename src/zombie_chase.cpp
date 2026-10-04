#include "zombie_chase.hpp"
#include "zombie_layer.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <glm/gtc/matrix_transform.hpp>

namespace viewer {
namespace {
constexpr float alertDistance = 20.0f;
constexpr float walkSpeed = 1.2f; // Meters per second in the runtime frame.
constexpr double tau = 6.283185307179586;

bool finite(glm::vec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
}

std::vector<glm::mat4> sampleZombieWalkPose(const AnimatedModel& asset,
                                         double animationSeconds, double walkSeconds,
                                         float phase) {
    auto pose = sampleAnimatedPose(asset, animationSeconds, phase);
    const auto base = pose;
    std::vector<float> angles(pose.size(), 0.0f);
    const float swing = std::sin(static_cast<float>(tau * (std::fmod(walkSeconds, 1.0) + phase)));
    const auto angle = [&](RagdollPart part, float value) {
        const int bone = asset.ragdollBones[static_cast<std::size_t>(part)];
        if (bone >= 0 && static_cast<std::size_t>(bone) < angles.size())
            angles[static_cast<std::size_t>(bone)] = value;
    };
    angle(RagdollPart::upperLegLeft, -0.35f * swing);
    angle(RagdollPart::upperLegRight, 0.35f * swing);
    angle(RagdollPart::lowerLegLeft, 0.5f * std::max(swing, 0.0f));
    angle(RagdollPart::lowerLegRight, 0.5f * std::max(-swing, 0.0f));

    std::vector<glm::mat4> motion(pose.size(), glm::mat4(1));
    std::vector<bool> ready(pose.size(), false);
    // glTF node order does not guarantee parents precede children.
    std::function<void(std::size_t)> resolve = [&](std::size_t bone) {
        if (ready[bone]) return;
        const int parent = asset.skeleton[bone].parent;
        if (parent >= 0) {
            const auto p = static_cast<std::size_t>(parent);
            resolve(p);
            motion[bone] = motion[p];
        }
        if (angles[bone] != 0.0f) {
            const auto pivot = glm::vec3(base[bone][3]);
            motion[bone] *= glm::translate(glm::mat4(1), pivot) *
                glm::rotate(glm::mat4(1), angles[bone], {1, 0, 0}) *
                glm::translate(glm::mat4(1), -pivot);
        }
        pose[bone] = motion[bone] * base[bone];
        ready[bone] = true;
    };
    for (std::size_t bone = 0; bone < pose.size(); ++bone) resolve(bone);
    return pose;
}

void ZombieLayer::chase(float seconds, glm::vec3 target, const CollisionWorld& world,
                       double animationSeconds) {
    if (!std::isfinite(seconds) || seconds <= 0.0f || !finite(target)) return;
    for (auto& instance : instances) {
        if (instance.ragdoll || instance.asset >= assets.size()) continue;
        auto feet = glm::vec3(instance.restRoot[3]);
        const auto offset = target - feet;
        if (!instance.alerted && glm::dot(offset, offset) <= alertDistance * alertDistance) {
            instance.chaseNormalization = glm::inverse(instance.restRoot) * instance.restTransform;
            instance.alerted = true;
        }
        if (!instance.alerted) continue;

        const glm::vec2 horizontal(offset.x, offset.z);
        const float distance = glm::length(horizontal);
        instance.walking = distance > 1e-4f;
        if (!instance.walking) continue;
        const auto direction = horizontal / distance;
        const float step = std::min(walkSpeed * seconds, distance);
        feet.x += direction.x * step;
        feet.z += direction.y * step;
        // A vertical BVH query follows the rendered terrain, ignoring roofs,
        // trees, walls, and other zombies. Beyond the map keep the last height.
        const auto ground = world.groundAt(feet.x, feet.z,
            std::numeric_limits<float>::lowest(), std::numeric_limits<float>::max(),
            world.stats().terrainTriangles != 0);
        if (ground) feet.y = ground->height;

        const float yaw = std::atan2(direction.x, direction.y);
        instance.restRoot = glm::rotate(glm::translate(glm::mat4(1), feet), yaw, {0, 1, 0});
        instance.transform = instance.restTransform = instance.restRoot * instance.chaseNormalization;
        instance.walkSeconds += step / walkSpeed;
        const auto& asset = assets[instance.asset];
        instance.walkPose = sampleZombieWalkPose(asset, animationSeconds, instance.walkSeconds, instance.phase);
        instance.ragdollBones.resize(asset.skeleton.size());
        for (std::size_t bone = 0; bone < asset.skeleton.size(); ++bone)
            instance.ragdollBones[bone] = instance.walkPose[bone] * asset.skeleton[bone].inverseBind;
    }
}

} // namespace viewer
