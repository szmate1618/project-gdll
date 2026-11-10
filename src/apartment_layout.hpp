#pragma once

#include "house_layout.hpp"

namespace viewer::apartment_layout {
// Coordinates follow the building axes. Upper floors share one apartment/common
// space template; ground-floor lobbies connect the existing exterior entrances.
// A single-storey caller retains the ground circulation without a stair flight.
std::optional<house_layout::Layout> generate(const std::string& building, house_layout::Rectangle bounds,
    const std::vector<glm::vec2>& outline, bool slab, float floor, const std::vector<float>& upperFloors,
    const std::vector<house_layout::Entrance>& entrances, const std::vector<house_layout::Rectangle>& windows);
} // namespace viewer::apartment_layout
