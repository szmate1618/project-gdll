#include "tree_depth_order.hpp"

#include <array>

namespace viewer {

void TreeDepthOrder::sort(std::vector<std::size_t>& instances,
                         const std::vector<VisibilityBounds>& bounds,
                         glm::vec3 eye, glm::vec3 forward) {
    if (instances.size() < 2) return;
    constexpr std::size_t bandCount = 128;
    constexpr float bandWidth = 8;
    std::array<std::size_t, bandCount> offsets{};
    bands_.resize(instances.size());
    for (std::size_t i = 0; i < instances.size(); ++i) {
        const auto& box = bounds.at(instances[i]);
        const auto center = box.minimum*.5f + box.maximum*.5f;
        const float depth = glm::dot(center-eye, forward);
        const auto band = !(depth > 0) ? std::size_t{0}
            : depth >= bandWidth*(bandCount-1) ? bandCount-1
            : static_cast<std::size_t>(depth/bandWidth);
        bands_[i] = static_cast<std::uint8_t>(band);
        ++offsets[band];
    }
    // No scatter/copy is needed if all candidates are in the same band.
    if (offsets[bands_.front()] == instances.size()) return;
    std::size_t first = 0;
    for (auto& offset : offsets) {
        const auto count = offset;
        offset = first;
        first += count;
    }
    scratch_.resize(instances.size());
    for (std::size_t i = 0; i < instances.size(); ++i)
        scratch_[offsets[bands_[i]]++] = instances[i];
    instances.swap(scratch_);
}

} // namespace viewer
