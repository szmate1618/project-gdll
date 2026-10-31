#include "house_layout.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
using namespace viewer::house_layout;
using Kind = viewer::HouseRoom::Kind;
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
float area(Rectangle r) { const auto d = r.high - r.low; return d.x * d.y; }
bool inside(Rectangle r, glm::vec2 p) {
    return glm::all(glm::greaterThanEqual(p, r.low - glm::vec2(.001f))) &&
        glm::all(glm::lessThanEqual(p, r.high + glm::vec2(.001f)));
}
Layout initial() {
    Layout result;
    result.rooms.push_back({Kind::Living, {{-7, -7}, {7, 7}}, {}});
    result.stairs.push_back({0, {{-.55f, -1.5f}, {.55f, 1.5f}}, {{-.59f, -1.54f}, {.59f, 1.5f}}, 1, true, .01f, 3});
    return result;
}
void validate(const Layout& layout, std::size_t levels) {
    for (std::size_t level = 1; level <= levels; ++level) {
        std::vector<std::size_t> rooms;
        std::size_t cores = 0;
        float total = 0;
        for (std::size_t i = 0; i < layout.rooms.size(); ++i) if (layout.rooms[i].storey == level) {
            const auto& r = layout.rooms[i]; rooms.push_back(i); total += area(r.bounds);
            cores += r.kind == Kind::StairArea;
            if (r.kind == Kind::Living) {
                const auto d = r.bounds.high - r.bounds.low;
                require(std::min(d.x, d.y) >= 2.5f - .001f && area(r.bounds) >= 10.f - .001f,
                        "Upper living rooms must retain the ground-floor minimum sizes");
            }
        }
        require(cores == 1 && std::abs(total - 196) < .01f, "One stair area and its living rooms must tile each upper storey");
        for (const auto a : rooms) for (const auto b : rooms) if (a < b) {
            const auto overlap = glm::min(layout.rooms[a].bounds.high, layout.rooms[b].bounds.high) -
                glm::max(layout.rooms[a].bounds.low, layout.rooms[b].bounds.low);
            require(overlap.x < .001f || overlap.y < .001f, "Upper room interiors must not overlap");
        }
        std::vector<std::set<std::size_t>> links(layout.rooms.size());
        std::size_t doors = 0;
        for (const auto& wall : layout.partitions) if (wall.storey == level) for (const auto& opening : wall.openings) {
            ++doors;
            glm::vec2 p; p[wall.axis] = wall.position; p[1 - wall.axis] = (opening.low + opening.high) * .5f;
            std::array<std::size_t, 2> ends{};
            for (int side = 0; side < 2; ++side) {
                const auto q = p + opening.outward * (side ? .1f : -.1f);
                std::vector<std::size_t> found;
                for (const auto id : rooms) if (inside(layout.rooms[id].bounds, q)) found.push_back(id);
                require(found.size() == 1, "Both sides of an upper doorway must occupy exactly one room"); ends[side] = found.front();
            }
            require(ends[0] != ends[1], "Upper doors must connect distinct rooms");
            if (layout.rooms[ends[1]].kind == Kind::StairArea)
                require(!opening.inward, "Doors on the stair-area boundary must swing into that area");
            links[ends[0]].insert(ends[1]); links[ends[1]].insert(ends[0]);
        }
        require(doors <= 2 * maxUpperRoomsPerSide, "Upper doors must fit the stable reserved budget");
        std::set<std::size_t> seen{rooms.front()}; std::vector<std::size_t> todo{rooms.front()};
        while (!todo.empty()) { auto id = todo.back(); todo.pop_back(); for (auto next : links[id]) if (seen.insert(next).second) todo.push_back(next); }
        require(seen.size() == rooms.size(), "Every upper room must be reachable from the stair area");
        const auto core = *std::find_if(rooms.begin(), rooms.end(), [&](auto id) { return layout.rooms[id].kind == Kind::StairArea; });
        const auto& incoming = layout.stairs.at(level - 1);
        require(inside(layout.rooms[core].bounds, incoming.opening.low) && inside(layout.rooms[core].bounds, incoming.opening.high),
                "Incoming floor opening must remain wholly inside the stair area");
    }
}
void probabilities() {
    int neither = 0, both = 0, low = 0, high = 0;
    std::set<std::array<int, 2>> modes;
    for (int seed = 0; seed < 1000; ++seed) {
        auto layout = initial();
        require(generateUpperStoreys(layout, "upper_" + std::to_string(seed), {{-7, -7}, {7, 7}}, {3}, 6.5f, {}), "Unconstrained upper storey must generate");
        validate(layout, 1);
        const auto core = std::find_if(layout.rooms.begin(), layout.rooms.end(), [](const Room& r) { return r.kind == Kind::StairArea; });
        const bool left = core->bounds.low.x > -6.99f || core->bounds.low.y > -6.99f;
        const bool right = core->bounds.high.x < 6.99f || core->bounds.high.y < 6.99f;
        low += left; high += right; both += left && right; neither += !left && !right;
        std::array<int, 2> patterns{-1, -1};
        for (const auto& wall : layout.partitions) if (wall.storey == 1 && !wall.openings.empty() && !wall.openings.front().inward) {
            const int side = wall.position < 0 ? 0 : 1;
            int living = 0;
            for (const auto& room : layout.rooms) if (room.storey == 1 && room.kind == Kind::Living &&
                (side == 0 ? room.bounds.high[wall.axis] <= wall.position + .001f : room.bounds.low[wall.axis] >= wall.position - .001f)) ++living;
            if (living > 1) patterns[side] = int(wall.openings.size()) == living ? 0 : 1;
            if (patterns[side] == 0) for (const auto& cut : layout.partitions) if (cut.storey == 1 && cut.openings.empty())
                require(cut.axis != wall.axis, "Direct-access sections must be divided perpendicular to D");
        }
        if (patterns[0] >= 0 && patterns[1] >= 0) modes.insert(patterns);
    }
    std::cout << "Upper side walls: " << low << ", " << high << "; both=" << both << ", neither=" << neither << '\n';
    require(low > 650 && low < 750 && high > 650 && high < 750 && both > 430 && both < 550 && neither > 55 && neither < 130,
            "Each side must independently use its 70 percent partition roll");
    require(modes.size() == 4, "Both sides must independently exercise direct-access and ground-style subdivision");
}
void windowsAndFurtherStairs() {
    const std::vector<Window> upperWindows{{{{-7, -7}, {-7, 7}}, 3.9f, 5.1f}, {{{7, -7}, {7, 7}}, 3.9f, 5.1f},
        {{{-7, -7}, {7, -7}}, 3.9f, 5.1f}, {{{-7, 7}, {7, 7}}, 3.9f, 5.1f}};
    auto glazed = initial();
    require(generateUpperStoreys(glazed, "upper_0", {{-7, -7}, {7, 7}}, {3}, 6.5f, upperWindows), "Glazed upper storey must keep its stair area");
    require(glazed.partitions.empty() && glazed.rooms.size() == 2, "Window clearance must suppress both unsafe upper side walls");
    auto downstairs = upperWindows; for (auto& w : downstairs) { w.bottom -= 3; w.top -= 3; }
    auto clear = initial();
    require(generateUpperStoreys(clear, "upper_0", {{-7, -7}, {7, 7}}, {3}, 6.5f, downstairs) && !clear.partitions.empty(),
            "Windows downstairs must not constrain upper-floor partitions");
    for (int seed = 0; seed < 100; ++seed) {
        auto layout = initial();
        require(generateUpperStoreys(layout, "stack_" + std::to_string(seed), {{-7, -7}, {7, 7}}, {3, 6}, 9.5f, {}), "Further floors must get a usable outgoing stair");
        validate(layout, 2);
        require(layout.stairs.size() == 2 && layout.rooms[layout.stairs[1].room].storey == 1 && layout.stairs[1].bottom == 3 && layout.stairs[1].top == 6,
                "Further stair must belong to the current floor and stop at the next floor");
        const auto overlap = glm::min(layout.stairs[1].ramp.high, layout.stairs[0].opening.high) -
            glm::max(layout.stairs[1].ramp.low, layout.stairs[0].opening.low);
        require(overlap.x <= 0 || overlap.y <= 0, "Outgoing ramps must not start in the incoming floor opening");
    }
}
} // namespace
int main() {
    try { probabilities(); windowsAndFurtherStairs(); std::cout << "Upper-storey generation tests passed\n"; return 0; }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
