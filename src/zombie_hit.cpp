#include "zombie_hit.hpp"
#include "zombie_layer.hpp"

#include <algorithm>
#include <limits>

namespace viewer {

ZombieHitSphere zombieHeadHitSphere(const AnimatedModel& asset, const ZombieInstance& instance,
                                   const std::vector<glm::mat4>& pose) {
    const int head = asset.ragdollBones[static_cast<std::size_t>(RagdollPart::head)];
    const auto fallback = ZombieHitSphere{
        glm::vec3(instance.restRoot * glm::vec4(0, 1.65f, 0, 1)), 0.23f};
    if (head < 0 || static_cast<std::size_t>(head) >= pose.size()) return fallback;
    std::vector<bool> headJoint(asset.skeleton.size(), false);
    for (std::size_t joint = 0; joint < asset.skeleton.size(); ++joint) {
        int ancestor = static_cast<int>(joint);
        for (std::size_t depth = 0; ancestor >= 0 && depth < asset.skeleton.size(); ++depth) {
            if (ancestor == head) { headJoint[joint] = true; break; }
            ancestor = asset.skeleton[static_cast<std::size_t>(ancestor)].parent;
        }
    }
    auto low = glm::vec3(std::numeric_limits<float>::max());
    auto high = glm::vec3(std::numeric_limits<float>::lowest());
    bool found = false;
    for (std::size_t p = 0; p < asset.animation.size(); ++p) {
        const auto& skinning = asset.animation[p].skinning;
        const auto& vertices = asset.model.primitives[p].vertices;
        for (std::size_t v = 0; v < skinning.size(); ++v) {
            const auto& skin = skinning[v];
            float headWeight = 0;
            for (std::size_t i = 0; i < skin.weights.size(); ++i)
                if (headJoint[skin.joints[i]]) headWeight += skin.weights[i];
            if (headWeight < 0.5f) continue;
            glm::vec4 position(0);
            for (std::size_t i = 0; i < skin.weights.size(); ++i) {
                if (skin.weights[i] == 0) continue;
                const auto joint = skin.joints[i];
                position += pose[joint] * asset.skeleton[joint].inverseBind *
                    glm::vec4(vertices[v].position, 1) * skin.weights[i];
            }
            const auto worldPosition = glm::vec3(instance.transform * position);
            low = glm::min(low, worldPosition);
            high = glm::max(high, worldPosition);
            found = true;
        }
    }
    if (!found) {
        // A recognized bone without weighted geometry still follows its pose.
        return {glm::vec3(instance.transform * pose[static_cast<std::size_t>(head)][3]) +
                    glm::vec3(0, 0.1f, 0), fallback.radius};
    }
    // Enclose the posed geometry, with a small aiming allowance in world meters.
    return {(low + high) * 0.5f, glm::length(high - low) * 0.5f + 0.02f};
}

} // namespace viewer
