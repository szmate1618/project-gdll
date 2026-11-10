#include "low_rise_floorplan.hpp"
#include "apartment_layout.hpp"
#include "building_storeys.hpp"
#include "house_basement.hpp"
#include "house_polygon.hpp"
#include "open_building_layout.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace viewer {
std::optional<LowRiseBuilding> classifyLowRiseBuilding(const Model& model, const HouseSource& source) {
    auto shell = building_shell::inspect(model, source);
    if (!shell || !shell->closed) return {};
    const auto size = shell->high - shell->low;
    const float width = std::min(size.x, size.y), length = std::max(size.x, size.y);
    const float envelopeArea = width * length;
    const float area = shell->outline.empty() ? envelopeArea : house_polygon::area(shell->outline);
    const float height = shell->eaves - shell->floor;
    if (area < 180 || area > 2200 || area < envelopeArea * .8f || width < 8 || length > 120 ||
        length > width * 8 || height < 2.4f || height >= 12 - .001f) return {};
    auto variant = LowRiseLayout::OpenHall;
    if (width <= 22 && length >= 20 && length >= width * 1.5f) variant = LowRiseLayout::CorridorRooms;
    else if (width >= 12 && length <= 35 && length <= width * 1.8f) variant = LowRiseLayout::CompactRooms;
    return LowRiseBuilding{std::move(*shell), variant};
}
std::size_t lowRiseDoorCapacity(const LowRiseBuilding& building, std::size_t storeys, std::size_t exteriorDoors) {
    if (building.preferredLayout == LowRiseLayout::OpenHall) return 0;
    const auto size = building.shell.high - building.shell.low;
    // The reused corridor/landing templates keep the prefab's >=3 m bays and
    // entrance splits. Open halls add no internal doors.
    return storeys * (2 * static_cast<std::size_t>(std::ceil(std::max(size.x, size.y) / 3)) + 8) + exteriorDoors * 6;
}
std::optional<HouseFloorplan> generateLowRiseFloorplan(const Model& model, const HouseSource& source,
        const std::vector<DoorPanel>& entrances, const LowRiseBuilding& building) {
    const auto& shell = building.shell;
    const auto faces = building_shell::faces(model, source.windows);
    const auto storeys = inferBuildingStoreys(shell.eaves, building_shell::windowHeads(faces), source, entrances, 12);
    if (!storeys) return {};
    HouseFloorplan result;
    result.kind = HouseFloorplan::Kind::LowRiseBuilding;
    result.name = source.name; result.floor = storeys->floor; result.ceiling = storeys->ceiling;
    result.upperFloors = storeys->upperFloors;
    for (std::size_t level = 0; level <= result.upperFloors.size(); ++level)
        if (result.ceilingHeight(level) - (level == 0 ? result.floor : result.floorHeight(level)) < 2.4f) return {};
    std::vector<house_layout::Rectangle> windows;
    for (const auto& face : faces) {
        house_layout::Rectangle bounds{glm::vec2(std::numeric_limits<float>::max()), glm::vec2(std::numeric_limits<float>::lowest())};
        for (const auto p : face) {
            const auto q = shell.local({p.x, p.z});
            bounds.low = glm::min(bounds.low, q); bounds.high = glm::max(bounds.high, q);
        }
        windows.push_back(bounds);
        const float bottom = std::min({face[0].y, face[1].y, face[2].y}), top = std::max({face[0].y, face[1].y, face[2].y});
        for (const float floor : result.upperFloors) if (bottom < floor - .02f && top > floor + .02f) return {};
    }
    std::vector<house_layout::Entrance> accesses;
    bool axisAligned = true;
    for (const auto id : source.doors) {
        const auto& p = entrances.at(id);
        if (isBasementEntrance(p, result.floor)) continue;
        const glm::vec2 normal(p.outward.x, p.outward.z);
        const glm::vec2 outward(glm::dot(normal, shell.u), glm::dot(normal, shell.v));
        axisAligned &= std::max(std::abs(outward.x), std::abs(outward.y)) >= .99f;
        accesses.push_back({id, shell.local({p.center.x, p.center.z}), outward, std::min(1.f, p.halfExtents.x * 2)});
    }
    std::optional<house_layout::Layout> layout;
    if (axisAligned && building.preferredLayout != LowRiseLayout::OpenHall)
        layout = apartment_layout::generate(source.name, {shell.low, shell.high}, shell.outline,
            building.preferredLayout == LowRiseLayout::CorridorRooms, result.floor, result.upperFloors, accesses, windows);
    if (!layout) layout = generateOpenBuildingLayout(result, shell, accesses);
    if (!layout) return {};
    addHouseBasement(result, *layout, source, entrances, shell);
    // The reused topology describes circulation and independently accessible
    // rooms. Neither geometry nor a generic OSM building tag establishes use.
    for (auto& room : layout->rooms) {
        if (room.kind == HouseRoom::Kind::Apartment || room.kind == HouseRoom::Kind::Living) room.kind = HouseRoom::Kind::OpenRoom;
        if (room.kind == HouseRoom::Kind::StairArea && result.upperFloors.empty()) room.kind = HouseRoom::Kind::Lobby;
    }
    house_layout::buildGeometry(result, source, *layout, shell.origin, shell.u, shell.v, {shell.low, shell.high}, shell.outline);
    if (result.doors.size() > lowRiseDoorCapacity(building, result.upperFloors.size() + 1, source.doors.size()))
        throw std::logic_error("Low-rise door reservation was exceeded");
    result.minimum = glm::vec3(std::numeric_limits<float>::max()); result.maximum = -result.minimum;
    for (const auto p : result.envelope) {
        result.minimum = glm::min(result.minimum, {p.x, result.basementFloor.value_or(result.floor), p.y});
        result.maximum = glm::max(result.maximum, {p.x, result.ceiling, p.y});
    }
    result.structure.boundsMin = result.minimum; result.structure.boundsMax = result.maximum;
    return result;
}
}
