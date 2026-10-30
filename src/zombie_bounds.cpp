#include "zombie_bounds.hpp"

#include <limits>

namespace viewer {

VisibilityBounds emptyZombieBounds() {
    return {glm::vec3(std::numeric_limits<float>::max()),
            glm::vec3(std::numeric_limits<float>::lowest())};
}

void includeZombieBounds(VisibilityBounds& target, const VisibilityBounds& other) {
    target.minimum = glm::min(target.minimum, other.minimum);
    target.maximum = glm::max(target.maximum, other.maximum);
}

void includeZombieGeometryBounds(const AnimatedModel& asset, VisibilityBounds& animation,
                                VisibilityBounds& bind) {
    for (const auto& primitive : asset.model.primitives)
        for (const auto& vertex : primitive.vertices) {
            bind.minimum = glm::min(bind.minimum, vertex.position);
            bind.maximum = glm::max(bind.maximum, vertex.position);
        }
    if (asset.bakedBounds) includeZombieBounds(animation, *asset.bakedBounds);
    else
        for (const auto& primitive : asset.animation)
            for (const auto& vertex : primitive.frames) {
                animation.minimum = glm::min(animation.minimum, glm::vec3(vertex.position));
                animation.maximum = glm::max(animation.maximum, glm::vec3(vertex.position));
            }
}

void releaseZombieAnimationVertices(AnimatedModel& asset) {
    if (!asset.bakedBounds) {
        auto animation = emptyZombieBounds(), bind = emptyZombieBounds();
        includeZombieGeometryBounds(asset, animation, bind);
        asset.bakedBounds = animation;
    }
    for (auto& primitive : asset.animation)
        std::vector<AnimationVertex>().swap(primitive.frames);
}

VisibilityBounds transformedZombieBounds(const VisibilityBounds& bounds, const glm::mat4& transform) {
    const auto center = glm::vec3(transform * glm::vec4((bounds.minimum + bounds.maximum) * .5f, 1));
    const auto half = (bounds.maximum - bounds.minimum) * .5f;
    const auto extent = glm::abs(glm::vec3(transform[0])) * half.x +
                        glm::abs(glm::vec3(transform[1])) * half.y +
                        glm::abs(glm::vec3(transform[2])) * half.z;
    return {center - extent, center + extent};
}

} // namespace viewer
