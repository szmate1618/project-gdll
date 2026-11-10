#include "house_floorplan.hpp"
#include "house_layout.hpp"
#include "building_shell.hpp"
#include "building_storeys.hpp"
#include "house_basement.hpp"
#include "house_polygon.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace viewer {
bool HouseFloorplan::contains(glm::vec2 p) const {
    if (!house_polygon::contains(footprint, p)) return false;
    for (const auto& court : courtyards) if (house_polygon::contains(court, p)) return false;
    return true;
}
using building_shell::faces;
using building_shell::windowHeads;
std::size_t houseUpperStoreyCount(const Model& model, const HouseSource& source) {
    const auto heads = windowHeads(faces(model, source.windows));
    return heads.empty() ? 0 : heads.size() - 1;
}

std::optional<HouseFloorplan> generateHouseFloorplan(const Model& model, const HouseSource& source,
                                                   const std::vector<DoorPanel>& entrances) {
    if (source.doors.empty()) return std::nullopt;
    const auto geometry = building_shell::inspect(model, source);
    if (!geometry) return std::nullopt;
    const auto& shell = *geometry;
    const auto low = shell.low, high = shell.high, size = high - low;
    const float area = size.x * size.y;
    if (area < 45 || area > 350 || std::min(size.x, size.y) < 5 || std::max(size.x, size.y) > 30 ||
        std::max(size.x, size.y) > 3.f * std::min(size.x, size.y)) return std::nullopt;
    const auto local = [&](glm::vec2 p) { return shell.local(p); };
    const auto world = [&](glm::vec2 p, float y) { return shell.world(p, y); };
    const auto u = shell.u, v = shell.v;
    const float eaves = shell.eaves;
    const auto windowFaces = faces(model, source.windows);
    const auto heads = windowHeads(windowFaces);
    const auto storeys = inferBuildingStoreys(eaves, heads, source, entrances);
    if (!storeys) return std::nullopt;
    const float floor = storeys->floor;
    HouseFloorplan result;
    result.name = source.name; result.floor = floor; result.ceiling = storeys->ceiling;
    result.upperFloors = storeys->upperFloors;
    const float ceiling = result.upperFloors.empty() ? result.ceiling : result.upperFloors.front();
    std::vector<house_layout::Entrance> entryDoors;
    for (const auto id : source.doors) {
        const auto& door = entrances.at(id);
        if (isBasementEntrance(door, floor)) continue;
        entryDoors.push_back({id, local({door.center.x, door.center.z}),
            {glm::dot(glm::vec2(door.outward.x, door.outward.z), u),
             glm::dot(glm::vec2(door.outward.x, door.outward.z), v)},
            std::min(1.f, door.halfExtents.x * 2)});
    }
    std::vector<house_layout::Rectangle> windows;
    std::vector<house_layout::Window> allWindows;
    for (const auto& face : windowFaces) {
        const float bottom = std::min({face[0].y, face[1].y, face[2].y});
        const float top = std::max({face[0].y, face[1].y, face[2].y});
        house_layout::Rectangle bounds{glm::vec2(std::numeric_limits<float>::max()),
                                       glm::vec2(std::numeric_limits<float>::lowest())};
        for (const auto p : face) {
            const auto q = local({p.x, p.z});
            bounds.low = glm::min(bounds.low, q); bounds.high = glm::max(bounds.high, q);
        }
        allWindows.push_back({bounds, bottom, top});
        if (top > floor && bottom < ceiling) windows.push_back(bounds);
    }
    auto layout = house_layout::generate(source.name, {low, high}, entryDoors, floor, result.upperFloors, windows, shell.outline);
    if (layout.rooms.empty() || (!result.upperFloors.empty() && layout.stairs.empty())) return std::nullopt;
    if (!house_layout::generateUpperStoreys(layout, source.name, {low, high}, result.upperFloors, result.ceiling, allWindows, shell.outline)) return std::nullopt;
    addHouseBasement(result, layout, source, entrances, shell);
    house_layout::buildGeometry(result, source, layout, shell.origin, u, v, {low, high}, shell.outline);
    result.minimum = glm::vec3(std::numeric_limits<float>::max()); result.maximum = -result.minimum;
    for (const auto q : {low, glm::vec2(low.x, high.y), high, glm::vec2(high.x, low.y)}) {
        result.minimum = glm::min(result.minimum, world(q, result.basementFloor.value_or(floor)));
        result.maximum = glm::max(result.maximum, world(q, result.ceiling));
    }
    result.structure.boundsMin = result.minimum; result.structure.boundsMax = result.maximum;
    return result;
}
} // namespace viewer
