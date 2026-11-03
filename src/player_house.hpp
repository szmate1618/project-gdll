#pragma once

#include "building_footprints.hpp"

namespace viewer {

// Per-player, simulation-thread sample. An empty ID explicitly means outside.
// The first sample is immediate; subsequent samples are at least three seconds apart.
class PlayerHouseState {
public:
    void update(glm::vec3 feet, double seconds, const BuildingFootprints& buildings);
    const std::optional<std::string>& house() const { return house_; }
    bool outside() const { return !house_; }
private:
    std::optional<std::string> house_;
    std::optional<double> checkedSeconds_;
};

} // namespace viewer
