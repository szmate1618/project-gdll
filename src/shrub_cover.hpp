#pragma once

#include <cstddef>
#include <memory>

#include <glm/glm.hpp>

namespace viewer {
struct TreeLayer;

// Immutable sight-only geometry for leafy/dry shrubs. Owns shared asset alpha
// masks and rest-card geometry; render LoD, culling and visual blast movement
// never change cover. Coordinates are world X-east/Y-up/Z-south meters.
class ShrubCover {
public:
    explicit ShrubCover(const TreeLayer& trees);
    ~ShrubCover();
    ShrubCover(ShrubCover&&) noexcept;
    ShrubCover& operator=(ShrubCover&&) noexcept;

    std::size_t size() const;
    // Double-sided finite rays sample native alpha texels, matching the default
    // nearest-filtered shrub cards. Ignore the first/last millimeter.
    bool blocksSight(glm::vec3 origin, glm::vec3 target) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace viewer
