#include "house_layout.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <limits>

namespace viewer::house_layout {
namespace {
class Random {
public:
    Random(const std::string& building, std::uint64_t domain) : state_(14695981039346656037ull) {
        for (const unsigned char c : building) { state_ ^= c; state_ *= 1099511628211ull; }
        state_ ^= domain;
    }
    std::uint32_t pick(std::uint32_t count) {
        state_ += 0x9e3779b97f4a7c15ull;
        auto x = state_;
        x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
        x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
        return static_cast<std::uint32_t>((x ^ (x >> 31)) >> 32) % count;
    }
private:
    std::uint64_t state_;
};
float area(Rectangle r) { const auto d = r.high - r.low; return d.x * d.y; }
bool usable(Rectangle r) {
    const auto size = r.high - r.low;
    return std::min(size.x, size.y) >= 2.5f && area(r) >= 10.f;
}
glm::vec2 point(int axis, float position, float along) {
    glm::vec2 p; p[axis] = position; p[1 - axis] = along; return p;
}
bool contains(Rectangle r, glm::vec2 p, float margin = .01f) {
    return glm::all(glm::greaterThanEqual(p, r.low - margin)) && glm::all(glm::lessThanEqual(p, r.high + margin));
}
// Test the finite partition, including its endpoints, against each pane's
// horizontal bounds. Windows on other storeys are excluded by the caller.
bool clearsWindows(int axis, float cut, float low, float high, const std::vector<Rectangle>& windows) {
    const int along = 1 - axis;
    for (const auto& window : windows) {
        const float across = std::max({window.low[axis] - cut, cut - window.high[axis], 0.f});
        const float beside = std::max({window.low[along] - high, low - window.high[along], 0.f});
        if (across * across + beside * beside < .2f * .2f) return false;
    }
    return true;
}
std::vector<float> partitionPositions(int axis, float low, float high, float preferred,
                                      float start, float end, const std::vector<Rectangle>& windows) {
    if (low > high) return {};
    std::vector<float> positions{glm::clamp(preferred, low, high), low, high};
    // Every free interval has an endpoint at a pane edge plus clearance, or
    // at a room limit. Consider those as well as the preferred position so a
    // narrow usable gap is found without a grid or random retry limit.
    constexpr float margin = .201f; // 20 cm plus 1 mm for world-space float rounding.
    for (const auto& window : windows) {
        const int along = 1 - axis;
        const float beside = std::max({window.low[along] - end, start - window.high[along], 0.f});
        if (beside >= margin) continue;
        const float reach = std::sqrt(margin * margin - beside * beside);
        for (const float edge : {window.low[axis] - reach, window.high[axis] + reach})
            if (edge >= low && edge <= high) positions.push_back(edge);
    }
    std::sort(positions.begin(), positions.end(), [&](float a, float b) {
        const float da = std::abs(a - preferred), db = std::abs(b - preferred);
        return da == db ? a < b : da < db;
    });
    positions.erase(std::unique(positions.begin(), positions.end()), positions.end());
    positions.erase(std::remove_if(positions.begin(), positions.end(), [&](float cut) {
        return !clearsWindows(axis, cut, start, end, windows);
    }), positions.end());
    return positions;
}
std::vector<glm::vec2> accessPoints(const std::vector<Partition>& partitions, const std::vector<Entrance>& entrances) {
    std::vector<glm::vec2> result;
    for (const auto& door : entrances) result.push_back(door.center);
    for (const auto& wall : partitions) for (const auto& door : wall.openings)
        result.push_back(point(wall.axis, wall.position, (door.low + door.high) * .5f));
    return result;
}
struct StairOption { std::size_t room; Rectangle ramp; int axis; bool ascending, exterior; };
std::vector<StairOption> stairOptions(const std::vector<Room>& rooms, const std::vector<Partition>& partitions,
                                    const std::vector<Entrance>& entrances, Rectangle footprint, float rise) {
    std::vector<StairOption> result;
    const auto accesses = accessPoints(partitions, entrances);
    constexpr float width = 1.1f, landing = .8f, inset = .06f;
    for (std::size_t room = 0; room < rooms.size(); ++room) {
        if (rooms[room].kind != HouseRoom::Kind::Living) continue;
        const auto bounds = rooms[room].bounds;
        for (int axis = 0; axis < 2; ++axis) {
            const int across = 1 - axis;
            const float length = bounds.high[axis] - bounds.low[axis];
            const float run = std::min(rise * 1.75f, length - 2 * (landing + inset));
            // A capsule walker supports slopes up to 45 degrees. Leave a small
            // margin and a body-width landing at each end of the straight ramp.
            if (run < rise / .98f || bounds.high[across] - bounds.low[across] < width + 2 * inset) continue;
            for (const bool farSide : {false, true}) for (const bool ascending : {false, true}) {
                auto ramp = bounds;
                ramp.low[axis] += landing + inset; ramp.high[axis] = ramp.low[axis] + run;
                if (farSide) { ramp.high[across] -= inset; ramp.low[across] = ramp.high[across] - width; }
                else { ramp.low[across] += inset; ramp.high[across] = ramp.low[across] + width; }
                auto occupied = ramp; occupied.low[axis] -= landing; occupied.high[axis] += landing;
                bool clear = true;
                for (const auto p : accesses) {
                    if (!contains(bounds, p)) continue;
                    const auto d = p - glm::clamp(p, occupied.low, occupied.high);
                    if (glm::dot(d, d) < .55f * .55f) { clear = false; break; }
                }
                if (!clear) continue;
                const float edge = farSide ? bounds.high[across] : bounds.low[across];
                const float outer = farSide ? footprint.high[across] : footprint.low[across];
                result.push_back({room, ramp, axis, ascending, std::abs(edge - outer) < .1f});
            }
        }
    }
    return result;
}
bool splitClearsDoors(const Room& room, int axis, float cut, const std::vector<Partition>& walls,
                      const std::vector<Entrance>& entrances) {
    const auto blocks = [&](glm::vec2 center, float halfWidth) {
        return contains(room.bounds, center) && std::abs(center[axis] - cut) < halfWidth + .2f;
    };
    for (const auto& door : entrances) {
        const int wallAxis = std::abs(door.outward.x) > std::abs(door.outward.y) ? 0 : 1;
        if (wallAxis != axis && blocks(door.center, door.width * .5f)) return false;
    }
    for (const auto& wall : walls) if (wall.axis != axis) for (const auto& door : wall.openings)
        if (blocks(point(wall.axis, wall.position, (door.low + door.high) * .5f), (door.high - door.low) * .5f)) return false;
    return true;
}
} // namespace

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
    const std::size_t target = 1 + random.pick(3);
    while (livingRooms.size() < target) {
        struct Split { std::vector<Room> rooms; Partition wall; };
        std::vector<Split> choices;
        for (std::size_t i = 0; i < livingRooms.size(); ++i) for (int axis = 0; axis < 2; ++axis) {
            const auto& room = livingRooms[i]; const auto size = room.bounds.high - room.bounds.low;
            for (int attempt = 0; attempt < 12; ++attempt) {
                const float fraction = .38f + .24f * (random.pick(1001) / 1000.f);
                const float preferredCut = room.bounds.low[axis] + size[axis] * fraction;
                const int along = 1 - axis;
                const float minSize = std::max(2.5f, 10.f / size[along]);
                for (const float cut : partitionPositions(axis, room.bounds.low[axis] + minSize,
                        room.bounds.high[axis] - minSize, preferredCut, room.bounds.low[along], room.bounds.high[along], windows)) {
                    auto first = room, second = room;
                    first.bounds.high[axis] = cut; second.bounds.low[axis] = cut;
                    if (!usable(first.bounds) || !usable(second.bounds) || !splitClearsDoors(room, axis, cut, result.partitions, entrances)) continue;
                    const float center = (room.bounds.low[along] + room.bounds.high[along]) * .5f;
                    glm::vec2 outward(0); outward[axis] = -1;
                    Partition wall{axis, cut, room.bounds.low[along], room.bounds.high[along],
                                   {{center - .45f, center + .45f, outward, std::nullopt}}};
                    auto rooms = livingRooms; rooms[i] = first; rooms.push_back(second);
                    auto walls = result.partitions; walls.push_back(wall);
                    if (maxRise > 0 && stairOptions(rooms, walls, entrances, footprint, maxRise).empty()) continue;
                    choices.push_back({std::move(rooms), std::move(wall)});
                    break;
                }
            }
        }
        if (choices.empty()) break;
        auto& split = choices[random.pick(static_cast<std::uint32_t>(choices.size()))];
        livingRooms = std::move(split.rooms); result.partitions.push_back(std::move(split.wall));
    }
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
        float bottom = floor + .01f;
        for (const auto top : upperFloors) {
            auto opening = chosen.ramp;
            const float run = chosen.ramp.high[chosen.axis] - chosen.ramp.low[chosen.axis];
            // Cut the complete standing head envelope, not just the endpoint.
            const float closedRun = run * std::max(0.f, (top - bottom - 2.05f) / (top - bottom));
            if (chosen.ascending) opening.low[chosen.axis] += closedRun;
            else opening.high[chosen.axis] -= closedRun;
            opening.low -= glm::vec2(.04f); opening.high += glm::vec2(.04f);
            if (chosen.ascending) opening.high[chosen.axis] = chosen.ramp.high[chosen.axis];
            else opening.low[chosen.axis] = chosen.ramp.low[chosen.axis];
            result.stairs.push_back({firstLiving + chosen.room, chosen.ramp, opening, chosen.axis, chosen.ascending, bottom, top});
            bottom = top;
        }
    }
    return result;
}
} // namespace viewer::house_layout
