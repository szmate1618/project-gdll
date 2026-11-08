#include "house_fixture.hpp"
#include "house_layout.hpp"
#include "collision_world.hpp"
#include "fps_controller.hpp"
#include "door_layer.hpp"
#include "physics_world.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
float area(const std::vector<glm::vec2>& polygon) {
    double sum = 0;
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        const auto a = polygon[i], b = polygon[(i + 1) % polygon.size()];
        sum += double(a.x) * b.y - double(a.y) * b.x;
    }
    return static_cast<float>(std::abs(sum) * .5);
}
bool inside(const std::vector<glm::vec2>& polygon, glm::vec2 p) {
    bool positive = false, negative = false;
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        const auto a = polygon[i], b = polygon[(i + 1) % polygon.size()];
        const auto e = b - a, d = p - a;
        const float cross = e.x * d.y - e.y * d.x;
        positive |= cross > .0001f; negative |= cross < -.0001f;
    }
    return !(positive && negative);
}
struct Fixture {
    viewer::Model model;
    std::vector<viewer::DoorPanel> doors;
    viewer::HouseSource source;
    Fixture(const glm::mat4& transform = glm::mat4(1), int storeys = 2, bool basement = false)
        : model(house_fixture::house(transform, true, storeys)) {
        if (basement) interior_fixture::quad(model, {-.5f, -2.6f, -6}, {.5f, -2.6f, -6},
            {.5f, -.5f, -6}, {-.5f, -.5f, -6}, "building_house_doors", transform);
        doors = viewer::prepareDoorPanels(model);
        source.name = "building_house"; source.firstDoor = 100;
        for (std::size_t i = 0; i < model.draws.size(); ++i) {
            const auto& draw = model.draws[i];
            if (draw.name == "building_house_walls" && draw.lowDetail) source.walls.push_back(i);
            if (draw.name == "building_house_roof") source.roofs.push_back(i);
            if (draw.name == "building_house_windows") source.windows.push_back(i);
        }
        for (std::size_t i = 0; i < doors.size(); ++i) source.doors.push_back(i);
    }
};
float segmentDistance(glm::vec2 a, glm::vec2 b, glm::vec2 c, glm::vec2 d) {
    const auto cross = [](glm::vec2 p, glm::vec2 q) { return p.x * q.y - p.y * q.x; };
    const float divisor = cross(b - a, d - c);
    if (std::abs(divisor) > 1e-6f) {
        const float t = cross(c - a, d - c) / divisor, u = cross(c - a, b - a) / divisor;
        if (t >= 0 && t <= 1 && u >= 0 && u <= 1) return 0;
    }
    const auto distance = [](glm::vec2 p, glm::vec2 start, glm::vec2 end) {
        const auto delta = end - start;
        const float t = glm::dot(delta, delta) > 0 ? glm::clamp(glm::dot(p - start, delta) / glm::dot(delta, delta), 0.f, 1.f) : 0.f;
        return glm::distance(p, start + t * delta);
    };
    return std::min({distance(a, c, d), distance(b, c, d), distance(c, a, b), distance(d, a, b)});
}
void windowClearance(const viewer::house_layout::Layout& plan, const std::vector<viewer::house_layout::Rectangle>& windows) {
    for (const auto& wall : plan.partitions) {
        glm::vec2 a, b;
        a[wall.axis] = b[wall.axis] = wall.position;
        a[1 - wall.axis] = wall.low; b[1 - wall.axis] = wall.high;
        for (const auto& window : windows)
            require(segmentDistance(a, b, window.low, window.high) >= .1999f,
                    "Every emitted partition must leave 20 cm at panes, including its endpoints");
    }
}
void windowPlacement() {
    using namespace viewer::house_layout;
    const Rectangle footprint{{-5, -6}, {5, 6}};
    const std::vector<Entrance> doors{{0, {0, -6}, {0, -1}, 1}};
    for (const bool deeper : {false, true}) {
        const std::vector<Rectangle> windows = deeper ? std::vector<Rectangle>{{{-5, -4.95f}, {-5, -3.6f}}} :
            std::vector<Rectangle>{{{-5, -4.3f}, {-5, -3.1f}}};
        int entries = 0;
        for (int seed = 0; seed < 100; ++seed) {
            const auto plan = generate("window_" + std::to_string(seed), footprint, doors, 0, {}, windows);
            windowClearance(plan, windows);
            for (const auto& wall : plan.partitions) for (const auto& opening : wall.openings) if (opening.entrance == 0) {
                const float depth = wall.position + 6;
                require(deeper ? depth > 2.5f && depth < 2.7f : depth >= 1.2f && depth < 1.6f,
                        "Entries must search both shallower and deeper than 2 m for window clearance");
                ++entries;
            }
        }
        require(entries > 90, "Window conflicts must move usable entries rather than simply skipping their rolls");
    }
    const std::vector<Rectangle> narrowGap{{{-5, -4.8f}, {-5, -3}}, {{-5, -2.5f}, {-5, -1}}};
    const auto narrow = generate("window_0", footprint, doors, 0, {}, narrowGap);
    bool foundGap = false;
    for (const auto& wall : narrow.partitions) for (const auto& opening : wall.openings)
        if (opening.entrance == 0) foundGap = wall.position > -2.8f && wall.position < -2.7f;
    require(foundGap, "Entry search must find a narrow valid gap between nearby windows");
    windowClearance(narrow, narrowGap);

    const std::vector<Rectangle> sideWindows{{{-3.4f, -6}, {-2.6f, -6}}, {{2.6f, -6}, {3.4f, -6}}};
    int sideRooms = 0;
    for (int seed = 0; seed < 100; ++seed) {
        const auto plan = generate("side_" + std::to_string(seed), footprint, doors, 0, {}, sideWindows);
        windowClearance(plan, sideWindows);
        for (const auto& room : plan.rooms) sideRooms += room.kind == viewer::HouseRoom::Kind::SmallRoom;
    }
    require(sideRooms > 50, "Side-room walls should move around windows when a usable room still fits");

    const std::vector<Rectangle> blocked{{{-5, -6}, {-5, 6}}, {{5, -6}, {5, 6}},
                                          {{-5, -6}, {5, -6}}, {{-5, 6}, {5, 6}}};
    const auto unsplit = generate("window_0", footprint, doors, 0, {}, blocked);
    require(unsplit.partitions.empty() && unsplit.rooms.size() == 1 && unsplit.rooms[0].kind == viewer::HouseRoom::Kind::Living,
            "If every facade is glazed, omit unsafe subdivisions and retain usable living space");
    const std::vector<Rectangle> gaps{{{-5, -6}, {-5, -.25f}}, {{-5, .25f}, {-5, 6}},
        {{5, -6}, {5, -.25f}}, {{5, .25f}, {5, 6}}, {{-5, -6}, {-.25f, -6}}, {{.25f, -6}, {5, -6}},
        {{-5, 6}, {-.25f, 6}}, {{.25f, 6}, {5, 6}}};
    int subdivided = 0;
    for (int seed = 0; seed < 100; ++seed) {
        const auto plan = generate("living_" + std::to_string(seed), footprint, {}, 0, {}, gaps);
        windowClearance(plan, gaps);
        subdivided += plan.rooms.size() > 1;
    }
    require(subdivided > 50, "Living subdivisions must search valid gaps outside their preferred random positions");
}
void loadedWindowGeometry(const glm::mat4& transform) {
    Fixture fixture(transform);
    const auto original = viewer::generateHouseFloorplan(fixture.model, fixture.source, fixture.doors);
    require(bool(original), "Window clearance fixture must generate an interior");
    const auto addWindow = [&](float bottom) {
        interior_fixture::quad(fixture.model, {-5, bottom, -4.3f}, {-5, bottom, -3.1f},
            {-5, bottom + 1.2f, -3.1f}, {-5, bottom + 1.2f, -4.3f}, "building_house_windows", transform);
        fixture.source.windows.push_back(fixture.model.draws.size() - 1);
    };
    addWindow(3.9f);
    const auto upstairsOnly = viewer::generateHouseFloorplan(fixture.model, fixture.source, fixture.doors);
    require(bool(upstairsOnly),
            "An upstairs pane must not constrain ground-storey partitions");
    for (std::size_t i = 0; i < original->rooms.size() && original->rooms[i].storey == 0; ++i) {
        require(i < upstairsOnly->rooms.size() && upstairsOnly->rooms[i].storey == 0 &&
            original->rooms[i].polygon == upstairsOnly->rooms[i].polygon && original->rooms[i].kind == upstairsOnly->rooms[i].kind,
            "Upstairs-only window must preserve ground-layout geometry");
    }
    addWindow(.9f);
    const auto shifted = viewer::generateHouseFloorplan(fixture.model, fixture.source, fixture.doors);
    require(bool(shifted), "Ground-floor window must move the partition without rejecting the house");
    const auto worldXZ = [&](glm::vec3 p) { const auto q = glm::vec3(transform * glm::vec4(p, 1)); return glm::vec2(q.x, q.z); };
    const auto start = worldXZ({-5, 0, -4.3f}), end = worldXZ({-5, 0, -3.1f});
    const auto closestWall = [&](const viewer::HouseFloorplan& plan) {
        float nearest = std::numeric_limits<float>::max();
        for (const auto& mesh : plan.structure.primitives) if (mesh.material == 0)
            for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
                if (mesh.vertices[mesh.indices[i]].position.y >= plan.upperFloors.front() &&
                    mesh.vertices[mesh.indices[i + 1]].position.y >= plan.upperFloors.front() &&
                    mesh.vertices[mesh.indices[i + 2]].position.y >= plan.upperFloors.front()) continue;
                for (int edge = 0; edge < 3; ++edge) {
                const auto a = mesh.vertices[mesh.indices[i + edge]].position;
                const auto b = mesh.vertices[mesh.indices[i + (edge + 1) % 3]].position;
                nearest = std::min(nearest, segmentDistance({a.x, a.z}, {b.x, b.z}, start, end));
                }
            }
        return nearest;
    };
    require(closestWall(*original) < .01f, "Loaded-window fixture must reproduce a partition crossing the pane");
    require(closestWall(*shifted) >= .2f, "Actual generated wall geometry must clear the loaded pane by 20 cm after transforming");
}
void variation() {
    const std::vector<viewer::house_layout::Entrance> entrances{{0, {0, -6}, {0, -1}, 1}, {1, {0, 6}, {0, 1}, 1}};
    constexpr int samples = 1000;
    int entries = 0, left = 0, right = 0, both = 0, none = 0, skippedWithRooms = 0;
    std::array<int, 3> livingCounts{};
    std::set<int> combinations;
    for (int seed = 0; seed < samples; ++seed) {
        const auto plan = viewer::house_layout::generate("house_" + std::to_string(seed), {{-5, -6}, {5, 6}}, entrances, 0, {3});
        require(!plan.rooms.empty() && plan.stairs.size() == 1, "Ordinary multi-storey houses must get a complete layout and stair");
        float total = 0;
        int living = 0, entryCount = 0;
        std::array<bool, 2> hasLeft{}, hasRight{};
        for (const auto& room : plan.rooms) {
            const auto size = room.bounds.high - room.bounds.low;
            total += size.x * size.y;
            if (room.kind == viewer::HouseRoom::Kind::Living) {
                ++living;
                require(size.x >= 2.5f && size.y >= 2.5f && size.x * size.y >= 10, "Living subdivisions must avoid narrow or tiny rooms");
            } else if (room.kind == viewer::HouseRoom::Kind::Anteroom) {
                ++entries; ++entryCount;
                require(std::min(size.x, size.y) >= 2, "Side rooms must leave a usable anteroom");
            } else {
                require(std::abs(size.x - 2) < .001f && std::abs(size.y - 2) < .001f, "Small rooms must be approximately 2 by 2 metres");
                const auto center = (room.bounds.low + room.bounds.high) * .5f;
                (center.x < 0 ? hasLeft : hasRight)[center.y > 0 ? 1 : 0] = true;
            }
        }
        require(living >= 1 && living <= 3 && std::abs(total - 120) < .01f, "All polygons must conserve footprint area with 1-3 living rooms");
        ++livingCounts[static_cast<std::size_t>(living - 1)];
        if (entryCount < 2) ++skippedWithRooms;
        for (int side = 0; side < 2; ++side) {
            left += hasLeft[side]; right += hasRight[side]; both += hasLeft[side] && hasRight[side];
            none += !hasLeft[side] && !hasRight[side];
            combinations.insert(int(hasLeft[side]) + 2 * int(hasRight[side]));
        }
        // Rectangle interiors must be disjoint, including the independent side rooms.
        for (std::size_t a = 0; a < plan.rooms.size(); ++a) for (std::size_t b = a + 1; b < plan.rooms.size(); ++b) {
            const auto overlap = glm::min(plan.rooms[a].bounds.high, plan.rooms[b].bounds.high) -
                                 glm::max(plan.rooms[a].bounds.low, plan.rooms[b].bounds.low);
            require(overlap.x <= .0001f || overlap.y <= .0001f, "Room interiors must not overlap");
        }
    }
    std::cout << "Entry/side-room samples: " << entries << "/2000 entries, " << left << " left, " << right << " right, " << both << " both, " << none << " neither\n";
    require(entries > 1850 && entries < 1950, "Entry creation must occur about 95 percent of the time per door");
    require(left > entries * .43 && left < entries * .57 && right > entries * .43 && right < entries * .57,
            "Each side must independently create a small room about half of the time");
    require(combinations.size() == 4 && both > entries * .18 && both < entries * .32 && none > entries * .18,
            "Side-room choices must include both, neither, and either side independently");
    require(skippedWithRooms > 50 && std::all_of(livingCounts.begin(), livingCounts.end(), [](int n) { return n > 150; }),
            "Skipping an entry must still generate varied living subdivisions, including 1, 2, and 3 rooms");
    std::cout << "1000 house layouts: " << entries << "/2000 entries; living room counts "
              << livingCounts[0] << ',' << livingCounts[1] << ',' << livingCounts[2] << '\n';
}
void connections(const viewer::HouseFloorplan& plan) {
    std::vector<std::vector<std::size_t>> links(plan.rooms.size());
    require(plan.doors.size() <= 8 + 8 * plan.upperFloors.size(), "Reserved door budget must cover every storey's subdivisions");
    for (const auto& door : plan.doors) {
        std::array<std::size_t, 2> endpoints{};
        for (int side = 0; side < 2; ++side) {
            const auto p = door.panel.center + door.panel.outward * (side ? .15f : -.15f);
            std::vector<std::size_t> containing;
            for (std::size_t room = 0; room < plan.rooms.size(); ++room)
                if (plan.rooms[room].storey == door.storey && inside(plan.rooms[room].polygon, {p.x, p.z})) containing.push_back(room);
            require(containing.size() == 1, "Each side of every internal doorway must open into one actual room");
            endpoints[static_cast<std::size_t>(side)] = containing.front();
        }
        require(endpoints[0] != endpoints[1], "Internal doors must connect different room polygons");
        if (plan.rooms[endpoints[0]].kind == viewer::HouseRoom::Kind::SmallRoom) {
            require(!door.panel.inward && plan.rooms[endpoints[1]].kind == viewer::HouseRoom::Kind::Anteroom,
                    "Small-room doors must swing outward into their anteroom");
        } else if (!door.panel.inward) require(door.storey > 0 && plan.rooms[endpoints[1]].kind == viewer::HouseRoom::Kind::StairArea,
                    "Upper side doors must open toward their stair area");
        links[endpoints[0]].push_back(endpoints[1]); links[endpoints[1]].push_back(endpoints[0]);
    }
    for (const auto& stair : plan.stairs) {
        const auto storey = plan.rooms.at(stair.room).storey;
        const auto landing = std::find_if(plan.rooms.begin(), plan.rooms.end(), [&](const viewer::HouseRoom& r) {
            return r.storey == storey + 1 && r.kind == viewer::HouseRoom::Kind::StairArea;
        });
        require(landing != plan.rooms.end(), "Every ramp must lead into the next storey's stair area");
        const auto upper = static_cast<std::size_t>(landing - plan.rooms.begin());
        links[stair.room].push_back(upper); links[upper].push_back(stair.room);
    }
    std::set<std::size_t> seen{0}; std::vector<std::size_t> todo{0};
    while (!todo.empty()) { const auto room = todo.back(); todo.pop_back(); for (const auto next : links[room]) if (seen.insert(next).second) todo.push_back(next); }
    require(seen.size() == plan.rooms.size(), "All anterooms, small rooms and living rooms must remain connected by doors");
}
bool smallRoomSwing(const viewer::HouseFloorplan& plan) {
    std::vector<viewer::DoorPanel> panels;
    for (const auto& door : plan.doors) panels.push_back(door.panel);
    auto physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::DoorLayer doors(panels, physics);
    bool tested = false;
    for (std::size_t id = 0; id < panels.size(); ++id) {
        const auto& panel = panels[id];
        if (panel.inward || plan.doors[id].storey != 0) continue;
        doors.setAngle(id, .8f);
        const auto opened = glm::vec3(doors.transform(id) * glm::vec4(panel.center, 1));
        require(std::any_of(plan.rooms.begin(), plan.rooms.end(), [&](const viewer::HouseRoom& room) {
            return room.storey == 0 && room.kind == viewer::HouseRoom::Kind::Anteroom && inside(room.polygon, {opened.x, opened.z});
        }), "Opened small-room panel must occupy the anteroom, rather than its small room");
        doors.setAngle(id, 0);
        const auto right = glm::cross(glm::vec3(0, 1, 0), panel.outward);
        doors.applyImpulse(id, panel.outward * 4.f, panel.center + right * panel.halfExtents.x);
        for (int frame = 0; frame < 60; ++frame) physics->step(1.f / 60);
        require(doors.angle(id) > .05f && doors.angle(id) < 2,
                "Small-room physical hinges must allow the outward swing and respect their limit");
        tested = true;
    }
    return tested;
}
void basementGeometry(const glm::mat4& transform) {
    for (const int storeys : {1, 2, 3}) {
        Fixture fixture(transform, storeys, true), original(transform, storeys);
        const auto plan = viewer::generateHouseFloorplan(fixture.model, fixture.source, fixture.doors);
        const auto without = viewer::generateHouseFloorplan(original.model, original.source, original.doors);
        require(plan && without && plan->basementFloor, "Lower doors must identify a basement even without an upstairs flight");
        require(std::abs(plan->floor - without->floor) < .001f && plan->upperFloors == without->upperFloors,
                "Lower leaves must not change the primary floor or upper-floor heights");
        require(plan->rooms.size() == without->rooms.size() + 1 && plan->doors.size() == without->doors.size(),
                "Adding a basement must preserve all ground and upper room choices and reserved interior doors");
        require(plan->stairs.size() == without->stairs.size() + (storeys > 1 ? 1 : 0),
                "Basement flights are conditional on an existing upward staircase");
        const auto& room = plan->rooms.back();
        require(room.storey == viewer::HouseFloorplan::basementStorey && room.entrances.size() == 1 &&
                plan->minimum.y <= *plan->basementFloor,
                "Basement room, lower doorway and streamed bounds must use the lower level");
        if (storeys == 1) continue;
        const auto& stair = plan->stairs.back();
        require(stair.ramp == plan->stairs.front().ramp && stair.bottom.y < plan->floor &&
                std::abs(stair.top.y - plan->floor - .01f) < .001f,
                "A transformed basement flight must stack under the first upward flight");
        viewer::CollisionWorld collision(plan->structure);
        const auto middle = (stair.bottom + stair.top) * .5f;
        require(collision.walkingGroundAt(middle.x, middle.z, middle.y - .001f, middle.y + .001f).has_value() &&
                !collision.walkingGroundAt(middle.x, middle.z, stair.top.y - .001f, stair.top.y + .001f),
                "Basement ramps must have walking support and a genuine ground-floor opening");
    }
}
void geometry(const glm::mat4& transform, int storeys) {
    Fixture fixture(transform, storeys);
    bool testedSmallRoom = false;
    for (int seed = 0; seed < 40; ++seed) {
        fixture.source.name = "floorplan_" + std::to_string(seed);
        const auto plan = viewer::generateHouseFloorplan(fixture.model, fixture.source, fixture.doors);
        require(bool(plan), "Transformed family house must remain eligible after subdivision");
        require(plan->stairs.size() == static_cast<std::size_t>(storeys - 1), "Each extra storey must have a ramp and matching floor opening");
        float sum = 0; int stairsRooms = 0;
        for (const auto& room : plan->rooms) { sum += area(room.polygon); stairsRooms += room.staircase; }
        require(std::abs(sum - area(plan->footprint) * storeys) < .02f * storeys && stairsRooms == storeys,
                "Room polygons must tile every storey with one ground staircase room and each upper stair area");
        connections(*plan);
        if (!testedSmallRoom) testedSmallRoom = smallRoomSwing(*plan);
        viewer::CollisionWorld collision(plan->structure);
        for (const auto& stair : plan->stairs) {
            require(plan->rooms.at(stair.room).staircase,
                    "The staircase must belong to the selected living room");
            const auto run = glm::vec2(stair.top.x - stair.bottom.x, stair.top.z - stair.bottom.z);
            require(stair.top.y > stair.bottom.y && glm::length(run) >= stair.top.y - stair.bottom.y,
                    "Ramp rise and run must remain walkable without jumping");
            for (const auto p : stair.ramp) require(inside(plan->rooms.at(stair.room).polygon, p), "Ramp must fit in its living room");
            glm::vec2 hole(0); for (const auto p : stair.opening) hole += p * .25f;
            require(!collision.walkingGroundAt(hole.x, hole.y, stair.top.y - .01f, stair.top.y + .01f),
                    "The rectangular opening must remove upper-floor collision as well as rendering");
            auto towardLanding = stair.top - stair.bottom; towardLanding.y = 0;
            towardLanding = glm::normalize(towardLanding);
            const auto landing = stair.top + towardLanding * .01f;
            const auto landingSupport = collision.walkingGroundAt(landing.x, landing.z, stair.top.y - .001f, stair.top.y + .001f);
            require(landingSupport && landingSupport->normal.y > .99f,
                    "The ramp endpoint must meet upper-floor support without a gap");
            const auto middle = (stair.bottom + stair.top) * .5f;
            const auto support = collision.walkingGroundAt(middle.x, middle.z, middle.y - .05f, middle.y + .05f);
            require(support && support->normal.y >= .707f && std::abs(support->height - middle.y) < .001f,
                    "Ramp plane must provide transformed, sloped walking support");
        }
        if (seed == 0) for (const auto& stair : plan->stairs) {
            auto direction = stair.top - stair.bottom; direction.y = 0; direction = glm::normalize(direction);
            viewer::FPSController player;
            player.teleport(stair.bottom - direction * .5f + glm::vec3(0, .03f, 0));
            viewer::FPSInput input; input.forward = 1; input.yawDegrees = glm::degrees(std::atan2(direction.z, direction.x));
            const int frames = static_cast<int>((glm::distance(stair.bottom, stair.top) + 1) / 3.f * 60) + 60;
            float highest = player.feet().y;
            bool arrived = false;
            for (int frame = 0; frame < frames; ++frame) {
                player.update(1.f / 60, input, collision);
                highest = std::max(highest, player.feet().y);
                if (glm::dot(player.feet() - stair.top, direction) > .35f && player.feet().y >= stair.top.y - .1f) {
                    arrived = true; break;
                }
            }
            if (!arrived) {
                const auto p = player.feet();
                std::cerr << "Ramp " << stair.bottom.x << ',' << stair.bottom.y << ',' << stair.bottom.z
                          << " -> " << stair.top.x << ',' << stair.top.y << ',' << stair.top.z
                          << "; player " << p.x << ',' << p.y << ',' << p.z << "; highest " << highest << '\n';
            }
            require(arrived, "Standing player must actually walk up the ramp through its ceiling cutout");
            for (int frame = 0; frame < 30; ++frame) player.update(1.f / 60, {}, collision);
            require(player.grounded() && std::abs(player.feet().y - stair.top.y) < .03f,
                    "Standing player must remain supported on the upper landing");
        }
    }
    require(testedSmallRoom, "Physical swing checks must exercise a generated small-room door");
}
} // namespace
int main() {
    try {
        variation(); windowPlacement();
        basementGeometry(glm::mat4(1));
        basementGeometry(glm::translate(glm::mat4(1), {87, 5, -39}) *
            glm::rotate(glm::mat4(1), glm::radians(31.f), glm::vec3(0, 1, 0)) * glm::scale(glm::mat4(1), {-1, 1, 1}));
        loadedWindowGeometry(glm::mat4(1));
        loadedWindowGeometry(glm::translate(glm::mat4(1), {-1108, 5, -250}) *
            glm::rotate(glm::mat4(1), glm::radians(31.f), glm::vec3(0, 1, 0)) * glm::scale(glm::mat4(1), {-1, 1, 1}));
        geometry(glm::mat4(1), 2);
        geometry(glm::translate(glm::mat4(1), {87, 5, -39}) * glm::rotate(glm::mat4(1), glm::radians(31.f), glm::vec3(0, 1, 0)) * glm::scale(glm::mat4(1), {-1, 1, 1}), 3);
        std::cout << "Room probability, connectivity, stair headroom and walking tests passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
