#include "small_building_floorplan.hpp"
#include "building_shell.hpp"
#include "building_storeys.hpp"
#include "house_basement.hpp"
#include "open_building_layout.hpp"
#include "house_polygon.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace viewer {
std::optional<HouseFloorplan> generateSmallBuildingFloorplan(const Model& model, const HouseSource& source,
        const std::vector<DoorPanel>& entrances) {
    if (source.doors.empty()) return {};
    const auto shell = building_shell::inspect(model, source, 25);
    if (!shell || !shell->closed) return {};
    const auto size = shell->high - shell->low;
    const float area = size.x * size.y, width = std::min(size.x, size.y), length = std::max(size.x, size.y);
    const float actualArea = shell->outline.empty() ? area : house_polygon::area(shell->outline);
    // World-space float32 transforms can move an exact 4 m side by a fraction
    // of a millimetre. Keep such shells on the inclusive dimension boundary.
    if (area < 25 || width < 4 - .001f || length > width * 2.5f + .001f ||
        (area >= 45 && actualArea >= 45 && width >= 5)) return {};
    const auto storeys = inferBuildingStoreys(shell->eaves,
        building_shell::windowHeads(building_shell::faces(model, source.windows)), source, entrances);
    if (!storeys) return {};
    HouseFloorplan result;
    result.kind = HouseFloorplan::Kind::SmallBuilding;
    result.name = source.name; result.floor = storeys->floor; result.ceiling = storeys->ceiling;
    result.upperFloors = storeys->upperFloors;
    const house_layout::Rectangle footprint{shell->low, shell->high};
    std::vector<house_layout::Entrance> accesses;
    for (const auto id : source.doors) {
        const auto& door = entrances.at(id);
        if (isBasementEntrance(door, result.floor)) continue;
        const glm::vec2 normal(door.outward.x, door.outward.z);
        accesses.push_back({id, shell->local({door.center.x, door.center.z}),
            {glm::dot(normal, shell->u), glm::dot(normal, shell->v)}, std::min(1.f, door.halfExtents.x * 2)});
    }
    auto layout = generateOpenBuildingLayout(result, *shell, accesses);
    if (!layout) return {};
    addHouseBasement(result, *layout, source, entrances, *shell);
    for (auto& room : layout->rooms) room.kind = HouseRoom::Kind::OpenRoom;
    house_layout::buildGeometry(result, source, *layout, shell->origin, shell->u, shell->v, footprint, shell->outline);
    result.minimum = glm::vec3(std::numeric_limits<float>::max()); result.maximum = -result.minimum;
    for (const auto p : result.envelope) {
        result.minimum = glm::min(result.minimum, {p.x, result.basementFloor.value_or(result.floor), p.y});
        result.maximum = glm::max(result.maximum, {p.x, result.ceiling, p.y});
    }
    result.structure.boundsMin = result.minimum; result.structure.boundsMax = result.maximum;
    return result;
}
}
