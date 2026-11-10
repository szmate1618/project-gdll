#include "house_fixture.hpp"
#include "floorplan_layer.hpp"
#include "house_navigation.hpp"
#include "house_polygon.hpp"
#include "door_layer.hpp"
#include "collision_world.hpp"
#include "fps_controller.hpp"
#include "physics_world.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
viewer::Model building(float width, float length, int levels, const glm::mat4& transform = glm::mat4(1)) {
    auto model = house_fixture::outlined({{-width/2,-length/2},{-width/2,length/2},
        {width/2,length/2},{width/2,-length/2}}, transform, levels);
    // Keep metre-wide doors and correctly place the fixture's side windows;
    // scaling a full house would shrink doors along with the footprint.
    for (const auto& draw : model.draws) if (draw.name == "building_house_windows")
        for (auto& vertex : model.primitives[draw.primitive].vertices) vertex.position.x = -width/2;
    return model;
}
void dimensions() {
    struct Case { float width, length; viewer::HouseFloorplan::Kind kind; };
    using Kind = viewer::HouseFloorplan::Kind;
    for (const auto& test : std::vector<Case>{{4,6.25f,Kind::SmallBuilding}, {5,5,Kind::SmallBuilding},
            {4,10,Kind::SmallBuilding}, {4,6.2f,Kind::BasicBuilding}, {3.9f,8,Kind::BasicBuilding},
            {4,10.1f,Kind::BasicBuilding}, {5,9,Kind::FamilyHouse}}) {
        auto model = building(test.width, test.length, 1);
        auto physics = std::make_shared<viewer::PhysicsWorld>();
        viewer::DoorLayer doors(viewer::prepareDoorPanels(model), physics);
        viewer::CollisionWorld world(model, {}, &doors);
        viewer::FloorplanLayer floors(model, doors, world);
        doors.setAngle(0, .7f); floors.update({doors.panel(0).center});
        require(floors.residentCount() == 1 && floors.resident().begin()->second.kind == test.kind,
            "Small-building boundaries must select an open layout, family house or basic fallback");
    }
}
void geometry(const glm::mat4& transform, int levels, bool basement) {
    auto model = building(4, 8, levels, transform);
    if (basement) {
        interior_fixture::quad(model, {-.5f,-2.6f,-4},{.5f,-2.6f,-4},
            {.5f,-.5f,-4},{-.5f,-.5f,-4}, "building_house_doors", transform);
        // The fixture's flat outdoor sheet would bisect basement headroom.
        // Leave the basement interior clear rather than simulating excavation.
        model.draws.erase(std::remove_if(model.draws.begin(), model.draws.end(),
            [](const viewer::DrawInstance& draw) { return draw.name == "terrain"; }), model.draws.end());
    }
    auto physics = std::make_shared<viewer::PhysicsWorld>();
    auto panels = viewer::prepareDoorPanels(model);
    const auto exteriorCount = panels.size();
    viewer::DoorLayer doors(std::move(panels), physics);
    viewer::CollisionWorld world(model, {}, &doors);
    viewer::FloorplanLayer floors(model, doors, world);
    floors.update({doors.panel(0).center});
    require(floors.residentCount() == 0, "Small interiors must retain the normal open-door activation trigger");
    doors.setAngle(basement ? exteriorCount-1 : 0, .7f);
    floors.update({doors.panel(0).center});
    require(floors.residentCount() == 1, "Small interiors must stream through main or basement doors");
    const auto plan = floors.resident().begin()->second;
    if (plan.kind != viewer::HouseFloorplan::Kind::SmallBuilding) std::cerr << "Rejected fixture levels=" << levels
        << " basement=" << basement << " origin=" << transform[3].x << ',' << transform[3].y << ',' << transform[3].z << '\n';
    require(plan.kind == viewer::HouseFloorplan::Kind::SmallBuilding && plan.doors.empty(),
        "Small buildings must use their own category without invented internal partitions or doors");
    require(plan.rooms.size() == std::size_t(levels + (basement ? 1 : 0)) && plan.upperFloors.size() == std::size_t(levels-1),
        "Small interiors must have one open room per evidenced storey");
    for (const auto& room : plan.rooms)
        require(room.kind == viewer::HouseRoom::Kind::OpenRoom && std::string(room.name()) == "open_room" &&
                std::abs(viewer::house_polygon::area(room.polygon)-32) < .01f,
            "Open rooms must cover the actual footprint without assuming residential use");
    const auto point = [&](glm::vec3 p) { return glm::vec3(transform * glm::vec4(p,1)); };
    for (int level = 0; level < levels; ++level) for (float x = -1.75f; x < 2; x += .5f) for (float z = -3.75f; z < 4; z += .5f) {
        const auto p = point({x, level == 0 ? .01f : level*3.f, z});
        bool opening = false;
        for (const auto& stair : plan.stairs)
            if (std::abs(stair.top.y-p.y)<.01f && viewer::house_polygon::contains(stair.opening,{p.x,p.z})) opening=true;
        if (opening) continue;
        require(world.walkingGroundAt(p.x,p.z,p.y-.004f,p.y+.004f).has_value(),
            "Small-building floors must support walkers throughout every storey");
    }
    require(plan.stairs.size() == std::size_t(levels-1 + (basement && levels>1 ? 1 : 0)),
        "Window-derived storeys must connect by safe stairs; single storeys need no invented flights");
    for (const auto& stair : plan.stairs) {
        auto direction=stair.top-stair.bottom; direction.y=0; direction=glm::normalize(direction);
        viewer::FPSController player;
        player.teleport(stair.bottom-direction*.5f+glm::vec3(0,.03f,0));
        viewer::FPSInput input; input.forward=1; input.yawDegrees=glm::degrees(std::atan2(direction.z,direction.x));
        bool arrived=false;
        for (int frame=0;frame<360;++frame) {
            player.update(1.f/60,input,world);
            if (glm::dot(player.feet()-stair.top,direction)>.35f && player.feet().y>=stair.top.y-.1f) { arrived=true; break; }
        }
        if (!arrived) std::cerr << "levels=" << levels << " basement=" << basement << " ramp="
            << stair.bottom.x << ',' << stair.bottom.y << ',' << stair.bottom.z << " -> "
            << stair.top.x << ',' << stair.top.y << ',' << stair.top.z << " feet="
            << player.feet().x << ',' << player.feet().y << ',' << player.feet().z << '\n';
        require(arrived,"Standing players must walk up every small-building ramp through its floor cutout");
    }
    const auto* graph=floors.navigationGraph(plan.name);
    require(graph,"Small buildings must publish navigation for their open rooms");
    std::vector<glm::vec3> centers;
    for (const auto& node:graph->nodes()) if (node.kind==viewer::HouseNavigationGraph::Kind::RoomCenter) centers.push_back(node.point);
    require(centers.size()==plan.rooms.size(),"Every small-building room must have a supported navigation center");
    if (!basement || levels>1) for (const auto center:centers)
        require(!graph->route(centers.front(),center+glm::vec3(0,1.6f,0)).points.empty(),"Small-building storeys must be reachable through their ramps");
    floors.update({}); require(floors.residentCount()==0,"Small buildings must evict without observers");
    doors.setAngle(1,.7f); floors.update({doors.panel(1).center});
    const auto& repeated=floors.resident().begin()->second;
    require(repeated.rooms.size()==plan.rooms.size() && repeated.stairs.size()==plan.stairs.size(),"Small buildings must regenerate deterministically");
    for (std::size_t i=0;i<plan.structure.primitives.size();++i) {
        const auto& a=plan.structure.primitives[i]; const auto& b=repeated.structure.primitives[i];
        require(a.indices==b.indices && a.vertices.size()==b.vertices.size(),"Reentry must preserve small-building topology");
        for (std::size_t j=0;j<a.vertices.size();++j) require(a.vertices[j].position==b.vertices[j].position,"Reentry must preserve exact small-building coordinates");
    }
}
void irregular() {
    const std::vector<std::vector<glm::vec2>> outlines{
        {{-3,-3},{-3,3},{1,3},{1,1},{3,1},{3,-3}},
        {{-3.5f,-3.5f},{-3.5f,3.5f},{1.5f,3.5f},{1.5f,.5f},{3.5f,.5f},{3.5f,-3.5f}}};
    for (std::size_t i = 0; i < outlines.size(); ++i) {
        auto model = house_fixture::outlined(outlines[i], glm::mat4(1), 1);
        auto physics = std::make_shared<viewer::PhysicsWorld>();
        viewer::DoorLayer doors(viewer::prepareDoorPanels(model), physics);
        viewer::CollisionWorld world(model, {}, &doors);
        viewer::FloorplanLayer floors(model, doors, world);
        doors.setAngle(0, .7f); floors.update({doors.panel(0).center});
        const auto& plan = floors.resident().begin()->second;
        require(plan.kind == viewer::HouseFloorplan::Kind::SmallBuilding && plan.footprint.size() == 6 &&
                std::abs(viewer::house_polygon::area(plan.rooms.front().polygon) - (i == 0 ? 32 : 43)) < .01f,
            "Small concave buildings must retain their full boundary, including when only the actual area is below 45 m2");
        require(world.walkingGroundAt(0,0,.005f,.015f).has_value() && !world.walkingGroundAt(2,2,.005f,.015f),
            "Small-building floors must preserve exterior notches");
    }
}
void storeyEvidence() {
    for (const bool missingWindows : {false, true}) {
        auto model = building(5, 5, 2);
        if (missingWindows) {
            model.draws.erase(std::remove_if(model.draws.begin(), model.draws.end(),
                [](const viewer::DrawInstance& draw) { return draw.name == "building_house_windows"; }), model.draws.end());
        } else {
            // A four-metre rise cannot fit a standing-player ramp in 5 x 5 m.
            for (const auto& draw : model.draws) for (auto& vertex : model.primitives[draw.primitive].vertices)
                if (vertex.position.y > 6 || (draw.name == "building_house_windows" && vertex.position.y > 3))
                    vertex.position.y += 1;
        }
        auto physics = std::make_shared<viewer::PhysicsWorld>();
        viewer::DoorLayer doors(viewer::prepareDoorPanels(model), physics);
        viewer::CollisionWorld world(model, {}, &doors);
        viewer::FloorplanLayer floors(model, doors, world);
        doors.setAngle(0, .7f); floors.update({doors.panel(0).center});
        const auto& plan = floors.resident().begin()->second;
        require(plan.kind == (missingWindows ? viewer::HouseFloorplan::Kind::SmallBuilding : viewer::HouseFloorplan::Kind::BasicBuilding) &&
                plan.upperFloors.empty() && plan.stairs.empty(),
            "Missing window rows must not invent storeys; unfittable ramps must retain the basic fallback");
    }
}
}
int main() {
    try {
        dimensions(); irregular(); storeyEvidence();
        const auto moved=glm::translate(glm::mat4(1),{87,5,-39})*glm::rotate(glm::mat4(1),glm::radians(31.f),glm::vec3(0,1,0))*glm::scale(glm::mat4(1),{-1,1,1});
        for (const auto transform:{glm::mat4(1),moved}) for (int levels:{1,2,3}) {
            geometry(transform,levels,false); geometry(transform,levels,true);
        }
        std::cout<<"Small-building classification, open rooms, walking, navigation and streaming tests passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
