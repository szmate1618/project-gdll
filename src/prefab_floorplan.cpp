#include "prefab_floorplan.hpp"
#include "apartment_layout.hpp"
#include "house_polygon.hpp"
#include "house_basement.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace viewer {
std::optional<PrefabBlock> classifyPrefabBlock(const Model& model, const HouseSource& source) {
    auto shell = building_shell::inspect(model, source);
    if (!shell || !shell->closed) return std::nullopt;
    const auto size = shell->high - shell->low;
    const float width = std::min(size.x, size.y), length = std::max(size.x, size.y);
    const float envelopeArea = width * length;
    const float area = shell->outline.empty() ? envelopeArea : house_polygon::area(shell->outline);
    const float height = shell->eaves - shell->floor;
    if (height < 12 - .001f || height > 50 + .001f || shell->roofTop - shell->eaves > .5f ||
        area < 180 || area > 2200 || area < envelopeArea * .8f) return std::nullopt;
    const bool slab = width >= 8 && width <= 22 && length >= 20 && length <= 120 && length >= width * 1.5f && length <= width * 8;
    const bool tower = width >= 12 && length <= 35 && length <= width * 1.8f;
    if (!slab && !tower) return std::nullopt;
    const auto storeys = static_cast<std::size_t>(std::clamp(std::lround(height / 3), 4l, 16l));
    return PrefabBlock{std::move(*shell), storeys, slab};
}
std::size_t prefabDoorCapacity(const PrefabBlock& block, std::size_t exteriorDoors) {
    const auto size = block.shell.high - block.shell.low;
    // A flat is >=3 m along either axis. Include ground-entrance splits and
    // common-space remnants without generating any apartment layouts at startup.
    return block.storeys * (2 * static_cast<std::size_t>(std::ceil(std::max(size.x, size.y) / 3)) + 8) + exteriorDoors * 6;
}
std::optional<HouseFloorplan> generatePrefabFloorplan(const Model& model, const HouseSource& source,
    const std::vector<DoorPanel>& entrances, const PrefabBlock& block) {
    if (source.doors.empty()) return std::nullopt;
    const auto& shell = block.shell;
    HouseFloorplan result;
    result.kind = HouseFloorplan::Kind::PrefabApartmentBlock;
    result.name = source.name; result.floor = shell.floor; result.ceiling = shell.eaves - .02f;
    // Classification is shell-only. Window heads may align the already chosen
    // number of storeys during layout fitting; missing panes use shell heights.
    const auto windowFaces = building_shell::faces(model, source.windows);
    const auto heads = building_shell::windowHeads(windowFaces);
    bool align = heads.size() == block.storeys && std::abs(heads.front() - result.floor - 2.1f) < .15f;
    if (align) for (std::size_t i = 1; i < heads.size(); ++i)
        align &= heads[i] - heads[i - 1] >= 2.4f && heads[i] - heads[i - 1] <= 4.2f;
    if (align) align &= result.ceiling - (result.floor + heads.back() - heads.front()) >= 2.4f;
    for (std::size_t i = 1; i < block.storeys; ++i)
        result.upperFloors.push_back(align ? result.floor + heads[i] - heads.front() :
            result.floor + (shell.eaves - result.floor) * float(i) / float(block.storeys));
    std::vector<house_layout::Rectangle> windows;
    for (const auto& face : windowFaces) {
        house_layout::Rectangle bounds{glm::vec2(std::numeric_limits<float>::max()), glm::vec2(std::numeric_limits<float>::lowest())};
        for (const auto p : face) {
            const auto q = shell.local({p.x, p.z}); bounds.low = glm::min(bounds.low, q); bounds.high = glm::max(bounds.high, q);
        }
        windows.push_back(bounds);
        const float bottom = std::min({face[0].y, face[1].y, face[2].y}), top = std::max({face[0].y, face[1].y, face[2].y});
        for (const float floor : result.upperFloors) if (bottom < floor - .02f && top > floor + .02f) return std::nullopt;
    }
    std::vector<house_layout::Entrance> entryDoors;
    for (const auto id : source.doors) {
        const auto& p = entrances.at(id);
        if (isBasementEntrance(p, result.floor)) continue;
        const glm::vec2 normal(p.outward.x, p.outward.z);
        const auto center = shell.local({p.center.x, p.center.z});
        const glm::vec2 outward(glm::dot(normal, shell.u), glm::dot(normal, shell.v));
        if (std::max(std::abs(outward.x), std::abs(outward.y)) < .99f) return std::nullopt;
        entryDoors.push_back({id, center, outward, std::min(1.f, p.halfExtents.x * 2)});
    }
    auto layout = apartment_layout::generate(source.name, {shell.low, shell.high}, shell.outline, block.slab,
        result.floor, result.upperFloors, entryDoors, windows);
    if (!layout) return std::nullopt;
    addHouseBasement(result, *layout, source, entrances, shell);
    house_layout::buildGeometry(result, source, *layout, shell.origin, shell.u, shell.v, {shell.low, shell.high}, shell.outline);
    if (result.doors.size() > prefabDoorCapacity(block, source.doors.size())) throw std::logic_error("Prefab door reservation was exceeded");
    result.minimum = glm::vec3(std::numeric_limits<float>::max()); result.maximum = -result.minimum;
    for (const auto p : result.envelope) {
        result.minimum = glm::min(result.minimum, glm::vec3(p.x, result.basementFloor.value_or(result.floor), p.y));
        result.maximum = glm::max(result.maximum, glm::vec3(p.x, result.ceiling, p.y));
    }
    result.structure.boundsMin = result.minimum; result.structure.boundsMax = result.maximum;
    return result;
}
} // namespace viewer
