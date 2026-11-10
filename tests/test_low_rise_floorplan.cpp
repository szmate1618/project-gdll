#include "apartment_fixture.hpp"
#include "low_rise_floorplan.hpp"
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
void classification() {
    struct Case { float width, length; int levels; bool lowRise; };
    for (const auto& test : std::vector<Case>{{12,40,1,true}, {24,24,3,true}, {40,45,2,true},
            {10,18,1,true}, {10,17,1,false}, {7,40,2,false}, {40,60,2,false}, {8,65,2,false}, {24,24,4,false}}) {
        auto model = apartment_fixture::block(test.width,test.length,test.levels);
        const auto panels = viewer::prepareDoorPanels(model);
        const auto source = apartment_fixture::source(model,panels);
        require(bool(viewer::classifyLowRiseBuilding(model,source)) == test.lowRise,
            "Low-rise classification must enforce area, width, aspect and wall-height limits");
    }
}
void variants(float width, float length, int levels, viewer::LowRiseLayout expected, const glm::mat4& transform) {
    auto model = apartment_fixture::block(width,length,levels,transform);
    auto panels = viewer::prepareDoorPanels(model);
    const auto source = apartment_fixture::source(model,panels);
    const auto classification = viewer::classifyLowRiseBuilding(model,source);
    require(classification && classification->preferredLayout == expected,
        "Shell shape must select corridor, compact or open-hall preferences without building-use assumptions");
    auto physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::DoorLayer doors(std::move(panels),physics);
    viewer::CollisionWorld world(model,{},&doors);
    viewer::FloorplanLayer floors(model,doors,world);
    doors.setAngle(0,.7f); floors.update({doors.panel(0).center});
    require(floors.residentCount() == 1,"Low-rise interiors must stream through existing exterior doors");
    const auto plan = floors.resident().begin()->second;
    require(plan.kind == viewer::HouseFloorplan::Kind::LowRiseBuilding && plan.upperFloors.size() == std::size_t(levels-1) &&
            plan.stairs.size() == std::size_t(levels-1),"Low-rise interiors must use all evidenced storeys with connecting stairs");
    if (expected == viewer::LowRiseLayout::OpenHall)
        require(plan.doors.empty() && plan.rooms.size() == std::size_t(levels),"Open halls must have one unpartitioned room per storey");
    else require(!plan.doors.empty(),"Fittable corridor and compact fixtures must install independent rooms and doors");
    float roomArea = 0;
    for (const auto& room : plan.rooms) {
        require(room.kind != viewer::HouseRoom::Kind::Apartment && room.kind != viewer::HouseRoom::Kind::Living &&
                room.kind != viewer::HouseRoom::Kind::Anteroom && room.kind != viewer::HouseRoom::Kind::SmallRoom,
            "General low-rise rooms must not be labelled as residential rooms or apartments");
        roomArea += viewer::house_polygon::area(room.polygon);
    }
    require(std::abs(roomArea-width*length*levels) < .15f,"Low-rise rooms and circulation must tile every actual storey");
    require(plan.doors.size() <= viewer::lowRiseDoorCapacity(*classification,levels,source.doors.size()),
        "Low-rise layouts must fit their deterministic runtime door reservation");
    for (const auto& door : plan.doors) {
        require(doors.present(door.id),"All generated low-rise doors must be installed in their reserved slots");
        doors.setAngle(door.id,door.panel.inward ? -1.5f : 1.5f);
    }
    const auto inverse = glm::inverse(transform);
    for (const auto& mesh : plan.structure.primitives) for (const auto& vertex : mesh.vertices) {
        const auto p = glm::vec3(inverse*glm::vec4(vertex.position,1));
        require(std::abs(p.x) <= width*.5f+.01f && std::abs(p.z) <= length*.5f+.01f,
            "Low-rise floors, partitions and ramps must remain within the shell");
    }
    const auto* graph = floors.navigationGraph(plan.name);
    require(graph,"Low-rise interiors must publish navigation for rooms and common circulation");
    std::vector<glm::vec3> centers;
    for (const auto& node : graph->nodes()) if (node.kind == viewer::HouseNavigationGraph::Kind::RoomCenter) centers.push_back(node.point);
    require(centers.size() == plan.rooms.size(),"Every low-rise room must have a supported navigation center");
    for (const auto center : centers) {
        const auto path = graph->route(centers.front(),center+glm::vec3(0,1.6f,0));
        require(!path.points.empty(),"All low-rise rooms and upper floors must be reachable through circulation and stairs");
        for (std::size_t i = 1; i < path.points.size(); ++i) {
            const auto a = path.points[i-1].point, b = path.points[i].point;
            const int samples = std::max(1,int(std::ceil(glm::distance(a,b)/.2f)));
            for (int sample = 0; sample <= samples; ++sample) {
                const auto p = glm::mix(a,b,float(sample)/samples);
                require(world.walkingGroundAt(p.x,p.z,p.y-.08f,p.y+.08f).has_value(),
                    "Low-rise navigation edges must retain floor or ramp support");
            }
        }
    }
    for (const auto& stair : plan.stairs) {
        auto direction = stair.top-stair.bottom; direction.y = 0; direction = glm::normalize(direction);
        viewer::FPSController player;
        player.teleport(stair.bottom-direction*.5f+glm::vec3(0,.03f,0));
        viewer::FPSInput input; input.forward = 1; input.yawDegrees = glm::degrees(std::atan2(direction.z,direction.x));
        bool arrived = false;
        for (int frame = 0; frame < 360; ++frame) {
            player.update(1.f/60,input,world);
            if (glm::dot(player.feet()-stair.top,direction)>.35f && player.feet().y>=stair.top.y-.1f) { arrived=true; break; }
        }
        require(arrived,"Standing players must climb low-rise ramps through their floor cutouts");
    }
    floors.update({});
    require(floors.residentCount() == 0,"Low-rise interiors must evict when observers leave");
    doors.setAngle(1,.7f); floors.update({doors.panel(1).center});
    const auto& repeated = floors.resident().begin()->second;
    require(repeated.rooms.size() == plan.rooms.size() && repeated.doors.size() == plan.doors.size() &&
            repeated.structure.primitives.size() == plan.structure.primitives.size(),"Low-rise reentry must reproduce the topology");
    for (std::size_t i = 0; i < plan.structure.primitives.size(); ++i) {
        const auto& a = plan.structure.primitives[i]; const auto& b = repeated.structure.primitives[i];
        require(a.indices == b.indices && a.vertices.size() == b.vertices.size(),"Low-rise reentry must retain triangulation");
        for (std::size_t j = 0; j < a.vertices.size(); ++j) require(a.vertices[j].position == b.vertices[j].position,
            "Low-rise reentry must retain exact geometry coordinates");
    }
}
void fallbacks() {
    for (const bool missingWindows : {false,true}) {
        auto model = apartment_fixture::block(12,40,2);
        if (missingWindows) model.draws.erase(std::remove_if(model.draws.begin(),model.draws.end(),
            [](const viewer::DrawInstance& draw) { return draw.name == "building_prefab_windows"; }),model.draws.end());
        else {
            // A pane along each complete wall prevents any partition endpoint
            // from keeping 20 cm of facade clearance. Open floors remain safe.
            model.draws.erase(std::remove_if(model.draws.begin(),model.draws.end(),
                [](const viewer::DrawInstance& draw) { return draw.name == "building_prefab_windows"; }),model.draws.end());
            for (const float x : {-6.f,6.f}) for (const float y : {.9f,3.9f})
                interior_fixture::quad(model,{x,y,-20},{x,y,20},{x,y+1.2f,20},{x,y+1.2f,-20},"building_prefab_windows");
            for (const float z : {-20.f,20.f}) for (const float y : {.9f,3.9f})
                interior_fixture::quad(model,{-6,y,z},{6,y,z},{6,y+1.2f,z},{-6,y+1.2f,z},"building_prefab_windows");
        }
        const auto panels = viewer::prepareDoorPanels(model);
        const auto source = apartment_fixture::source(model,panels);
        const auto building = viewer::classifyLowRiseBuilding(model,source);
        require(bool(building),"Fallback fixture must pass the low-rise shell classification");
        const auto plan = viewer::generateLowRiseFloorplan(model,source,panels,*building);
        require(bool(plan),"Low-rise partition failures must retain a usable open layout");
        if (missingWindows) require(plan->upperFloors.empty() && plan->stairs.empty(),"Missing window rows must not invent upper storeys");
        else require(plan->doors.empty() && plan->rooms.size() == 2,"Unfittable partitions must fall back to open rooms on both storeys");
    }
}
void shellGeometry() {
    auto model = apartment_fixture::block(20,30,2,glm::mat4(1),
        {{-10,-15},{-10,15},{5,15},{5,10},{10,10},{10,-15}});
    const auto panels = viewer::prepareDoorPanels(model);
    const auto source = apartment_fixture::source(model,panels);
    const auto building = viewer::classifyLowRiseBuilding(model,source);
    require(bool(building),"Modest low-rise recesses must retain shell classification");
    const auto plan = viewer::generateLowRiseFloorplan(model,source,panels,*building);
    require(bool(plan),"A concave low-rise shell must fit a partitioned or open layout");
    viewer::CollisionWorld world(plan->structure);
    require(world.walkingGroundAt(0,0,.005f,.015f).has_value() && !world.walkingGroundAt(8,12,.005f,.015f),
        "Low-rise floor geometry must preserve exterior recesses");
    for (const auto& mesh : plan->structure.primitives) for (std::size_t i=0;i<mesh.indices.size();i+=3) {
        const auto a=mesh.vertices[mesh.indices[i]].position,b=mesh.vertices[mesh.indices[i+1]].position,c=mesh.vertices[mesh.indices[i+2]].position;
        if (std::abs(glm::cross(b-a,c-a).y)<.001f) continue;
        const auto p=(a+b+c)/3.f;
        require(plan->contains({p.x,p.z}),"Low-rise floor and ramp triangles must not bridge exterior recesses");
    }
    for (const bool courtyard : {false,true}) {
        auto invalid = apartment_fixture::block(24,24,2);
        if (courtyard) {
            const std::array<glm::vec2,4> ring{{{-2,-2},{-2,2},{2,2},{2,-2}}};
            for (std::size_t i=0;i<ring.size();++i) {
                const auto a=ring[i],b=ring[(i+1)%ring.size()];
                interior_fixture::quad(invalid,{a.x,-.08f,a.y},{b.x,-.08f,b.y},{b.x,6.5f,b.y},{a.x,6.5f,a.y},
                    "building_prefab_walls",glm::mat4(1),true);
            }
        } else invalid.draws.erase(std::remove_if(invalid.draws.begin(),invalid.draws.end(),
            [](const viewer::DrawInstance& draw) { return draw.name == "building_prefab_roof"; }),invalid.draws.end());
        const auto doors=viewer::prepareDoorPanels(invalid);
        require(!viewer::classifyLowRiseBuilding(invalid,apartment_fixture::source(invalid,doors)),
            "Courtyard boundaries and missing roofs must not receive a generic low-rise layout");
    }
}
void basement() {
    auto model = apartment_fixture::block(12,40,2);
    interior_fixture::quad(model,{-.5f,-2.6f,-20},{.5f,-2.6f,-20},{.5f,-.5f,-20},{-.5f,-.5f,-20},"building_prefab_doors");
    const auto panels = viewer::prepareDoorPanels(model);
    const auto source = apartment_fixture::source(model,panels);
    const auto building = viewer::classifyLowRiseBuilding(model,source);
    require(bool(building),"Basement doors must not change low-rise shell classification");
    const auto plan = viewer::generateLowRiseFloorplan(model,source,panels,*building);
    require(plan && plan->basementFloor && plan->stairs.size() == 2 && plan->stairs.back().ramp == plan->stairs.front().ramp,
        "Low-rise basements must stack a connecting flight under the existing upward flight");
    require(plan->rooms.back().kind == viewer::HouseRoom::Kind::OpenRoom &&
            plan->rooms.back().storey == viewer::HouseFloorplan::basementStorey && plan->rooms.back().entrances.size() == 1,
        "Low-rise basements must retain their lower entrance and a neutral room label");
    viewer::CollisionWorld world(plan->structure);
    const auto& stair = plan->stairs.back(); const auto p = (stair.bottom+stair.top)*.5f;
    require(world.walkingGroundAt(p.x,p.z,p.y-.001f,p.y+.001f).has_value() &&
            !world.walkingGroundAt(p.x,p.z,stair.top.y-.001f,stair.top.y+.001f),
        "Low-rise basement ramps must retain walking support through a real ground-floor cutout");
}
}
int main() {
    try {
        classification(); fallbacks(); shellGeometry(); basement();
        const auto moved = glm::translate(glm::mat4(1),{87,5,-39})*glm::rotate(glm::mat4(1),glm::radians(31.f),glm::vec3(0,1,0))*glm::scale(glm::mat4(1),{-1,1,1});
        for (const auto transform : {glm::mat4(1),moved}) for (const int levels : {1,2,3}) {
            variants(12,40,levels,viewer::LowRiseLayout::CorridorRooms,transform);
            variants(24,24,levels,viewer::LowRiseLayout::CompactRooms,transform);
            variants(40,45,levels,viewer::LowRiseLayout::OpenHall,transform);
        }
        std::cout << "Low-rise variants, circulation, stairs, walking, fallbacks and streaming tests passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
