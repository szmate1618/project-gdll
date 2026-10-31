#include "house_layout.hpp"
#include "house_layout_rules.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <limits>

namespace viewer::house_layout {
using namespace rules;

Layout generate(const std::string& building, Rectangle footprint, const std::vector<Entrance>& entrances,
                float floor, const std::vector<float>& upperFloors, const std::vector<Rectangle>& windows) {
    Layout result;
    std::map<int, std::vector<Entrance>> sides;
    float maxRise = 0, previous = floor;
    for (const auto y : upperFloors) { maxRise = std::max(maxRise, y - previous); previous = y; }
    for (const auto& door : entrances) {
        const int axis = std::abs(door.outward.x) > std::abs(door.outward.y) ? 0 : 1;
        const bool positive = door.outward[axis] > 0;
        if (std::abs(door.outward[axis]) < .99f ||
            std::abs(door.center[axis] - (positive ? footprint.high[axis] : footprint.low[axis])) > .15f) return {};
        sides[axis * 2 + int(positive)].push_back(door);
    }
    auto living = footprint;
    for (const auto& side : sides) {
        const int axis = side.first / 2, along = 1 - axis;
        const bool positive = side.first % 2;
        std::vector<Entrance> selected;
        for (const auto& door : side.second) {
            Random random(building, 0x656e747279ull ^ door.id);
            if (random.pick(100) < 95) selected.push_back(door);
        }
        if (selected.empty()) continue;
        auto entry = living, remaining = living;
        Partition wall;
        const float minLiving = std::max(2.5f, 10.f / (living.high[along] - living.low[along]));
        const float preferred = positive ? footprint.high[axis] - 2 : footprint.low[axis] + 2;
        const float minimum = positive ? living.low[axis] + minLiving : footprint.low[axis] + 1.2f;
        const float maximum = positive ? footprint.high[axis] - 1.2f : living.high[axis] - minLiving;
        bool placed = false;
        for (const float cut : partitionPositions(axis, minimum, maximum, preferred, living.low[along], living.high[along], windows)) {
            entry = remaining = living;
            if (positive) { entry.low[axis] = cut; remaining.high[axis] = cut; }
            else { entry.high[axis] = cut; remaining.low[axis] = cut; }
            if (!usable(remaining) || !splitClearsDoors({HouseRoom::Kind::Living, living, {}}, axis, cut, result.partitions, entrances)) continue;
            wall = {axis, cut, entry.low[along], entry.high[along], {}};
            bool clear = true;
            for (const auto& door : selected) {
                const float a = door.center[along] - door.width * .5f, b = a + door.width;
                if (a < entry.low[along] + .12f || b > entry.high[along] - .12f) { clear = false; break; }
                wall.openings.push_back({a, b, door.outward, door.id});
            }
            // An adjacent entry strip must not swallow a previously placed access.
            for (const auto& existing : result.partitions) for (const auto& door : existing.openings) {
                if (!door.entrance) continue;
                const auto p = point(existing.axis, existing.position, (door.low + door.high) * .5f);
                if (!contains(remaining, p)) clear = false;
            }
            if (!clear) continue;
            auto trialWalls = result.partitions; trialWalls.push_back(wall);
            const std::vector<Room> trialRooms{{HouseRoom::Kind::Living, remaining, {}}};
            if (maxRise > 0 && stairOptions(trialRooms, trialWalls, entrances, footprint, maxRise).empty()) continue;
            placed = true; break;
        }
        if (!placed) continue;
        living = remaining;
        result.partitions.push_back(std::move(wall));
        auto anteroom = entry;
        for (const bool right : {false, true}) {
            Random random(building, (right ? 0x7269676874ull : 0x6c656674ull) ^ side.second.front().id);
            const bool chosen = random.pick(100) < 50; // Independent, even when the other side cannot fit.
            if (!chosen || anteroom.high[along] - anteroom.low[along] < 3.5f ||
                anteroom.high[axis] - anteroom.low[axis] < 1.5f) continue;
            auto small = anteroom;
            const float preferredSide = right ? small.high[along] - 2 : small.low[along] + 2;
            const float minSide = anteroom.low[along] + (right ? 2.f : 1.5f);
            const float maxSide = anteroom.high[along] - (right ? 1.5f : 2.f);
            bool fits = false;
            for (const float position : partitionPositions(along, minSide, maxSide, preferredSide,
                                                           small.low[axis], small.high[axis], windows)) {
                small = anteroom;
                if (right) small.low[along] = position; else small.high[along] = position;
                // Keep every exterior door and its aligned internal portal in
                // the anteroom, including doors whose own entry roll failed.
                fits = true;
                for (const auto& door : side.second) {
                    if (door.center[along] + door.width * .5f + .25f > small.low[along] &&
                        door.center[along] - door.width * .5f - .25f < small.high[along]) fits = false;
                }
                if (fits) break;
            }
            if (!fits) continue;
            const float position = right ? small.low[along] : small.high[along];
            glm::vec2 outward(0); outward[along] = right ? -1.f : 1.f;
            const float center = (small.low[axis] + small.high[axis]) * .5f;
            result.partitions.push_back({along, position, small.low[axis], small.high[axis],
                                         {{center - .4f, center + .4f, outward, std::nullopt, false}}});
            result.rooms.push_back({HouseRoom::Kind::SmallRoom, small, {}});
            if (right) anteroom.high[along] = position; else anteroom.low[along] = position;
        }
        std::vector<std::size_t> exteriorIds;
        for (const auto& door : side.second) exteriorIds.push_back(door.id);
        result.rooms.push_back({HouseRoom::Kind::Anteroom, anteroom, std::move(exteriorIds)});
    }
    std::vector<Room> livingRooms{{HouseRoom::Kind::Living, living, {}}};
    Random random(building, 0x6c6976696e67ull);
    subdivideLiving(livingRooms, result.partitions, random, footprint, entrances, windows, maxRise);
    for (auto& room : livingRooms) for (const auto& door : entrances)
        if (contains(room.bounds, door.center)) room.entrances.push_back(door.id);
    const auto firstLiving = result.rooms.size();
    result.rooms.insert(result.rooms.end(), livingRooms.begin(), livingRooms.end());
    if (maxRise > 0) {
        auto choices = stairOptions(livingRooms, result.partitions, entrances, footprint, maxRise);
        if (choices.empty()) return {};
        if (std::any_of(choices.begin(), choices.end(), [](const StairOption& s) { return s.exterior; }))
            choices.erase(std::remove_if(choices.begin(), choices.end(), [](const StairOption& s) { return !s.exterior; }), choices.end());
        std::vector<std::size_t> rooms;
        for (const auto& choice : choices) if (std::find(rooms.begin(), rooms.end(), choice.room) == rooms.end()) rooms.push_back(choice.room);
        Random stairRandom(building, 0x737461697273ull);
        const auto chosenRoom = rooms[stairRandom.pick(static_cast<std::uint32_t>(rooms.size()))];
        choices.erase(std::remove_if(choices.begin(), choices.end(), [&](const StairOption& s) { return s.room != chosenRoom; }), choices.end());
        const auto chosen = choices[stairRandom.pick(static_cast<std::uint32_t>(choices.size()))];
        result.stairs.push_back(makeStair(chosen, firstLiving + chosen.room, floor + .01f, upperFloors.front()));
    }
    return result;
}
} // namespace viewer::house_layout
