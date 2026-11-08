#include "apartment_fixture.hpp"
#include "house_polygon.hpp"
#include "floorplan_layer.hpp"
#include "door_layer.hpp"
#include "collision_world.hpp"
#include "fps_controller.hpp"
#include "physics_world.hpp"

#include <iostream>
#include <limits>
#include <numeric>
#include <set>
#include <stdexcept>

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
bool sharesEdge(const std::vector<glm::vec2>& a, const std::vector<glm::vec2>& b) {
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto edge = a[(i + 1) % a.size()] - a[i];
        const float length = glm::length(edge);
        if (length < .01f) continue;
        const auto direction = edge / length;
        for (std::size_t j = 0; j < b.size(); ++j) {
            const auto first = b[j] - a[i], second = b[(j + 1) % b.size()] - a[i];
            const auto distance = [&](glm::vec2 p) { return std::abs(p.x * direction.y - p.y * direction.x); };
            if (distance(first) > .002f || distance(second) > .002f) continue;
            const float start = glm::dot(first, direction), end = glm::dot(second, direction);
            if (std::min(length, std::max(start, end)) - std::max(0.f, std::min(start, end)) > .01f) return true;
        }
    }
    return false;
}
void circulation(const viewer::HouseFloorplan& plan) {
    using Kind = viewer::HouseRoom::Kind;
    for (std::size_t level = 0; level <= plan.upperFloors.size(); ++level) {
        std::set<std::size_t> reached;
        for (std::size_t i = 0; i < plan.rooms.size(); ++i)
            if (plan.rooms[i].storey == level && plan.rooms[i].kind == Kind::StairArea) reached.insert(i);
        require(reached.size() == 1, "Every floor must have one shared stair core");
        bool changed = true;
        while (changed) {
            changed = false;
            for (std::size_t i = 0; i < plan.rooms.size(); ++i) {
                const auto& room = plan.rooms[i];
                if (room.storey != level || room.kind == Kind::Apartment || reached.count(i)) continue;
                for (const auto j : reached) if (sharesEdge(room.polygon, plan.rooms[j].polygon)) {
                    reached.insert(i); changed = true; break;
                }
            }
        }
        std::vector<int> entrances(plan.rooms.size());
        for (const auto& door : plan.doors) if (door.storey == level) {
            const auto inside = door.panel.center - door.panel.outward * .35f;
            const auto outside = door.panel.center + door.panel.outward * .35f;
            bool connected = false, apartment = false;
            for (std::size_t i = 0; i < plan.rooms.size(); ++i) {
                const auto& room = plan.rooms[i];
                if (room.storey != level) continue;
                if (room.kind == Kind::Apartment && viewer::house_polygon::contains(room.polygon, {inside.x, inside.z})) {
                    ++entrances[i]; apartment = true;
                }
                if (reached.count(i) && viewer::house_polygon::contains(room.polygon, {outside.x, outside.z})) connected = true;
            }
            require(apartment && connected, "Every apartment entrance must reach common space connected to the stairs");
        }
        for (std::size_t i = 0; i < plan.rooms.size(); ++i) {
            const auto& room = plan.rooms[i];
            if (room.storey != level) continue;
            if (room.kind == Kind::Apartment) require(entrances[i] == 1, "Each individual apartment must have exactly one entrance");
            else require(reached.count(i), "Every common polygon must connect to the stairs through an open shared edge");
        }
    }
}
void windowClearance(const viewer::HouseFloorplan& plan, const viewer::Model& model,
                     const viewer::HouseSource& source, const viewer::building_shell::Shell& shell) {
    std::vector<std::pair<glm::vec2, glm::vec2>> panes;
    for (const auto& face : viewer::building_shell::faces(model, source.windows)) {
        glm::vec2 low(std::numeric_limits<float>::max()), high(-low);
        for (const auto p : face) {
            const auto q = shell.local({p.x, p.z}); low = glm::min(low, q); high = glm::max(high, q);
        }
        panes.emplace_back(low, high);
    }
    for (const auto& mesh : plan.structure.primitives) if (mesh.material == 0)
        for (std::size_t i = 0; i + 3 < mesh.vertices.size(); i += 4) {
            const auto a = mesh.vertices[i].position, b = mesh.vertices[i + 1].position;
            const auto first = shell.local({a.x, a.z}), second = shell.local({b.x, b.z});
            const auto low = glm::min(first, second), high = glm::max(first, second);
            for (const auto& pane : panes) {
                const auto separation = glm::max(glm::max(pane.first - high, low - pane.second), glm::vec2(0));
                require(glm::length(separation) >= .198f, "Every physical apartment partition must remain 20 cm from all storeys' windows");
            }
        }
}
void basement() {
    auto model = apartment_fixture::block();
    for (auto& draw : model.draws) if (draw.name == "building_prefab_walls" && draw.lowDetail) {
        for (auto& vertex : model.primitives.at(draw.primitive).vertices)
            if (vertex.position.y < 0) vertex.position.y = -2.68f;
        draw.buildingFloor = 0;
    }
    interior_fixture::quad(model, {-.5f, -2.6f, -12}, {.5f, -2.6f, -12},
        {.5f, -.5f, -12}, {-.5f, -.5f, -12}, "building_prefab_doors");
    const auto doors = viewer::prepareDoorPanels(model);
    const auto source = apartment_fixture::source(model, doors);
    const auto block = viewer::classifyPrefabBlock(model, source);
    require(block && block->storeys == 5 && std::abs(block->shell.floor) < .001f,
            "The recorded main-floor datum must preserve shell classification when foundations extend into a basement");
    const auto plan = viewer::generatePrefabFloorplan(model, source, doors, *block);
    require(plan && plan->basementFloor && plan->stairs.size() == block->storeys,
            "Prefab balcony buildings must also extend the first stair flight into a basement");
    require(plan->stairs.back().ramp == plan->stairs.front().ramp &&
            plan->rooms.back().storey == viewer::HouseFloorplan::basementStorey,
            "Prefab basement stairs must share the footprint of the ground-to-upper flight");
}
void classifier() {
    auto model = apartment_fixture::block();
    const auto doors = viewer::prepareDoorPanels(model);
    auto source = apartment_fixture::source(model, doors);
    const auto original = viewer::classifyPrefabBlock(model, source);
    require(original && original->storeys == 5 && !original->slab, "Compact block must classify from its shell");
    source.windows = {std::numeric_limits<std::size_t>::max()}; source.doors = source.windows;
    const auto independent = viewer::classifyPrefabBlock(model, source);
    require(independent && independent->storeys == original->storeys && independent->shell.floor == original->shell.floor,
            "Classifier must never inspect window or door descriptors");
    source.windows.clear(); source.doors.clear();
    require(bool(viewer::classifyPrefabBlock(model, source)), "Missing windows/doors must not alter shell classification");
    source = apartment_fixture::source(model, doors); source.windows.clear();
    const auto withoutPanes = viewer::generatePrefabFloorplan(model, source, doors, *original);
    require(withoutPanes && withoutPanes->upperFloors.size() == 4 && std::abs(withoutPanes->upperFloors.front() - 3.1f) < .001f,
            "Shell-only storeys must also generate with no window descriptors");
    for (const auto size : {glm::vec3(10,12,2), {20,24,3}, {20,24,17}, {50,50,5}}) {
        auto invalid = apartment_fixture::block(size.x, size.y, int(size.z));
        const auto panels = viewer::prepareDoorPanels(invalid);
        require(!viewer::classifyPrefabBlock(invalid, apartment_fixture::source(invalid, panels)), "Small, short, tall or oversized shells must remain unclassified");
    }
    auto pitched = apartment_fixture::block();
    for (const auto draw : apartment_fixture::source(pitched, {}).roofs)
        pitched.primitives[pitched.draws[draw].primitive].vertices.front().position.y += 1;
    require(!viewer::classifyPrefabBlock(pitched, apartment_fixture::source(pitched, {})), "Roof relief above half a metre must reject pitched shells");
    auto slope = apartment_fixture::block();
    for (const auto draw : apartment_fixture::source(slope, {}).walls)
        for (auto& vertex : slope.primitives[slope.draws[draw].primitive].vertices)
            if (vertex.position.y < 0 && vertex.position.x < 0) vertex.position.y -= 2;
    const auto uneven = viewer::classifyPrefabBlock(slope, apartment_fixture::source(slope, {}));
    require(uneven && std::abs(uneven->shell.floor) < .001f && uneven->storeys == 5,
            "Buried downhill wall bases must not add a floor to the shell height");
    auto courtyard = apartment_fixture::block();
    const std::vector<glm::vec2> hole{{-2,-2},{2,-2},{2,2},{-2,2}};
    for (std::size_t i = 0; i < hole.size(); ++i) {
        const auto a = hole[i], b = hole[(i + 1) % hole.size()];
        interior_fixture::quad(courtyard, {a.x,-.08f,a.y}, {b.x,-.08f,b.y}, {b.x,15.5f,b.y}, {a.x,15.5f,a.y},
                               "building_prefab_walls", glm::mat4(1), true);
    }
    require(!viewer::classifyPrefabBlock(courtyard, apartment_fixture::source(courtyard, {})), "Courtyard boundaries must remain outside the block heuristic");
}
void generated(float width, float depth, int levels, const glm::mat4& transform,
               const std::vector<glm::vec2>& outline = {}) {
    std::cout << "Block " << width << 'x' << depth << ", floors=" << levels << ", x=" << transform[3].x << std::endl;
    auto model = apartment_fixture::block(width, depth, levels, transform, outline);
    auto panels = viewer::prepareDoorPanels(model);
    const auto source = apartment_fixture::source(model, panels);
    const auto classification = viewer::classifyPrefabBlock(model, source);
    require(bool(classification), "Block fixture must pass shell classification");
    const auto plan = viewer::generatePrefabFloorplan(model, source, panels, *classification);
    require(bool(plan), "A classified slab/tower fixture must generate connected apartment regions");
    require(plan->kind == viewer::HouseFloorplan::Kind::PrefabApartmentBlock && plan->upperFloors.size() == std::size_t(levels - 1),
            "Prefab plan must expose its kind and all storeys");
    circulation(*plan);
    windowClearance(*plan, model, source, classification->shell);
    std::vector<int> apartments(std::size_t(levels), 0);
    float total = 0;
    for (const auto& room : plan->rooms) {
        require(viewer::house_polygon::simple(room.polygon), "Each apartment/common area must have a simple connected boundary");
        total += viewer::house_polygon::area(room.polygon);
        if (room.kind == viewer::HouseRoom::Kind::Apartment) {
            ++apartments[room.storey]; require(viewer::house_polygon::area(room.polygon) >= 24.99f, "Apartment remnants must merge to at least 25 square metres");
        }
    }
    require(std::abs(total - viewer::house_polygon::area(plan->footprint) * levels) < .2f,
            "Rooms must tile the entire footprint on every floor without duplicate common areas");
    for (const auto count : apartments) require(count >= 2, "Each floor must retain at least two apartments");
    require(plan->stairs.size() == std::size_t(levels - 1) && plan->doors.size() <= viewer::prefabDoorCapacity(*classification, source.doors.size()),
            "Every upper floor needs an incoming flight and door IDs must fit the cold reservation");
    // One physical inward-opening entrance per apartment.
    int apartmentDoors = 0;
    for (const auto& door : plan->doors) {
        require(door.panel.inward, "Apartment entrances must open into the apartment");
        const auto p = door.panel.center - door.panel.outward * .3f;
        require(std::any_of(plan->rooms.begin(), plan->rooms.end(), [&](const auto& r) {
            return r.storey == door.storey && r.kind == viewer::HouseRoom::Kind::Apartment && viewer::house_polygon::contains(r.polygon, {p.x,p.z});
        }), "The inward side of every entrance must actually be an apartment");
        ++apartmentDoors;
    }
    require(apartmentDoors == std::accumulate(apartments.begin(), apartments.end(), 0), "Each apartment must have exactly one common-space doorway");
    for (std::size_t level = 2; level < std::size_t(levels); ++level) {
        std::vector<const viewer::HouseRoom*> previous, current;
        for (const auto& room : plan->rooms) {
            if (room.storey == level - 1) previous.push_back(&room);
            if (room.storey == level) current.push_back(&room);
        }
        require(previous.size() == current.size(), "Upper floors must repeat one template");
        for (std::size_t i = 0; i < previous.size(); ++i)
            require(previous[i]->polygon == current[i]->polygon && previous[i]->kind == current[i]->kind, "Upper apartment and landing boundaries must stack exactly");
    }
    auto physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::DoorLayer doors(std::move(panels), physics);
    viewer::CollisionWorld collision(model, {}, &doors);
    viewer::FloorplanLayer floors(model, doors, collision);
    doors.setAngle(0, .7f); floors.update({doors.panel(0).center});
    require(floors.residentCount() == 1 && floors.resident().begin()->second.kind == viewer::HouseFloorplan::Kind::PrefabApartmentBlock,
            "Runtime streaming must select the apartment generator and install its reserved doors");
    for (const auto& stair : plan->stairs) {
        const auto middle = (stair.bottom + stair.top) * .5f;
        const auto hit = collision.walkingGroundAt(middle.x, middle.z, middle.y - .05f, middle.y + .05f);
        require(hit && hit->normal.y > .7f, "Every repeated flight must provide walkable sloped support");
    }
    // Walk the whole stack, including lateral turns on each landing. This
    // catches opposing ramps whose openings overlap or whose exits are sealed.
    viewer::FPSController player;
    auto direction = plan->stairs.front().top - plan->stairs.front().bottom; direction.y = 0; direction = glm::normalize(direction);
    player.teleport(plan->stairs.front().bottom - direction * .5f + glm::vec3(0,.03f,0));
    for (std::size_t level = 0; level < plan->stairs.size(); ++level) {
        const auto& stair = plan->stairs[level];
        direction = stair.top - stair.bottom; direction.y = 0; direction = glm::normalize(direction);
        viewer::FPSInput input; input.forward = 1; input.yawDegrees = glm::degrees(std::atan2(direction.z,direction.x));
        bool arrived = false;
        for (int frame = 0; frame < 300; ++frame) {
            player.update(1.f/60,input,collision);
            if (glm::dot(player.feet()-stair.top,direction)>.35f && player.feet().y>=stair.top.y-.1f) { arrived=true; break; }
        }
        require(arrived, "A standing player must ascend every flight through its floor opening");
        if (level + 1 < plan->stairs.size()) {
            const auto target = plan->stairs[level+1].bottom + direction * .4f;
            bool turned = false;
            for (int frame = 0; frame < 180; ++frame) {
                auto delta = target - player.feet(); delta.y = 0;
                if (glm::length(delta)<.08f) {turned=true;break;}
                input.yawDegrees=glm::degrees(std::atan2(delta.z,delta.x));
                input.forward=std::min(1.f,glm::length(delta)*10);
                player.update(1.f/60,input,collision);
            }
            require(turned && player.feet().y>=stair.top.y-.1f, "A standing player must turn between alternating bays without falling into the incoming opening");
        }
    }
    const auto& physical = floors.resident().begin()->second.doors.front();
    const auto normal = physical.panel.outward;
    require(!collision.lineOfSight(physical.panel.center-normal*.6f,physical.panel.center+normal*.6f), "Closed apartment panels must block the common-space doorway");
    doors.setAngle(physical.id,-1.5f);
    require(collision.lineOfSight(physical.panel.center-normal*.6f,physical.panel.center+normal*.6f), "Opened apartment panels must uncover their doorway");
    const auto first = floors.resident().begin()->second;
    floors.update({}); doors.setAngle(1, .7f); floors.update({doors.panel(1).center});
    const auto& repeated = floors.resident().begin()->second;
    require(first.rooms.size() == repeated.rooms.size() && first.doors.size() == repeated.doors.size(), "Reentry must retain apartment topology and IDs");
    for (std::size_t i = 0; i < first.rooms.size(); ++i) require(first.rooms[i].polygon == repeated.rooms[i].polygon, "Apartment generation must be independent of entrance and activation order");
}
}
int main() {
    try {
        classifier(); basement();
        for (const auto size : {glm::vec3(14,48,5), {48,14,5}, {20,24,5}, {24,20,5}, {20,24,11}, {20,24,16}, {8,24,4}})
            generated(size.x,size.y,int(size.z),glm::mat4(1));
        generated(20,24,5,glm::translate(glm::mat4(1),{-1108,5,-250}) * glm::rotate(glm::mat4(1),glm::radians(31.f),glm::vec3(0,1,0)) * glm::scale(glm::mat4(1),{-1,1,1}));
        glm::mat4 skew(1); skew[2].x = .018f;
        generated(20,24,5,skew);
        generated(48,14,5,glm::translate(glm::mat4(1),{512,0,139}) * skew);
        generated(20,24,5,skew,{{-10,-12},{-10,12},{10,12},{10,1},{9.4f,1},{9.4f,-2},{10,-2},{10,-12}});
        std::cout << "Prefab classification, apartments, repeated storeys, circulation and streaming tests passed\n"; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
