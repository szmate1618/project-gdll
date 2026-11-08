#include "basic_floorplan.hpp"
#include "building_footprints.hpp"
#include "building_shell.hpp"
#include "house_polygon.hpp"
#include <algorithm>
#include <limits>

namespace viewer {
std::optional<HouseFloorplan> generateBasicFloorplan(const Model& model, const HouseSource& source,
        const std::vector<DoorPanel>& entrances, const BuildingFootprints& footprints) {
    const auto roof = building_shell::faces(model, source.roofs);
    const auto walls = building_shell::faces(model, source.walls);
    const auto* rings = footprints.rings(source.name);
    if (roof.empty() || walls.empty() || !rings || rings->empty()) return {};
    HouseFloorplan result;
    result.kind = HouseFloorplan::Kind::BasicBuilding;
    result.name = source.name;
    result.floor = std::numeric_limits<float>::lowest();
    // Wall foundations can follow sloped terrain. The indoor plane sits above
    // the highest base sample, matching the generator's foundation datum.
    std::vector<std::pair<glm::vec2, float>> bases;
    for (const auto& face : walls) for (const auto p : face) {
        const glm::vec2 xz(p.x, p.z);
        const auto found = std::find_if(bases.begin(), bases.end(), [&](const auto& base) {
            return glm::distance(base.first, xz) < .01f;
        });
        if (found == bases.end()) bases.emplace_back(xz, p.y);
        else found->second = std::min(found->second, p.y);
    }
    for (const auto& base : bases) result.floor = std::max(result.floor, base.second + .08f);
    for (const auto id : source.walls) if (model.draws.at(id).buildingFloor) {
        result.floor = *model.draws.at(id).buildingFloor; break;
    }
    result.ceiling = std::numeric_limits<float>::max();
    for (const auto& face : roof) for (const auto p : face) result.ceiling = std::min(result.ceiling, p.y);
    if (result.ceiling <= result.floor + .02f) return {};
    const auto outer = std::max_element(rings->begin(), rings->end(), [](const auto& a, const auto& b) {
        return house_polygon::area(a) < house_polygon::area(b);
    });
    result.footprint = *outer;
    for (auto it = rings->begin(); it != rings->end(); ++it)
        if (it != outer && house_polygon::contains(*outer, it->front())) result.courtyards.push_back(*it);
    result.minimum = glm::vec3(std::numeric_limits<float>::max()); result.maximum = -result.minimum;
    for (const auto& ring : *rings) for (const auto p : ring) {
        result.minimum = glm::min(result.minimum, {p.x, result.floor, p.y});
        result.maximum = glm::max(result.maximum, {p.x, result.ceiling, p.y});
    }
    const glm::vec2 low(result.minimum.x, result.minimum.z), high(result.maximum.x, result.maximum.z);
    result.envelope = {{low, {low.x, high.y}, high, {high.x, low.y}}};
    Primitive slab; slab.material = 0;
    for (auto face : roof) {
        for (auto& p : face) p.y = result.floor + .01f;
        const float up = glm::cross(face[1] - face[0], face[2] - face[0]).y;
        if (std::abs(up) < 1e-6f) continue;
        if (up < 0) std::swap(face[1], face[2]);
        HouseRoom room; room.kind = HouseRoom::Kind::Lobby;
        const auto first = static_cast<std::uint32_t>(slab.vertices.size());
        for (const auto p : face) {
            Vertex vertex; vertex.position = p; vertex.normal = {0, 1, 0};
            slab.vertices.push_back(vertex); room.polygon.emplace_back(p.x, p.z);
        }
        slab.indices.insert(slab.indices.end(), {first, first + 1, first + 2});
        for (const auto id : source.doors) {
            const auto& door = entrances.at(id);
            const auto point = door.center - door.outward * .2f;
            if (house_polygon::contains(room.polygon, {point.x, point.z})) room.entrances.push_back(id);
        }
        result.rooms.push_back(std::move(room));
    }
    if (slab.indices.empty()) return {};
    Material material; material.baseColor = {.36f, .30f, .23f, 1}; material.doubleSided = true;
    result.structure.materials.push_back(material);
    result.structure.primitives.push_back(std::move(slab));
    result.structure.draws.push_back({0, glm::mat4(1), "building_interior_floor"});
    result.structure.boundsMin = result.minimum; result.structure.boundsMax = result.maximum;
    return result;
}
}
