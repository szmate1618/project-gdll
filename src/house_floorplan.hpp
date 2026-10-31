#pragma once

#include "door_geometry.hpp"
#include <optional>

namespace viewer {
// Draw IDs refer to the immutable, already-loaded map. No generated-map metadata
// or saved floorplans are needed. All output coordinates are runtime X/Y/Z metres.
struct HouseSource {
    std::string name;
    std::vector<std::size_t> walls, roofs, windows, doors;
};
struct HouseRoom {
    enum class Kind { Entry, Living };
    Kind kind = Kind::Living;
    std::vector<glm::vec2> polygon; // X/Z metres; ground storey only.
    std::vector<std::size_t> entrances; // Exterior door IDs sharing this entry.
};
struct HouseInteriorDoor {
    std::size_t entrance; // Stable exterior ID; also identifies its generated counterpart.
    DoorPanel panel;
    Primitive geometry; // World-space plane in its closed pose.
};
struct HouseFloorplan {
    std::string name;
    glm::vec3 minimum{0}, maximum{0};
    float floor = 0, ceiling = 0;
    std::vector<float> upperFloors;
    std::vector<glm::vec2> footprint; // Original oriented rectangle, X/Z metres.
    std::vector<HouseRoom> rooms;
    Model structure; // Floors/ceilings and the simplest partition wall quads.
    std::vector<HouseInteriorDoor> doors;
};
// Conservative footprint heuristic: rectangular perimeter, 45..220 m²,
// sides 5..22 m, aspect <= 2.5. Reject courtyards, concavities and missing shells.
// Window row heads determine storeys; this function has no RNG or mutable inputs.
std::optional<HouseFloorplan> generateHouseFloorplan(const Model& model, const HouseSource& source,
                                                   const std::vector<DoorPanel>& entrances);
} // namespace viewer
