#include "house_layout_rules.hpp"

#include <algorithm>
#include <cmath>

namespace viewer::house_layout::rules {
Random::Random(const std::string& building, std::uint64_t domain) : state_(14695981039346656037ull) {
    for (const unsigned char c : building) { state_ ^= c; state_ *= 1099511628211ull; }
    state_ ^= domain;
}
std::uint32_t Random::pick(std::uint32_t count) {
    state_ += 0x9e3779b97f4a7c15ull;
    auto x = state_;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    return static_cast<std::uint32_t>((x ^ (x >> 31)) >> 32) % count;
}
float area(Rectangle r) { const auto d = r.high - r.low; return d.x * d.y; }
bool usable(Rectangle r) {
    const auto size = r.high - r.low;
    return std::min(size.x, size.y) >= 2.5f && area(r) >= 10.f;
}
glm::vec2 point(int axis, float position, float along) {
    glm::vec2 p; p[axis] = position; p[1 - axis] = along; return p;
}
bool contains(Rectangle r, glm::vec2 p, float margin) {
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
std::vector<StairOption> stairOptions(const std::vector<Room>& rooms, const std::vector<Partition>& partitions,
                                    const std::vector<Entrance>& entrances, Rectangle footprint, float rise, const std::vector<Rectangle>& blocked) {
    std::vector<StairOption> result;
    const auto accesses = accessPoints(partitions, entrances);
    constexpr float width = 1.1f, landing = .8f, inset = .06f;
    for (std::size_t room = 0; room < rooms.size(); ++room) {
        if (rooms[room].kind != HouseRoom::Kind::Living && rooms[room].kind != HouseRoom::Kind::StairArea) continue;
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
                for (const auto& obstacle : blocked) {
                    // Keep a standing player's corridor between the outgoing
                    // flight and the incoming opening/landing, not merely two
                    // nonintersecting ramp planes.
                    const auto overlap = glm::min(occupied.high + glm::vec2(.65f), obstacle.high) - glm::max(occupied.low - glm::vec2(.65f), obstacle.low);
                    if (overlap.x > 0 && overlap.y > 0) clear = false;
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
void subdivideLiving(std::vector<Room>& rooms, std::vector<Partition>& walls, Random& random,
    Rectangle footprint, const std::vector<Entrance>& entrances, const std::vector<Rectangle>& windows, float rise) {
    const std::size_t target = 1 + random.pick(3);
    while (rooms.size() < target) {
        struct Split { std::vector<Room> rooms; Partition wall; };
        std::vector<Split> choices;
        for (std::size_t i = 0; i < rooms.size(); ++i) for (int axis = 0; axis < 2; ++axis) {
            const auto& room = rooms[i]; const auto size = room.bounds.high - room.bounds.low;
            for (int attempt = 0; attempt < 12; ++attempt) {
                const float fraction = .38f + .24f * (random.pick(1001) / 1000.f);
                const float preferredCut = room.bounds.low[axis] + size[axis] * fraction;
                const int along = 1 - axis;
                const float minSize = std::max(2.5f, 10.f / size[along]);
                for (const float cut : partitionPositions(axis, room.bounds.low[axis] + minSize,
                        room.bounds.high[axis] - minSize, preferredCut, room.bounds.low[along], room.bounds.high[along], windows)) {
                    auto first = room, second = room;
                    first.bounds.high[axis] = cut; second.bounds.low[axis] = cut;
                    if (!usable(first.bounds) || !usable(second.bounds) || !splitClearsDoors(room, axis, cut, walls, entrances)) continue;
                    const float center = (room.bounds.low[along] + room.bounds.high[along]) * .5f;
                    glm::vec2 outward(0); outward[axis] = -1;
                    Partition wall{axis, cut, room.bounds.low[along], room.bounds.high[along],
                                   {{center - .45f, center + .45f, outward, std::nullopt}}};
                    auto trialRooms = rooms; trialRooms[i] = first; trialRooms.push_back(second);
                    auto trialWalls = walls; trialWalls.push_back(wall);
                    if (rise > 0 && stairOptions(trialRooms, trialWalls, entrances, footprint, rise).empty()) continue;
                    choices.push_back({std::move(trialRooms), std::move(wall)});
                    break;
                }
            }
        }
        if (choices.empty()) break;
        auto& split = choices[random.pick(static_cast<std::uint32_t>(choices.size()))];
        rooms = std::move(split.rooms); walls.push_back(std::move(split.wall));
    }
}
Stair makeStair(const StairOption& option, std::size_t room, float bottom, float top) {
    auto opening = option.ramp;
    const float run = option.ramp.high[option.axis] - option.ramp.low[option.axis];
    const float closedRun = run * std::max(0.f, (top - bottom - 2.05f) / (top - bottom));
    if (option.ascending) opening.low[option.axis] += closedRun;
    else opening.high[option.axis] -= closedRun;
    opening.low -= glm::vec2(.04f); opening.high += glm::vec2(.04f);
    if (option.ascending) opening.high[option.axis] = option.ramp.high[option.axis];
    else opening.low[option.axis] = option.ramp.low[option.axis];
    return {room, option.ramp, opening, option.axis, option.ascending, bottom, top};
}
} // namespace viewer::house_layout::rules
