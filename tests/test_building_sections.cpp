#include "section_fixture.hpp"
#include "building_sections.hpp"
#include "complex_floorplan.hpp"
#include "floorplan_layer.hpp"
#include "house_navigation.hpp"
#include "door_layer.hpp"
#include "collision_world.hpp"
#include "physics_world.hpp"
#include "fps_controller.hpp"
#include "model_merge.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
bool inside(const section_fixture::Polygon& p,glm::vec2 point) {
    int crossings=0;
    for (std::size_t i=0;i<p.size();++i) {
        const auto a=p[i],b=p[(i+1)%p.size()];
        if ((a.y>point.y)!=(b.y>point.y) && point.x<a.x+(b.x-a.x)*(point.y-a.y)/(b.y-a.y)) ++crossings;
    }
    return crossings%2;
}
void check(viewer::Model model,bool courtyard,bool mixed) {
    auto panels=viewer::prepareDoorPanels(model);
    require(panels.size()==1,"Section fixture must retain its authored exterior entrance");
    const auto source=section_fixture::source(model,panels);
    viewer::BuildingFootprints footprints(model);
    const auto prepared=viewer::prepareBuildingSections(model,source,footprints);
    if (!prepared || prepared->sections.size()<2 || prepared->passages.empty()) {
        const auto* rings=footprints.rings(source.name);
        std::cerr<<"Decomposition failed: rings="<<(rings ? rings->size() : 0)<<" sections="<<(prepared ? prepared->sections.size() : 0)<<'\n';
        if (rings) for (const auto& ring:*rings) { for (const auto p:ring) std::cerr<<p.x<<','<<p.y<<' '; std::cerr<<'\n'; }
    }
    require(prepared && prepared->sections.size()>1 && !prepared->passages.empty(),"Complex footprints must decompose into connected wings");
    require(prepared->courtyards.size()==(courtyard ? 1u : 0u),"Section decomposition must preserve courtyard rings");
    float floorArea=0;
    for (const auto& section:prepared->sections) floorArea+=viewer::house_polygon::area(section.shell.outline);
    float expected=viewer::house_polygon::area(prepared->footprint);
    for (const auto& hole:prepared->courtyards) expected-=viewer::house_polygon::area(hole);
    require(std::abs(floorArea-expected)<.05f,"Section regions must exactly tile the shell minus courtyards");
    auto physics=std::make_shared<viewer::PhysicsWorld>();
    viewer::DoorLayer doors(std::move(panels),physics);
    viewer::CollisionWorld collision(model,{},&doors);
    viewer::FloorplanLayer floors(model,doors,collision);
    floors.update({doors.panel(0).center}); require(floors.residentCount()==0,"Closed doors must not activate complex interiors");
    doors.setAngle(0,1.5f); floors.update({doors.panel(0).center});
    require(floors.residentCount()==1,"A complex building must retain a usable interior");
    const auto plan=floors.resident().begin()->second;
    if (plan.kind!=viewer::HouseFloorplan::Kind::ComplexBuilding) {
        std::cerr<<"Layout fallback: sections="<<prepared->sections.size()<<" courtyard="<<courtyard<<" mixed="<<mixed<<" kind="<<int(plan.kind)<<'\n';
        for (const auto p:prepared->footprint) { std::cerr<<p.x<<','<<p.y<<' '; }
        std::cerr<<'\n';
        for (const auto mode:{viewer::ComplexLayout::Rooms,viewer::ComplexLayout::OpenSections}) {
            const auto candidate=viewer::generateComplexFloorplan(model,source,std::vector<viewer::DoorPanel>{doors.panel(0)},*prepared,mode);
            std::cerr<<"mode="<<int(mode)<<" generated="<<bool(candidate);
            if (candidate) {
                viewer::CollisionWorld support(candidate->structure); viewer::HouseNavigationGraph nav(*candidate,doors,support,0);
                std::cerr<<" rooms="<<candidate->rooms.size()<<" passages="<<candidate->passages.size()<<" stairs="<<candidate->stairs.size()<<" connected="<<nav.reachesAllRooms(source.doors);
                if (mode==viewer::ComplexLayout::OpenSections) {
                    for (std::size_t n=0;n<nav.nodes().size();++n) {
                        const auto& node=nav.nodes()[n];
                        std::cerr<<"\nnode "<<n<<" kind="<<int(node.kind)<<" level="<<node.storey<<" at="<<node.point.x<<','<<node.point.y<<','<<node.point.z<<" edges=";
                        for (const auto& edge:nav.edges()[n]) std::cerr<<edge.to<<',';
                    }
                }
            }
            std::cerr<<'\n';
        }
    }
    require(plan.kind==viewer::HouseFloorplan::Kind::ComplexBuilding,"Valid wings must produce a specialized complex interior");
    require(!plan.passages.empty(),"Section seams must have explicit open passages");
    float groundRooms=0;
    for (const auto& room:plan.rooms) if (room.storey==0) groundRooms+=viewer::house_polygon::area(room.polygon);
    require(std::abs(groundRooms-expected)<.06f,"Ground rooms must tile the whole exact footprint without overlaps or missing sections");
    const auto* graph=floors.navigationGraph(plan.name);
    require(graph && graph->reachesAllRooms(source.doors),"Every complex room and upper floor must be reachable from the exterior entrance");
    for (const auto& node:graph->nodes()) if (node.kind==viewer::HouseNavigationGraph::Kind::RoomCenter) {
        require(collision.walkingGroundAt(node.point.x,node.point.z,node.point.y-.05f,node.point.y+.05f).has_value(),
            "Complex navigation centers must stand on generated floors");
    }
    for (const auto& edges:graph->edges()) for (const auto& edge:edges) for (std::size_t i=1;i<edge.points.size();++i) {
        const auto a=edge.points[i-1],b=edge.points[i];
        const int samples=std::max(1,int(std::ceil(glm::distance(a,b)/.2f)));
        for (int sample=0;sample<=samples;++sample) {
            const auto p=glm::mix(a,b,float(sample)/samples);
            const auto support=collision.walkingGroundAt(p.x,p.z,p.y-.08f,p.y+.08f);
            if (!support) std::cerr<<"Unsupported graph edge: at="<<p.x<<','<<p.y<<','<<p.z<<" from="<<a.x<<','<<a.y<<','<<a.z
                <<" to="<<b.x<<','<<b.y<<','<<b.z<<" courtyard="<<courtyard<<" mixed="<<mixed<<'\n';
            require(support.has_value(),
                "Every navigation edge must retain support across section seams and ramp landings");
        }
    }
    for (const auto& mesh:plan.structure.primitives) for (std::size_t i=0;i<mesh.indices.size();i+=3) {
        const auto a=mesh.vertices[mesh.indices[i]].position,b=mesh.vertices[mesh.indices[i+1]].position,c=mesh.vertices[mesh.indices[i+2]].position;
        if (std::abs(glm::cross(b-a,c-a).y)<.001f) continue;
        for (const auto weight:{glm::vec3(.34f,.33f,.33f),glm::vec3(.8f,.1f,.1f),glm::vec3(.1f,.8f,.1f),glm::vec3(.1f,.1f,.8f)}) {
            const auto p=a*weight.x+b*weight.y+c*weight.z;
            require(inside(prepared->footprint,{p.x,p.z}),"Complex floor/ramp triangles must stay inside the exact outer boundary");
            for (const auto& hole:prepared->courtyards) require(!inside(hole,{p.x,p.z}),"No complex floor, ceiling or ramp may fill a courtyard");
        }
    }
    require(plan.doors.size()<=viewer::complexDoorCapacity(*prepared,viewer::houseUpperStoreyCount(model,source)+1,source.doors.size()),
        "Complex doors must fit their stable capacity without reserving IDs at activation");
    for (const auto& door:plan.doors) {
        require(doors.present(door.id),"Complex interior door IDs must be installed");
        doors.setAngle(door.id,door.panel.inward ? -1.5f : 1.5f);
    }
    {
        const auto& door=doors.panel(0);
        viewer::FPSController player;
        auto start=door.center+door.outward*.8f; start.y=plan.floor+.03f; player.teleport(start);
        viewer::FPSInput input; input.forward=1; input.yawDegrees=glm::degrees(std::atan2(-door.outward.z,-door.outward.x));
        bool entered=false;
        for (int frame=0;frame<180;++frame) {
            player.update(1.f/60,input,collision);
            if (glm::dot(player.feet()-door.center,-door.outward)>.7f) { entered=true; break; }
        }
        require(entered,"A standing player must actually enter the section building through its prepared door");
    }
    for (const auto& stair:plan.stairs) {
        auto direction=stair.top-stair.bottom; direction.y=0; direction=glm::normalize(direction);
        viewer::FPSController player; player.teleport(stair.bottom-direction*.5f+glm::vec3(0,.03f,0));
        viewer::FPSInput input; input.forward=1; input.yawDegrees=glm::degrees(std::atan2(direction.z,direction.x));
        bool arrived=false;
        for (int frame=0;frame<360;++frame) {
            player.update(1.f/60,input,collision);
            if (glm::dot(player.feet()-stair.top,direction)>.35f && player.feet().y>=stair.top.y-.1f) { arrived=true; break; }
        }
        require(arrived,"Standing players must climb section ramps through their cutouts");
    }
    if (mixed) {
        bool low=false,high=false;
        for (const auto& section:prepared->sections) { low|=section.shell.eaves-plan.floor<4; high|=section.shell.eaves-plan.floor>6; }
        require(low && high,"Entrance-level tracing must ignore upper step walls and retain both roof heights");
        require(!plan.upperFloors.empty(),"A high wing must retain its evidenced upper floor beside a lower wing");
        for (const auto& volume:plan.volumes) {
            glm::vec2 center(0); for (const auto p:volume.footprint) center+=p/float(volume.footprint.size());
            require(plan.containsInterior({center.x,volume.floor+1,center.y}) &&
                    !plan.containsInterior({center.x,volume.ceiling+.1f,center.y}),
                "Lower roof tops must remain outside the interior beside a taller wing");
        }
        for (const auto& room:plan.rooms) if (room.storey>0) for (const auto p:room.polygon) {
            bool supported=false;
            for (const auto& section:prepared->sections)
                if (section.shell.eaves>plan.floorHeight(room.storey)+2.4f &&
                    viewer::house_polygon::contains(section.shell.outline,section.shell.local(p))) supported=true;
            require(supported,"Upper rooms must remain below their own wing's roof");
        }
    }
    floors.update({}); require(floors.residentCount()==0,"Complex interiors must evict after observers leave");
    floors.update({doors.panel(0).center});
    const auto& repeated=floors.resident().begin()->second;
    require(plan.rooms.size()==repeated.rooms.size() && plan.doors.size()==repeated.doors.size() &&
            plan.passages.size()==repeated.passages.size(),"Complex topology and door IDs must regenerate deterministically");
    for (std::size_t i=0;i<plan.structure.primitives.size();++i) {
        const auto& a=plan.structure.primitives[i]; const auto& b=repeated.structure.primitives[i];
        require(a.indices==b.indices && a.vertices.size()==b.vertices.size(),"Section triangulation must survive reentry exactly");
        for (std::size_t j=0;j<a.vertices.size();++j) require(a.vertices[j].position==b.vertices[j].position,"Section coordinates must survive reentry exactly");
    }
}
void disconnected() {
    auto model=section_fixture::building({{0,0},{0,8},{8,8},{8,0}},{},{{{{0,0},{0,8},{8,8},{8,0}},3.5f}}, {0,4});
    auto other=section_fixture::building({{16,0},{16,8},{24,8},{24,0}},{},{{{{16,0},{16,8},{24,8},{24,0}},3.5f}}, {16,4});
    const auto offset=model.primitives.size();
    for (auto draw:other.draws) if (draw.name!="terrain") {
        draw.primitive+=offset; model.draws.push_back(draw);
    }
    model.primitives.insert(model.primitives.end(),other.primitives.begin(),other.primitives.end());
    const auto panels=viewer::prepareDoorPanels(model); const auto source=section_fixture::source(model,panels);
    viewer::BuildingFootprints footprints(model);
    require(!viewer::prepareBuildingSections(model,source,footprints),"Disconnected components must not receive invented connections");
}
void activationOrder() {
    auto model=section_fixture::lShape(2);
    auto second=section_fixture::uShape(2,glm::translate(glm::mat4(1),{80,0,0}));
    for (auto& draw:second.draws) if (draw.name.rfind("building_sections_",0)==0)
        draw.name.replace(0,17,"building_second");
    viewer::appendStaticModel(model,std::move(second));
    auto replica=model;
    auto physicsA=std::make_shared<viewer::PhysicsWorld>(),physicsB=std::make_shared<viewer::PhysicsWorld>();
    viewer::DoorLayer a(viewer::prepareDoorPanels(model),physicsA),b(viewer::prepareDoorPanels(replica),physicsB);
    viewer::CollisionWorld worldA(model,{},&a),worldB(replica,{},&b);
    viewer::FloorplanLayer first(model,a,worldA),last(replica,b,worldB);
    for (std::size_t i=0;i<2;++i) { a.setAngle(i,1.5f); b.setAngle(i,1.5f); }
    first.update({a.panel(0).center}); last.update({b.panel(1).center});
    require(first.residentCount()==1 && last.residentCount()==1,"Opposite observers must initially stream different section buildings");
    first.update({a.panel(0).center,a.panel(1).center}); last.update({b.panel(0).center,b.panel(1).center});
    require(first.residentCount()==2 && last.residentCount()==2,"Both section buildings must remain independently resident");
    for (const auto& [name,plan]:first.resident()) {
        const auto& other=last.resident().at(name);
        require(plan.kind==viewer::HouseFloorplan::Kind::ComplexBuilding && other.kind==plan.kind && plan.doors.size()==other.doors.size() &&
                plan.rooms.size()==other.rooms.size() && plan.passages.size()==other.passages.size(),"Activation order must not select different section topology");
        for (std::size_t i=0;i<plan.doors.size();++i)
            require(plan.doors[i].id==other.doors[i].id && plan.doors[i].panel.center==other.doors[i].panel.center,
                "Section doors must retain the same reserved IDs in opposite activation orders");
    }
}
}
int main() {
    try {
        disconnected(); activationOrder();
        const auto moved=glm::translate(glm::mat4(1),{-1108,5,-250})*glm::rotate(glm::mat4(1),glm::radians(31.f),glm::vec3(0,1,0))*glm::scale(glm::mat4(1),{-1,1,1});
        for (const auto transform:{glm::mat4(1),moved}) for (const int levels:{1,2,3}) {
            check(section_fixture::lShape(levels,transform),false,false);
            check(section_fixture::uShape(levels,transform),false,false);
            check(section_fixture::courtyard(levels,transform),true,false);
            check(section_fixture::angled(levels,transform),false,false);
        }
        check(section_fixture::lShape(2,glm::mat4(1),true),false,true);
        check(section_fixture::lShape(2,moved,true),false,true);
        std::cout<<"Complex wings, courtyards, angled sections, roof limits, walking and connectivity tests passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
