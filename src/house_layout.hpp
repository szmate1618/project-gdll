#pragma once

#include "house_floorplan.hpp"
#include <array>

namespace viewer::house_layout {
// Temporary layout coordinates use the house's two horizontal wall axes.
struct Rectangle { glm::vec2 low{0}, high{0}; };
struct Entrance { std::size_t id; glm::vec2 center, outward; float width; };
struct Room {
    HouseRoom::Kind kind;
    Rectangle bounds;
    std::vector<std::size_t> entrances;
    std::size_t storey = 0;
    std::vector<glm::vec2> polygon{}; // Optional pre-clipped boundary for apartment/common-space regions.
};
struct Opening {
    float low, high;
    glm::vec2 outward;
    std::optional<std::size_t> entrance;
    bool inward = true; // False swings toward outward (out of a small room).
    float height = 2.1f; // Palace doorways can be taller than domestic doors.
};
struct Partition {
    int axis;
    float position, low, high;
    std::vector<Opening> openings;
    std::size_t storey = 0;
};
struct Stair {
    std::size_t room;
    Rectangle ramp, opening;
    int axis;
    bool ascending;
    float bottom, top;
};
struct Layout {
    std::vector<Room> rooms;
    std::vector<Partition> partitions;
    std::vector<Stair> stairs;
    bool wholeGroundFloor = false; // Apartment regions can have nonrectangular bounds.
    // Optional disjoint simple polygons tiling an authored shell with courtyards.
    // Slabs use these regions instead of triangulating the outer boundary alone.
    std::vector<std::vector<glm::vec2>> floorRegions;
};
struct Window { Rectangle bounds; float bottom, top; };
inline constexpr std::size_t maxUpperRoomsPerSide = 4;
bool generateUpperStoreys(Layout& layout, const std::string& building, Rectangle footprint,
    const std::vector<float>& upperFloors, float ceiling, const std::vector<Window>& windows,
    const std::vector<glm::vec2>& outline = {});
// Integer-only random choices have a specified sequence, independent of the
// standard library, activation order, wall-clock time and process RNG state.
Layout generate(const std::string& building, Rectangle footprint, const std::vector<Entrance>& entrances,
                float floor, const std::vector<float>& upperFloors,
                const std::vector<Rectangle>& windows = {}, const std::vector<glm::vec2>& outline = {});
// Transform layout coordinates once into runtime X/Y/Z metres and build minimal
// partition, floor and ramp planes. Door IDs start at source.firstDoor.
void buildGeometry(HouseFloorplan& result, const HouseSource& source, const Layout& layout,
                   glm::vec2 origin, glm::vec2 u, glm::vec2 v, Rectangle footprint,
                   const std::vector<glm::vec2>& outline = {});
} // namespace viewer::house_layout
