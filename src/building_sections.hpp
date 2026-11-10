#pragma once
#include "building_footprints.hpp"
#include "building_shell.hpp"
#include "house_layout.hpp"

namespace viewer {
struct BuildingSection {
    building_shell::Shell shell; // Own orthonormal frame; outline is exact, local X/Z metres.
};
struct SectionPassage {
    std::size_t first, second;
    glm::vec2 a, b; // Shared boundary in world X/Z metres. No physical wall is implied.
};
struct BuildingSections {
    std::vector<glm::vec2> footprint;
    std::vector<std::vector<glm::vec2>> courtyards;
    std::vector<BuildingSection> sections;
    std::vector<SectionPassage> passages;
};
// Exact vertical decomposition of a connected footprint, followed by convex
// wing merging. Holes remain voids; section frames may have different axes.
// Returns no result for disconnected, invalid or excessively complex inputs.
std::optional<BuildingSections> prepareBuildingSections(const Model& model, const HouseSource& source,
    const BuildingFootprints& footprints);
std::optional<std::pair<glm::vec2,glm::vec2>> sharedSectionBoundary(
    const std::vector<glm::vec2>& first, const std::vector<glm::vec2>& second);
}
