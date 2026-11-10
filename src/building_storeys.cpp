#include "building_storeys.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace viewer {
std::optional<BuildingStoreys> inferBuildingStoreys(float eaves, const std::vector<float>& heads,
        const HouseSource& source, const std::vector<DoorPanel>& entrances) {
    if (source.doors.empty()) return {};
    float doorHead = std::numeric_limits<float>::lowest();
    for (const auto id : source.doors)
        doorHead = std::max(doorHead, entrances.at(id).center.y + entrances.at(id).halfExtents.y);
    // Generated main doors and the first window row share the 2.1 m datum.
    // Sloping wall bases and lower basement leaves do not change it.
    const float floor = doorHead - 2.1f;
    if ((!heads.empty() && std::abs(heads.front() - doorHead) > .15f) || eaves - floor < 2.4f || eaves - floor > 10) return {};
    BuildingStoreys result{floor, eaves - .02f, {}};
    for (std::size_t i = 1; i < heads.size(); ++i) {
        const float spacing = heads[i] - heads[i - 1];
        if (spacing < 2.4f || spacing > 4.2f) return {};
        result.upperFloors.push_back(floor + heads[i] - heads.front());
    }
    const float ceiling = result.upperFloors.empty() ? result.ceiling : result.upperFloors.front();
    if (ceiling - floor < 2.4f) return {};
    return result;
}
}
