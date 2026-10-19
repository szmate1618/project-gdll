#pragma once

#include "grenades.hpp"
#include "tree_layer.hpp"
#include "visibility.hpp"

namespace viewer {

struct VegetationBend {
    std::size_t instance = 0;
    glm::mat4 rest{1}, transform{1};
};

// Cosmetic world-space shear about the root; collision and source matrices stay
// unchanged. Every card in a shrub shares the same transform, including shadows.
class VegetationBlastMotion {
public:
    static constexpr float radius = 12, duration = .9f, maxSlope = .6f;
    VegetationBlastMotion() = default;
    explicit VegetationBlastMotion(const std::vector<TreeInstance>& instances);
    std::vector<VegetationBend> sample(const std::vector<GrenadeExplosion>& explosions) const;
private:
    struct Shrub { std::size_t instance; glm::mat4 rest; glm::vec3 center; };
    std::vector<Shrub> shrubs_;
    VisibilityIndex index_;
};

// Conservative bounds for every possible blast pose, in world meters (Y up).
VisibilityBounds vegetationBlastBounds(const TreeInstance& instance);

} // namespace viewer
