#pragma once
#include "interior_fixture.hpp"
#include "house_floorplan.hpp"
#include "house_polygon.hpp"
#include <algorithm>
#include <stdexcept>

namespace section_fixture {
using Polygon=std::vector<glm::vec2>;
struct Roof { Polygon polygon; float height; };
inline float winding(const Polygon& p) {
    float sum=0;
    for (std::size_t i=0;i<p.size();++i) sum+=p[i].x*p[(i+1)%p.size()].y-p[i].y*p[(i+1)%p.size()].x;
    return sum;
}
inline viewer::Model building(Polygon outer,std::vector<Polygon> holes,std::vector<Roof> roofs,
        glm::vec2 entry,const glm::mat4& transform=glm::mat4(1)) {
    viewer::Model model; model.materials.resize(1); model.materials[0].doubleSided=true;
    if (winding(outer)>0) std::reverse(outer.begin(),outer.end());
    for (auto& hole:holes) if (winding(hole)<0) std::reverse(hole.begin(),hole.end());
    const auto quad=[&](glm::vec3 a,glm::vec3 b,glm::vec3 c,glm::vec3 d,const char* suffix,bool low=false) {
        interior_fixture::quad(model,a,b,c,d,std::string("building_sections_")+suffix,transform,low);
    };
    interior_fixture::quad(model,{-40,0,-40},{-40,0,80},{80,0,80},{80,0,-40},"terrain",transform);
    for (auto& roof:roofs) {
        if (winding(roof.polygon)>0) std::reverse(roof.polygon.begin(),roof.polygon.end());
        const auto p=[&](std::size_t i) { const auto q=roof.polygon[i]; return glm::vec3(q.x,roof.height,q.y); };
        // Independently authored convex roof pieces, triangulated as fans.
        for (std::size_t i=1;i+1<roof.polygon.size();++i) quad(p(0),p(i),p(i+1),p(i+1),"roof");
    }
    std::vector<Polygon> rings{outer}; rings.insert(rings.end(),holes.begin(),holes.end());
    for (const auto& ring:rings) for (std::size_t i=0;i<ring.size();++i) {
        const auto a=ring[i],b=ring[(i+1)%ring.size()],direction=glm::normalize(b-a);
        const float length=glm::distance(a,b);
        const glm::vec2 outward(-direction.y,direction.x);
        std::vector<float> cuts{0,length};
        for (const auto& roof:roofs) for (const auto p:roof.polygon) {
            const float along=glm::dot(p-a,direction);
            if (along>.001f && along<length-.001f && glm::distance(a+direction*along,p)<.001f) cuts.push_back(along);
        }
        std::sort(cuts.begin(),cuts.end()); cuts.erase(std::unique(cuts.begin(),cuts.end()),cuts.end());
        for (std::size_t c=1;c<cuts.size();++c) {
            const float low=cuts[c-1],high=cuts[c];
            const auto middle=a+direction*((low+high)*.5f)-outward*.01f;
            float eaves=0;
            for (const auto& roof:roofs) if (viewer::house_polygon::contains(roof.polygon,middle)) eaves=std::max(eaves,roof.height);
            if (eaves<=0) throw std::runtime_error("Fixture wall has no roof");
            const auto p=[&](float along,float y) { const auto q=a+direction*along; return glm::vec3(q.x,y,q.y); };
            const auto wall=[&](float left,float right,float bottom,float top,const char* suffix,bool lod=false) {
                quad(p(left,bottom),p(right,bottom),p(right,top),p(left,top),suffix,lod);
            };
            wall(low,high,-.08f,eaves,"walls",true);
            const float door=glm::dot(entry-a,direction);
            const bool hasDoor=door>low+.6f && door<high-.6f && glm::distance(a+direction*door,entry)<.001f;
            if (hasDoor) {
                wall(low,door-.5f,-.08f,eaves,"detailed_walls");
                wall(door+.5f,high,-.08f,eaves,"detailed_walls");
                wall(door-.5f,door+.5f,2.1f,eaves,"detailed_walls");
                wall(door-.5f,door+.5f,0,2.1f,"doors");
            } else wall(low,high,-.08f,eaves,"detailed_walls");
            for (float bottom=.9f;bottom+1.2f<eaves-.15f;bottom+=3) {
                const float center=low+(high-low)*.75f;
                if (high-low<2 || (hasDoor && std::abs(center-door)<1.3f && bottom<2.1f)) continue;
                wall(center-.6f,center+.6f,bottom,bottom+1.2f,"windows");
            }
        }
    }
    // Closed vertical roof steps between unequal-height wings. These faces
    // are above entrance level and must not become footprint branches.
    for (std::size_t i=0;i<roofs.size();++i) for (std::size_t j=i+1;j<roofs.size();++j) {
        if (roofs[i].height==roofs[j].height) continue;
        const auto& high=roofs[i].height>roofs[j].height ? roofs[i] : roofs[j];
        const auto& low=roofs[i].height>roofs[j].height ? roofs[j] : roofs[i];
        for (std::size_t e=0;e<high.polygon.size();++e) {
            const auto a=high.polygon[e],b=high.polygon[(e+1)%high.polygon.size()],u=glm::normalize(b-a);
            for (std::size_t f=0;f<low.polygon.size();++f) {
                const auto c=low.polygon[f],d=low.polygon[(f+1)%low.polygon.size()];
                const auto delta=c-a;
                if (std::abs(delta.x*u.y-delta.y*u.x)>.001f || std::abs((d-a).x*u.y-(d-a).y*u.x)>.001f) continue;
                const float first=std::max(0.f,std::min(glm::dot(c-a,u),glm::dot(d-a,u)));
                const float last=std::min(glm::distance(a,b),std::max(glm::dot(c-a,u),glm::dot(d-a,u)));
                if (last-first<.001f) continue;
                const auto p=a+u*first,q=a+u*last;
                quad({p.x,low.height,p.y},{q.x,low.height,q.y},{q.x,high.height,q.y},{p.x,high.height,p.y},"walls",true);
                quad({p.x,low.height,p.y},{q.x,low.height,q.y},{q.x,high.height,q.y},{p.x,high.height,p.y},"detailed_walls");
            }
        }
    }
    interior_fixture::bounds(model); return model;
}
inline viewer::HouseSource source(const viewer::Model& model,const std::vector<viewer::DoorPanel>& doors) {
    viewer::HouseSource result; result.name="building_sections"; result.firstDoor=doors.size();
    for (std::size_t i=0;i<model.draws.size();++i) {
        const auto& draw=model.draws[i];
        if (draw.name=="building_sections_walls" && draw.lowDetail) result.walls.push_back(i);
        if (draw.name=="building_sections_roof") result.roofs.push_back(i);
        if (draw.name=="building_sections_windows") result.windows.push_back(i);
    }
    for (std::size_t i=0;i<doors.size();++i) result.doors.push_back(i);
    return result;
}
inline viewer::Model lShape(int levels,const glm::mat4& transform=glm::mat4(1),bool mixed=false) {
    const float height=levels*3.f+.5f;
    return building({{0,0},{0,24},{8,24},{8,8},{24,8},{24,0}},{},
        {{{{0,0},{0,24},{8,24},{8,0}},height},{{{8,0},{8,8},{24,8},{24,0}},mixed ? 3.5f : height}}, {0,4},transform);
}
inline viewer::Model uShape(int levels,const glm::mat4& transform=glm::mat4(1)) {
    const float h=levels*3.f+.5f;
    return building({{0,0},{0,24},{8,24},{8,8},{16,8},{16,24},{24,24},{24,0}},{},
        {{{{0,0},{0,24},{8,24},{8,0}},h},{{{8,0},{8,8},{16,8},{16,0}},h},{{{16,0},{16,24},{24,24},{24,0}},h}}, {0,4},transform);
}
inline viewer::Model courtyard(int levels,const glm::mat4& transform=glm::mat4(1)) {
    const float h=levels*3.f+.5f;
    return building({{0,0},{0,24},{24,24},{24,0}},{{{8,8},{16,8},{16,16},{8,16}}},
        {{{{0,0},{0,24},{8,24},{8,0}},h},{{{8,0},{8,8},{16,8},{16,0}},h},
         {{{8,16},{8,24},{16,24},{16,16}},h},{{{16,0},{16,24},{24,24},{24,0}},h}}, {0,4},transform);
}
inline viewer::Model angled(int levels,const glm::mat4& transform=glm::mat4(1)) {
    const float h=levels*3.f+.5f;
    return building({{0,0},{0,8},{12,8},{24,20},{30,14},{16,0}},{},
        {{{{0,0},{0,8},{12,8},{16,0}},h},{{{12,8},{24,20},{30,14},{16,0}},h}}, {0,4},transform);
}
}
