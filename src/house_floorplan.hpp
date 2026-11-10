#pragma once

#include "door_geometry.hpp"
#include <array>
#include <optional>
#include <limits>

namespace viewer {
// Draw IDs refer to the immutable, already-loaded map. Procedural houses need
// no saved floorplan; authored interiors use embedded map metadata. All output
// coordinates are runtime X/Y/Z metres.
struct HouseSource {
    std::string name;
    std::vector<std::size_t> walls, roofs, windows, doors;
    std::size_t firstDoor = 0; // Reserved runtime IDs; geometry is still generated lazily.
};
struct HouseRoom {
    enum class Kind { Anteroom, SmallRoom, Living, StairArea, Apartment, Corridor, Lobby, OpenRoom };
    Kind kind = Kind::Living;
    std::vector<glm::vec2> polygon; // X/Z metres on the indicated storey.
    std::vector<std::size_t> entrances; // Exterior door IDs opening into this room.
    bool staircase = false;
    std::size_t storey = 0; // Ground = 0; upper levels = 1..N; basement uses basementStorey.
    std::string label{}; // Optional evidence-based room name for authored layouts.
    const char* name() const {
        return kind == Kind::Anteroom ? "anteroom" : kind == Kind::SmallRoom ? "small_rooms" :
            kind == Kind::StairArea ? "stair_area" : kind == Kind::Apartment ? "apartment" :
            kind == Kind::Corridor ? "corridor" : kind == Kind::Lobby ? "lobby" : kind == Kind::OpenRoom ? "open_room" : "living";
    }
};
struct HouseInteriorDoor {
    std::size_t id; // Stable reserved DoorLayer ID, independent of streaming order.
    std::optional<std::size_t> entrance; // Exterior counterpart, if this is an entry partition door.
    DoorPanel panel;
    Primitive geometry; // World-space plane in its closed pose.
    std::size_t storey = 0;
};
struct HouseStaircase {
    std::size_t room; // Index of the room containing this flight's lower end.
    std::vector<glm::vec2> ramp, opening; // Rectangles in world X/Z metres.
    glm::vec3 bottom, top; // Centreline endpoints on the walkable plane.
};
struct HousePassage {
    std::size_t firstRoom, secondRoom;
    glm::vec2 a, b; // An open shared boundary in world X/Z metres, on both rooms' storey.
};
struct HouseInteriorVolume {
    std::vector<glm::vec2> footprint; // One section in world X/Z metres, without holes.
    float floor, ceiling; // Runtime Y metres; permits lower roofs beside taller wings.
};
struct HouseFloorplan {
    static constexpr std::size_t basementStorey = std::numeric_limits<std::size_t>::max();
    enum class Kind { FamilyHouse, PrefabApartmentBlock, AuthoredPalace, AuthoredBuilding, BasicBuilding, SmallBuilding, LowRiseBuilding, ComplexBuilding };
    Kind kind = Kind::FamilyHouse;
    std::string name;
    glm::vec3 minimum{0}, maximum{0};
    float floor = 0, ceiling = 0;
    std::vector<float> upperFloors;
    std::optional<float> basementFloor;
    float floorHeight(std::size_t storey) const {
        if (storey == basementStorey) return basementFloor.value() + .01f;
        return storey ? upperFloors.at(storey - 1) : floor + .01f;
    }
    float ceilingHeight(std::size_t storey) const {
        if (storey == basementStorey) return floor + .01f;
        return storey < upperFloors.size() ? upperFloors[storey] : ceiling;
    }
    std::vector<glm::vec2> footprint; // Actual outer shell boundary, X/Z metres.
    std::vector<std::vector<glm::vec2>> courtyards; // Open voids within authored shells.
    bool contains(glm::vec2 p) const; // Horizontal shell membership, excluding courtyards.
    bool containsInterior(glm::vec3 p) const;
    std::array<glm::vec2, 4> envelope{}; // Oriented rectangle: low, low/high, high, high/low.
    std::vector<HouseRoom> rooms;
    Model structure; // Floors/ceilings and the simplest partition wall quads.
    std::vector<HouseInteriorDoor> doors;
    std::vector<HouseStaircase> stairs;
    std::vector<HousePassage> passages; // Explicit open section seams; never physical walls.
    std::vector<HouseInteriorVolume> volumes; // Empty uses the established whole-building height range.
};
// Conservative footprint heuristic: approximately orthogonal walls, 45..350 m²
// envelope, sides 5..30 m, aspect <= 3, at least 75% envelope fill and 45 m²
// actual floor area. Reject courtyards, disconnected and missing shells.
// Window row heads determine storeys; random decisions depend only on the stable building identity.
std::optional<HouseFloorplan> generateHouseFloorplan(const Model& model, const HouseSource& source,
                                                   const std::vector<DoorPanel>& entrances);
// Window-head rows already loaded in the map, for reserving stable door IDs.
std::size_t houseUpperStoreyCount(const Model& model, const HouseSource& source);
} // namespace viewer
