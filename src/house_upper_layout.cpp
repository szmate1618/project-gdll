#include "house_layout_rules.hpp"

#include <algorithm>
#include <optional>

namespace viewer::house_layout {
namespace {
using namespace rules;
struct Side { Room room; std::size_t wall; };
std::vector<Rectangle> incomingClearance(const Stair& stair) {
    auto landing = stair.ramp;
    if (stair.ascending) {
        landing.low[stair.axis] = stair.ramp.high[stair.axis];
        landing.high[stair.axis] += .8f;
    } else {
        landing.high[stair.axis] = stair.ramp.low[stair.axis];
        landing.low[stair.axis] -= .8f;
    }
    return {stair.opening, landing};
}
bool fitsNextStair(Rectangle core, const std::vector<Partition>& walls, Rectangle footprint,
                   float rise, const std::vector<Rectangle>& blocked) {
    return rise <= 0 || !stairOptions({{HouseRoom::Kind::StairArea, core, {}}}, walls, {}, footprint, rise, blocked).empty();
}
std::vector<Room> perpendicularRooms(const Side& side, int axis, Random& random,
    std::vector<Partition>& walls, Rectangle core, Rectangle footprint, float rise,
    const std::vector<Rectangle>& windows, const std::vector<Rectangle>& blocked) {
    const int along = 1 - axis;
    const auto bounds = side.room.bounds;
    const float minLength = std::max(2.5f, 10.f / (bounds.high[axis] - bounds.low[axis]));
    const auto maximum = std::min<std::size_t>(maxUpperRoomsPerSide,
        static_cast<std::size_t>((bounds.high[along] - bounds.low[along]) / minLength));
    const auto target = 1 + random.pick(static_cast<std::uint32_t>(std::max<std::size_t>(1, maximum)));
    // If doors or windows prevent the chosen count, retain as many usable
    // sections as fit. Each section has its own doorway into the stair area.
    for (std::size_t count = target; count > 0; --count) {
        auto trialWalls = walls;
        std::vector<Room> rooms;
        auto remaining = side.room;
        bool clear = true;
        for (std::size_t part = 1; part < count; ++part) {
            const float fraction = (.85f + random.pick(301) / 1000.f) / float(count - part + 1);
            const float preferred = remaining.bounds.low[along] +
                (bounds.high[along] - remaining.bounds.low[along]) * fraction;
            const auto positions = partitionPositions(along, remaining.bounds.low[along] + minLength,
                bounds.high[along] - minLength * float(count - part), preferred,
                bounds.low[axis], bounds.high[axis], windows);
            if (positions.empty()) { clear = false; break; }
            auto room = remaining;
            room.bounds.high[along] = positions.front(); remaining.bounds.low[along] = positions.front();
            if (!usable(room.bounds) || !usable(remaining.bounds)) { clear = false; break; }
            rooms.push_back(room);
            trialWalls.push_back({along, positions.front(), bounds.low[axis], bounds.high[axis], {}});
        }
        if (!clear) continue;
        rooms.push_back(remaining);
        auto& boundary = trialWalls[side.wall];
        const auto outward = boundary.openings.front().outward;
        boundary.openings.clear();
        for (const auto& room : rooms) {
            const float center = (room.bounds.low[along] + room.bounds.high[along]) * .5f;
            boundary.openings.push_back({center - .45f, center + .45f, outward, std::nullopt, false});
        }
        if (!fitsNextStair(core, trialWalls, footprint, rise, blocked)) continue;
        walls = std::move(trialWalls);
        return rooms;
    }
    return {side.room}; // The original single doorway was already validated.
}
} // namespace

bool generateUpperStoreys(Layout& layout, const std::string& building, Rectangle footprint,
    const std::vector<float>& upperFloors, float ceiling, const std::vector<Window>& allWindows) {
    if (upperFloors.empty()) return true;
    if (layout.stairs.empty()) return false;
    for (std::size_t level = 0; level < upperFloors.size(); ++level) {
        const auto storey = level + 1;
        const float floor = upperFloors[level];
        const float top = level + 1 < upperFloors.size() ? upperFloors[level + 1] : ceiling;
        if (top - floor < 2.4f) return false;
        const float rise = level + 1 < upperFloors.size() ? top - floor : 0;
        const auto incoming = layout.stairs.back();
        const auto blocked = incomingClearance(incoming);
        auto protectedArea = blocked.front();
        for (const auto& r : blocked) {
            protectedArea.low = glm::min(protectedArea.low, r.low);
            protectedArea.high = glm::max(protectedArea.high, r.high);
        }
        std::vector<Rectangle> windows;
        for (const auto& w : allWindows) if (w.bottom < top && w.top > floor) windows.push_back(w.bounds);
        const auto domain = std::uint64_t(storey) << 32;
        Random direction(building, 0x757070657244ull ^ domain);
        // axis is the cut normal; 1-axis is the wall direction D.
        const int axis = int(direction.pick(2)), along = 1 - axis;
        auto core = footprint;
        std::array<std::optional<Side>, 2> sides;
        std::vector<Partition> walls;
        if (!fitsNextStair(core, walls, footprint, rise, blocked)) return false;
        for (int side = 0; side < 2; ++side) {
            Random roll(building, 0x757070657243ull ^ domain ^ std::uint64_t(side));
            if (roll.pick(100) >= 70) continue;
            const bool highSide = side == 1;
            const float minLiving = std::max(2.5f, 10.f / (core.high[along] - core.low[along]));
            const float minimum = highSide ? protectedArea.high[axis] + .2f : footprint.low[axis] + minLiving;
            const float maximum = highSide ? footprint.high[axis] - minLiving : protectedArea.low[axis] - .2f;
            const float preferred = highSide ? incoming.ramp.high[axis] + 2 : incoming.ramp.low[axis] - 2;
            for (const float cut : partitionPositions(axis, minimum, maximum, preferred, core.low[along], core.high[along], windows)) {
                auto living = core, nextCore = core;
                if (highSide) { living.low[axis] = cut; nextCore.high[axis] = cut; }
                else { living.high[axis] = cut; nextCore.low[axis] = cut; }
                if (!usable(living)) continue;
                glm::vec2 outward(0); outward[axis] = highSide ? -1.f : 1.f;
                const float center = (core.low[along] + core.high[along]) * .5f;
                auto trialWalls = walls;
                trialWalls.push_back({axis, cut, core.low[along], core.high[along],
                    {{center - .45f, center + .45f, outward, std::nullopt, false}}});
                if (!fitsNextStair(nextCore, trialWalls, footprint, rise, blocked)) continue;
                sides[side] = Side{{HouseRoom::Kind::Living, living, {}, storey}, walls.size()};
                walls = std::move(trialWalls); core = nextCore;
                break;
            }
        }
        const auto stairRoom = layout.rooms.size();
        layout.rooms.push_back({HouseRoom::Kind::StairArea, core, {}, storey});
        for (int side = 0; side < 2; ++side) {
            if (!sides[side]) continue;
            Random subdivision(building, 0x757070657253ull ^ domain ^ std::uint64_t(side));
            std::vector<Room> rooms;
            if (subdivision.pick(2) == 0) {
                rooms = perpendicularRooms(*sides[side], axis, subdivision, walls, core, footprint, rise, windows, blocked);
            } else {
                rooms.push_back(sides[side]->room);
                subdivideLiving(rooms, walls, subdivision, footprint, {}, windows);
            }
            layout.rooms.insert(layout.rooms.end(), rooms.begin(), rooms.end());
        }
        if (rise > 0) {
            const auto options = stairOptions({{HouseRoom::Kind::StairArea, core, {}}}, walls, {}, footprint, rise, blocked);
            if (options.empty()) return false;
            Random choice(building, 0x757070657252ull ^ domain);
            layout.stairs.push_back(makeStair(options[choice.pick(static_cast<std::uint32_t>(options.size()))], stairRoom, floor, top));
        }
        for (auto& wall : walls) { wall.storey = storey; layout.partitions.push_back(std::move(wall)); }
    }
    return true;
}
} // namespace viewer::house_layout
