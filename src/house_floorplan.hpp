#pragma once

#include "door_geometry.hpp"
#include <optional>

namespace viewer {
// Draw IDs refer to the immutable, already-loaded map. No generated-map metadata
// or saved floorplans are needed. All output coordinates are runtime X/Y/Z metres.
struct HouseSource {
    std::string name;
    std::vector<std::size_t> walls, roofs, windows, doors;
    std::size_t firstDoor = 0; // Reserved runtime IDs; geometry is still generated lazily.
};
struct HouseRoom {
    enum class Kind { Anteroom, SmallRoom, Living };
    Kind kind = Kind::Living;
    std::vector<glm::vec2> polygon; // X/Z metres; ground storey only.
    std::vector<std::size_t> entrances; // Exterior door IDs opening into this room.
    bool staircase = false;
    const char* name() const {
        return kind == Kind::Anteroom ? "anteroom" : kind == Kind::SmallRoom ? "small_rooms" : "living";
    }
};
struct HouseInteriorDoor {
    std::size_t id; // Stable reserved DoorLayer ID, independent of streaming order.
    std::optional<std::size_t> entrance; // Exterior counterpart, if this is an entry partition door.
    DoorPanel panel;
    Primitive geometry; // World-space plane in its closed pose.
};
struct HouseStaircase {
    std::size_t room; // Index in HouseFloorplan::rooms; every flight uses this living room.
    std::vector<glm::vec2> ramp, opening; // Rectangles in world X/Z metres.
    glm::vec3 bottom, top; // Centreline endpoints on the walkable plane.
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
    std::vector<HouseStaircase> stairs;
};
// Conservative footprint heuristic: rectangular perimeter, 45..220 m²,
// sides 5..22 m, aspect <= 2.5. Reject courtyards, concavities and missing shells.
// Window row heads determine storeys; random decisions depend only on the stable building identity.
std::optional<HouseFloorplan> generateHouseFloorplan(const Model& model, const HouseSource& source,
                                                   const std::vector<DoorPanel>& entrances);
} // namespace viewer
