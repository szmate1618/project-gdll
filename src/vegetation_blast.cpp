#include "vegetation_blast.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace viewer {

VisibilityBounds vegetationBlastBounds(const TreeInstance& instance) {
    VisibilityBounds bounds{instance.boundsMin, instance.boundsMax};
    if (instance.blastResponsive) {
        const float rootY = instance.transform[3].y;
        const float reach = VegetationBlastMotion::maxSlope * std::max(
            std::abs(bounds.minimum.y - rootY), std::abs(bounds.maximum.y - rootY));
        const glm::vec3 padding(reach, 0, reach);
        bounds.minimum -= padding;
        bounds.maximum += padding;
    }
    return bounds;
}

VegetationBlastMotion::VegetationBlastMotion(const std::vector<TreeInstance>& instances) {
    std::vector<VisibilityBounds> bounds;
    for (std::size_t i = 0; i < instances.size(); ++i) {
        const auto& instance = instances[i];
        if (!instance.blastResponsive) continue;
        const glm::vec3 center = glm::vec3(instance.transform[3]) +
            glm::vec3(0, instance.dimensions.y * .5f, 0);
        shrubs_.push_back({i, instance.transform, center});
        bounds.push_back({center, center});
    }
    index_ = VisibilityIndex(bounds);
}

std::vector<VegetationBend> VegetationBlastMotion::sample(
    const std::vector<GrenadeExplosion>& explosions) const {
    std::unordered_map<std::size_t, glm::vec2> slopes;
    std::vector<std::size_t> nearby;
    for (const auto& explosion : explosions) {
        if (!std::isfinite(explosion.age) || explosion.age <= 0 || explosion.age >= duration ||
            !std::isfinite(explosion.position.x) || !std::isfinite(explosion.position.y) ||
            !std::isfinite(explosion.position.z)) continue;
        const float remaining = 1 - explosion.age / duration;
        const float pulse = maxSlope * std::sin(12 * explosion.age) *
            std::exp(-3 * explosion.age) * remaining * remaining;
        index_.query(VisibilityBounds{explosion.position - glm::vec3(radius),
                                     explosion.position + glm::vec3(radius)}, nearby);
        for (const auto index : nearby) {
            const glm::vec3 offset = shrubs_[index].center - explosion.position;
            const float distance = glm::length(offset);
            if (distance >= radius) continue;
            const glm::vec2 horizontal(offset.x, offset.z);
            const float length = glm::length(horizontal);
            // A blast directly under a shrub still gets a finite, stable push.
            const glm::vec2 direction = length > .001f ? horizontal / length : glm::vec2(1, 0);
            const float falloff = 1 - distance / radius;
            auto inserted = slopes.try_emplace(index, glm::vec2(0));
            inserted.first->second += direction * (pulse * falloff * falloff);
        }
    }
    std::vector<VegetationBend> result;
    result.reserve(slopes.size());
    for (const auto& entry : slopes) {
        glm::vec2 slope = entry.second;
        const float length = glm::length(slope);
        if (length > maxSlope) slope *= maxSlope / length;
        const auto& shrub = shrubs_[entry.first];
        auto transform = shrub.rest;
        const glm::vec4 shear(slope.x, 0, slope.y, 0);
        // Left-compose a world-space shear around the root. Preserve arbitrary
        // rotation/reflection/shear in the authoritative instance matrix.
        for (int column = 0; column < 3; ++column)
            transform[column] += shear * shrub.rest[column].y;
        result.push_back({shrub.instance, shrub.rest, transform});
    }
    return result;
}

} // namespace viewer
