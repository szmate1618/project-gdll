#include "complex_floorplan.hpp"
#include "apartment_layout.hpp"
#include "building_storeys.hpp"
#include "house_basement.hpp"
#include "house_polygon.hpp"
#include "model_merge.hpp"
#include "open_building_layout.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <iterator>
#include <stdexcept>

namespace viewer {
namespace {
using Polygon=house_polygon::Polygon;
bool fitsSection(const house_layout::Layout& layout,const building_shell::Shell& shell) {
    for (const auto& room:layout.rooms)
        if (room.polygon.empty() && house_polygon::clip(shell.outline,room.bounds.low,room.bounds.high).empty()) return false;
    for (const auto& wall:layout.partitions) {
        const auto spans=house_polygon::spans(shell.outline,wall.axis,wall.position,wall.low,wall.high);
        auto openings=wall.openings;
        std::sort(openings.begin(),openings.end(),[](const auto& a,const auto& b) { return a.low<b.low; });
        float previous=std::numeric_limits<float>::lowest();
        for (const auto& opening:openings) {
            if (opening.low<previous || !std::any_of(spans.begin(),spans.end(),[&](const auto& span) {
                    return opening.low>=span.first && opening.high<=span.second;
                })) return false;
            previous=opening.high;
        }
    }
    return true;
}
std::vector<house_layout::Window> windowsForSection(const std::vector<building_shell::Face>& faces,const building_shell::Shell& shell) {
    std::vector<house_layout::Window> result;
    for (const auto& face:faces) {
        house_layout::Rectangle bounds{glm::vec2(std::numeric_limits<float>::max()),glm::vec2(std::numeric_limits<float>::lowest())};
        bool touches=false;
        for (const auto p:face) {
            const auto q=shell.local({p.x,p.z}); bounds.low=glm::min(bounds.low,q); bounds.high=glm::max(bounds.high,q);
            touches |= house_polygon::contains(shell.outline,q,.05f);
        }
        if (touches) result.push_back({bounds,std::min({face[0].y,face[1].y,face[2].y}),std::max({face[0].y,face[1].y,face[2].y})});
    }
    return result;
}
house_layout::Layout wingLayout(const HouseFloorplan& plan,const building_shell::Shell& shell,
        const std::vector<house_layout::Entrance>& accesses,const std::vector<house_layout::Window>& windows,ComplexLayout mode) {
    const house_layout::Rectangle bounds{shell.low,shell.high};
    if (mode==ComplexLayout::Rooms) {
        std::vector<house_layout::Rectangle> groundWindows,allWindows;
        for (const auto& window:windows) {
            allWindows.push_back(window.bounds);
            if (window.bottom<plan.ceilingHeight(0) && window.top>plan.floor) groundWindows.push_back(window.bounds);
        }
        const auto size=shell.high-shell.low;
        const float width=std::min(size.x,size.y),length=std::max(size.x,size.y);
        const bool aligned=std::all_of(accesses.begin(),accesses.end(),[](const auto& door) {
            return std::max(std::abs(door.outward.x),std::abs(door.outward.y))>=.99f;
        });
        if (aligned) {
            const bool slab=width>=8 && width<=22 && length>=20 && length>=width*1.5f;
            const bool compact=width>=12 && length<=35 && length<=width*1.8f;
            if (slab || compact) if (auto layout=apartment_layout::generate(plan.name,bounds,shell.outline,slab,
                    plan.floor,plan.upperFloors,accesses,allWindows)) return *layout;
            auto layout=house_layout::generate(plan.name,bounds,accesses,plan.floor,plan.upperFloors,groundWindows,shell.outline);
            if (!layout.rooms.empty() && (plan.upperFloors.empty() || !layout.stairs.empty()) &&
                house_layout::generateUpperStoreys(layout,plan.name,bounds,plan.upperFloors,plan.ceiling,windows,shell.outline)) return layout;
        }
    }
    std::vector<house_layout::Rectangle> reserved;
    for (const auto& access:accesses) {
        const glm::vec2 across(-access.outward.y,access.outward.x);
        house_layout::Rectangle approach{glm::vec2(std::numeric_limits<float>::max()),glm::vec2(std::numeric_limits<float>::lowest())};
        for (const float depth:{0.f,.9f}) for (const float side:{-.45f,.45f}) {
            const auto p=access.center-access.outward*depth+across*side;
            approach.low=glm::min(approach.low,p); approach.high=glm::max(approach.high,p);
        }
        reserved.push_back(approach);
    }
    if (auto layout=generateOpenBuildingLayout(plan,shell,accesses,reserved)) return *layout;
    // Another connected wing can supply the staircase. Final navigation
    // validation rejects any upper room that cannot actually reach an entrance.
    house_layout::Layout layout;
    for (std::size_t level=0;level<=plan.upperFloors.size();++level) {
        std::vector<std::size_t> doors;
        if (level==0) for (const auto& entry:accesses) doors.push_back(entry.id);
        layout.rooms.push_back({HouseRoom::Kind::OpenRoom,bounds,std::move(doors),level});
    }
    return layout;
}
}
std::size_t complexDoorCapacity(const BuildingSections& building,std::size_t storeys,std::size_t exteriorDoors) {
    storeys=std::min<std::size_t>(storeys,16);
    std::size_t bays=0;
    for (const auto& section:building.sections) {
        const auto size=section.shell.high-section.shell.low;
        bays+=2*static_cast<std::size_t>(std::ceil(std::max(size.x,size.y)/3))+8;
    }
    return storeys*bays+6*(exteriorDoors+2*building.passages.size());
}
std::optional<HouseFloorplan> generateComplexFloorplan(const Model& model,const HouseSource& source,
        const std::vector<DoorPanel>& entrances,const BuildingSections& building,ComplexLayout mode) {
    if (source.doors.empty() || building.sections.empty()) return {};
    float eaves=std::numeric_limits<float>::lowest();
    for (const auto& section:building.sections) eaves=std::max(eaves,section.shell.eaves);
    const auto windowFaces=building_shell::faces(model,source.windows);
    auto storeys=inferBuildingStoreys(eaves,building_shell::windowHeads(windowFaces),source,entrances,50);
    if (!storeys) return {};
    while (!storeys->upperFloors.empty() && storeys->ceiling-storeys->upperFloors.back()<2.4f) storeys->upperFloors.pop_back();
    if (storeys->upperFloors.size()>15) return {};
    HouseFloorplan result;
    result.kind=HouseFloorplan::Kind::ComplexBuilding; result.name=source.name;
    result.floor=storeys->floor; result.ceiling=storeys->ceiling; result.upperFloors=storeys->upperFloors;
    result.footprint=building.footprint; result.courtyards=building.courtyards;
    std::vector<std::pair<std::size_t,std::size_t>> roomRanges;
    for (std::size_t index=0;index<building.sections.size();++index) {
        const auto& shell=building.sections[index].shell;
        if (shell.eaves-.02f-result.floor<2.4f) return {};
        HouseFloorplan wing;
        wing.name=source.name+"_section_"+std::to_string(index); wing.floor=result.floor; wing.ceiling=shell.eaves-.02f;
        for (const auto floor:result.upperFloors) if (wing.ceiling-floor>=2.4f) wing.upperFloors.push_back(floor);
        const auto windows=windowsForSection(windowFaces,shell);
        for (const auto& window:windows) for (const auto floor:wing.upperFloors)
            if (window.bottom<floor-.02f && window.top>floor+.02f) return {};
        HouseSource wingSource=source; wingSource.name=wing.name; wingSource.firstDoor=source.firstDoor+result.doors.size(); wingSource.doors.clear();
        std::vector<house_layout::Entrance> accesses;
        for (const auto id:source.doors) {
            const auto& door=entrances.at(id);
            const auto inside=door.center-door.outward*.3f;
            if (!house_polygon::contains(shell.outline,shell.local({inside.x,inside.z}))) continue;
            wingSource.doors.push_back(id);
            if (isBasementEntrance(door,result.floor)) continue;
            const glm::vec2 normal(door.outward.x,door.outward.z);
            accesses.push_back({id,shell.local({door.center.x,door.center.z}),{glm::dot(normal,shell.u),glm::dot(normal,shell.v)},std::min(1.f,door.halfExtents.x*2)});
        }
        for (std::size_t p=0;p<building.passages.size();++p) {
            const auto& passage=building.passages[p];
            if (passage.first!=index && passage.second!=index) continue;
            const auto center=(passage.a+passage.b)*.5f;
            const auto edge=glm::normalize(passage.b-passage.a);
            glm::vec2 normal(edge.y,-edge.x);
            if (house_polygon::contains(shell.outline,shell.local(center+normal*.2f))) normal=-normal;
            accesses.push_back({entrances.size()+2*p+(passage.second==index),shell.local(center),
                {glm::dot(normal,shell.u),glm::dot(normal,shell.v)},std::min(1.f,glm::distance(passage.a,passage.b)-.2f)});
        }
        auto layout=wingLayout(wing,shell,accesses,windows,mode);
        // Several section passages can request nearby entry openings on one
        // facade. Reject overlapping or clipped subdivisions before emission.
        if (!fitsSection(layout,shell)) layout=wingLayout(wing,shell,accesses,windows,ComplexLayout::OpenSections);
        if (!fitsSection(layout,shell)) return {};
        addHouseBasement(wing,layout,wingSource,entrances,shell);
        if (wing.basementFloor) {
            if (result.basementFloor && std::abs(*result.basementFloor-*wing.basementFloor)>.05f) return {};
            result.basementFloor=wing.basementFloor;
        }
        for (auto& room:layout.rooms) {
            room.entrances.erase(std::remove_if(room.entrances.begin(),room.entrances.end(),[&](auto id) { return id>=entrances.size(); }),room.entrances.end());
            if (room.kind==HouseRoom::Kind::Living || room.kind==HouseRoom::Kind::SmallRoom || room.kind==HouseRoom::Kind::Apartment) room.kind=HouseRoom::Kind::OpenRoom;
            if (room.kind==HouseRoom::Kind::Anteroom) room.kind=HouseRoom::Kind::Lobby;
        }
        for (auto& wall:layout.partitions) for (auto& opening:wall.openings)
            if (opening.entrance && *opening.entrance>=entrances.size()) opening.entrance.reset();
        // Independent wing transforms at kilometre-scale map coordinates can
        // round a ramp tip and the meeting slab edge to opposite sides. Keep
        // a 2 mm support overlap at the upper landing rather than a hairline gap.
        for (auto& stair:layout.stairs) {
            if (stair.ascending) stair.opening.high[stair.axis]-=.002f;
            else stair.opening.low[stair.axis]+=.002f;
        }
        const auto first=result.rooms.size();
        house_layout::buildGeometry(wing,wingSource,layout,shell.origin,shell.u,shell.v,{shell.low,shell.high},shell.outline);
        result.volumes.push_back({wing.footprint,wing.basementFloor.value_or(wing.floor),wing.ceiling});
        for (auto& stair:wing.stairs) { stair.room+=first; result.stairs.push_back(std::move(stair)); }
        result.rooms.insert(result.rooms.end(),std::make_move_iterator(wing.rooms.begin()),std::make_move_iterator(wing.rooms.end()));
        result.doors.insert(result.doors.end(),std::make_move_iterator(wing.doors.begin()),std::make_move_iterator(wing.doors.end()));
        appendStaticModel(result.structure,std::move(wing.structure));
        roomRanges.emplace_back(first,result.rooms.size());
    }
    for (const auto& passage:building.passages) {
        const auto first=roomRanges[passage.first],second=roomRanges[passage.second];
        for (std::size_t a=first.first;a<first.second;++a) for (std::size_t b=second.first;b<second.second;++b) {
            if (result.rooms[a].storey!=result.rooms[b].storey) continue;
            const auto span=sharedSectionBoundary(result.rooms[a].polygon,result.rooms[b].polygon);
            if (span && glm::distance(span->first,span->second)>=.8f) result.passages.push_back({a,b,span->first,span->second});
        }
    }
    if (result.doors.size()>complexDoorCapacity(building,storeys->upperFloors.size()+1,source.doors.size()))
        throw std::logic_error("Complex-building door reservation was exceeded");
    result.minimum=glm::vec3(std::numeric_limits<float>::max()); result.maximum=-result.minimum;
    for (const auto p:result.footprint) {
        result.minimum=glm::min(result.minimum,{p.x,result.basementFloor.value_or(result.floor),p.y});
        result.maximum=glm::max(result.maximum,{p.x,result.ceiling,p.y});
    }
    const glm::vec2 low(result.minimum.x,result.minimum.z),high(result.maximum.x,result.maximum.z);
    result.envelope={{low,{low.x,high.y},high,{high.x,low.y}}};
    result.structure.boundsMin=result.minimum; result.structure.boundsMax=result.maximum;
    return result;
}
}
