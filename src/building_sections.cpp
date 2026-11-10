#include "building_sections.hpp"
#include "house_polygon.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace viewer {
namespace {
using Polygon = house_polygon::Polygon;
constexpr float tolerance = .002f;
float cross(glm::vec2 a, glm::vec2 b) { return a.x*b.y-a.y*b.x; }
float signedArea(const Polygon& p) {
    double sum=0;
    for (std::size_t i=0;i<p.size();++i) sum+=double(cross(p[i]-p.front(),p[(i+1)%p.size()]-p.front()));
    return float(sum*.5);
}
Polygon canonical(Polygon p) {
    bool changed=true;
    while (changed && p.size()>3) {
        changed=false;
        for (std::size_t i=0;i<p.size();++i) {
            const auto a=p[(i+p.size()-1)%p.size()],b=p[i],c=p[(i+1)%p.size()];
            if (glm::distance(a,b)<.0001f || std::abs(cross(b-a,c-b))<.0001f*std::max(glm::distance(a,b),glm::distance(b,c))) {
                p.erase(p.begin()+static_cast<std::ptrdiff_t>(i)); changed=true; break;
            }
        }
    }
    if (signedArea(p)<0) std::reverse(p.begin(),p.end());
    const auto first=std::min_element(p.begin(),p.end(),[](auto a,auto b) { return a.x<b.x || (a.x==b.x && a.y<b.y); });
    std::rotate(p.begin(),first,p.end());
    return p;
}
std::optional<std::pair<glm::vec2,glm::vec2>> shared(const Polygon& a,const Polygon& b) {
    std::optional<std::pair<glm::vec2,glm::vec2>> result;
    float longest=0;
    for (std::size_t i=0;i<a.size();++i) {
        const auto start=a[i],edge=a[(i+1)%a.size()]-start;
        const float length=glm::length(edge);
        const auto u=edge/length;
        for (std::size_t j=0;j<b.size();++j) {
            const auto c=b[j]-start,d=b[(j+1)%b.size()]-start;
            if (std::abs(cross(c,u))>tolerance || std::abs(cross(d,u))>tolerance) continue;
            const float low=std::max(0.f,std::min(glm::dot(c,u),glm::dot(d,u)));
            const float high=std::min(length,std::max(glm::dot(c,u),glm::dot(d,u)));
            if (high-low>longest) { longest=high-low; result={{start+u*low,start+u*high}}; }
        }
    }
    return longest>tolerance ? result : std::nullopt;
}
Polygon convexHull(Polygon p) {
    std::sort(p.begin(),p.end(),[](auto a,auto b) { return a.x<b.x || (a.x==b.x && a.y<b.y); });
    p.erase(std::unique(p.begin(),p.end(),[](auto a,auto b) { return glm::distance(a,b)<.0001f; }),p.end());
    if (p.size()<3) return {};
    Polygon hull;
    for (const auto q:p) {
        while (hull.size()>1 && cross(hull.back()-hull[hull.size()-2],q-hull.back())<=0) hull.pop_back();
        hull.push_back(q);
    }
    const auto lower=hull.size();
    for (auto it=p.rbegin()+1;it!=p.rend();++it) {
        while (hull.size()>lower && cross(hull.back()-hull[hull.size()-2],*it-hull.back())<=0) hull.pop_back();
        hull.push_back(*it);
    }
    hull.pop_back();
    return canonical(std::move(hull));
}
// Clip a roof face in 3D by a convex horizontal section, retaining interpolated
// roof heights. Boundary-only contacts with a lower neighbour have zero area.
std::vector<glm::vec3> roofIntersection(const building_shell::Face& face,const Polygon& section) {
    std::vector<glm::vec3> result(face.begin(),face.end());
    for (std::size_t i=0;i<section.size() && !result.empty();++i) {
        const auto a=section[i],edge=section[(i+1)%section.size()]-a;
        const auto side=[&](glm::vec3 p) { return cross(edge,glm::vec2(p.x,p.z)-a); };
        std::vector<glm::vec3> next;
        for (std::size_t j=0;j<result.size();++j) {
            const auto p=result[j],q=result[(j+1)%result.size()];
            const float first=side(p),second=side(q);
            if (first>=0) next.push_back(p);
            if ((first>=0)!=(second>=0)) next.push_back(glm::mix(p,q,first/(first-second)));
        }
        result=std::move(next);
    }
    return result;
}
}
std::optional<std::pair<glm::vec2,glm::vec2>> sharedSectionBoundary(const Polygon& first,const Polygon& second) {
    return shared(first,second);
}
std::optional<BuildingSections> prepareBuildingSections(const Model& model,const HouseSource& source,
        const BuildingFootprints& footprints) {
    const auto* input=footprints.rings(source.name);
    if (!input || input->empty() || input->size()>16 || source.roofs.empty() || source.walls.empty()) return {};
    std::vector<Polygon> rings;
    std::size_t points=0;
    for (const auto& ring:*input) { rings.push_back(canonical(ring)); points+=rings.back().size(); }
    if (points>128) return {}; // Existing interior-sun shader boundary capacity.
    const auto outer=std::max_element(rings.begin(),rings.end(),[](const auto& a,const auto& b) { return house_polygon::area(a)<house_polygon::area(b); });
    BuildingSections result; result.footprint=*outer;
    float expected=house_polygon::area(*outer);
    for (auto it=rings.begin();it!=rings.end();++it) if (it!=outer) {
        if (!house_polygon::contains(*outer,it->front())) return {}; // Disconnected component.
        for (auto other=rings.begin();other!=rings.end();++other)
            if (other!=outer && other!=it && house_polygon::contains(*other,it->front())) return {};
        result.courtyards.push_back(*it); expected-=house_polygon::area(*it);
    }
    if (expected<25 || expected>10000) return {};
    const auto origin=outer->front();
    glm::vec2 u(1,0); float longest=0;
    for (const auto& ring:rings) for (std::size_t i=0;i<ring.size();++i) {
        const auto d=ring[(i+1)%ring.size()]-ring[i];
        if (glm::dot(d,d)>longest) { longest=glm::dot(d,d); u=glm::normalize(d); }
    }
    if (u.x<-.001f || (std::abs(u.x)<=.001f && u.y<0)) u=-u;
    const glm::vec2 v(-u.y,u.x);
    const auto local=[&](glm::vec2 p) { const auto d=p-origin; return glm::vec2(glm::dot(d,u),glm::dot(d,v)); };
    const auto world=[&](glm::vec2 p) { return origin+u*p.x+v*p.y; };
    std::vector<float> cuts;
    for (auto& ring:rings) for (auto& p:ring) { p=local(p); cuts.push_back(p.x); }
    std::sort(cuts.begin(),cuts.end());
    cuts.erase(std::unique(cuts.begin(),cuts.end(),[](float a,float b) { return std::abs(a-b)<.0001f; }),cuts.end());
    bool orthogonal=true;
    for (const auto& ring:rings) for (std::size_t i=0;i<ring.size();++i) {
        const auto direction=glm::normalize(ring[(i+1)%ring.size()]-ring[i]);
        orthogonal &= std::min(std::abs(direction.x),std::abs(direction.y))<=.087156f;
    }
    if (result.courtyards.empty() && outer->size()==4 && orthogonal) return {};
    struct Crossing { float middle,left,right; };
    std::vector<Polygon> cells;
    for (std::size_t cut=1;cut<cuts.size();++cut) {
        const float left=cuts[cut-1],right=cuts[cut],middle=(left+right)*.5f;
        std::vector<Crossing> edges;
        for (const auto& ring:rings) for (std::size_t i=0;i<ring.size();++i) {
            const auto a=ring[i],b=ring[(i+1)%ring.size()];
            if (!((a.x<middle && b.x>middle)||(b.x<middle && a.x>middle))) continue;
            const auto at=[&](float x) { return a.y+(b.y-a.y)*(x-a.x)/(b.x-a.x); };
            edges.push_back({at(middle),at(left),at(right)});
        }
        std::sort(edges.begin(),edges.end(),[](auto a,auto b) { return a.middle<b.middle; });
        if (edges.size()%2) return {};
        for (std::size_t i=0;i<edges.size();i+=2) {
            auto p=canonical({{left,edges[i].left},{right,edges[i].right},{right,edges[i+1].right},{left,edges[i+1].left}});
            if (house_polygon::area(p)<.00001f) continue;
            if (!house_polygon::simple(p)) return {};
            cells.push_back(std::move(p));
            if (cells.size()>512) return {};
        }
    }
    // Merge only convex unions whose area equals the two original regions.
    // A hull that bridges a recess or courtyard necessarily gains area.
    bool merged=true;
    while (merged) {
        merged=false;
        for (std::size_t i=0;i<cells.size() && !merged;++i) for (std::size_t j=i+1;j<cells.size();++j) {
            if (!shared(cells[i],cells[j])) continue;
            auto points=cells[i]; points.insert(points.end(),cells[j].begin(),cells[j].end());
            auto hull=convexHull(std::move(points));
            const float sum=house_polygon::area(cells[i])+house_polygon::area(cells[j]);
            if (hull.size()>12 || std::abs(house_polygon::area(hull)-sum)>.001f) continue;
            cells[i]=std::move(hull); cells.erase(cells.begin()+static_cast<std::ptrdiff_t>(j)); merged=true; break;
        }
    }
    if (cells.empty() || cells.size()>64) return {};
    float sum=0;
    for (const auto& cell:cells) sum+=house_polygon::area(cell);
    if (std::abs(sum-expected)>std::max(.02f,expected*.00001f)) return {};
    std::sort(cells.begin(),cells.end(),[](const auto& a,const auto& b) { return a.front().x<b.front().x || (a.front().x==b.front().x && a.front().y<b.front().y); });
    const auto roof=building_shell::faces(model,source.roofs);
    for (const auto& cell:cells) {
        Polygon boundary;
        for (const auto p:cell) boundary.push_back(world(p));
        boundary=canonical(std::move(boundary));
        building_shell::Shell shell;
        shell.origin=boundary.front(); longest=0;
        for (std::size_t i=0;i<boundary.size();++i) {
            const auto d=boundary[(i+1)%boundary.size()]-boundary[i];
            if (glm::dot(d,d)>longest) { longest=glm::dot(d,d); shell.u=glm::normalize(d); }
        }
        if (shell.u.x<-.001f || (std::abs(shell.u.x)<=.001f && shell.u.y<0)) shell.u=-shell.u;
        shell.v={-shell.u.y,shell.u.x};
        shell.low=glm::vec2(std::numeric_limits<float>::max()); shell.high=-shell.low;
        for (const auto p:boundary) {
            const auto q=shell.local(p); shell.outline.push_back(q); shell.low=glm::min(shell.low,q); shell.high=glm::max(shell.high,q);
        }
        shell.eaves=std::numeric_limits<float>::max(); shell.roofTop=-shell.eaves;
        float coverage=0;
        for (const auto& face:roof) {
            const auto clipped=roofIntersection(face,boundary);
            Polygon projection; for (const auto p:clipped) projection.push_back({p.x,p.z});
            const float area=house_polygon::area(projection);
            if (area<.0001f) continue;
            coverage+=area;
            for (const auto p:clipped) { shell.eaves=std::min(shell.eaves,p.y); shell.roofTop=std::max(shell.roofTop,p.y); }
        }
        if (coverage<=0 || std::abs(coverage-house_polygon::area(boundary))>std::max(.03f,coverage*.0001f) || !std::isfinite(shell.eaves)) return {};
        shell.closed=true;
        result.sections.push_back({std::move(shell)});
    }
    for (std::size_t i=0;i<cells.size();++i) for (std::size_t j=i+1;j<cells.size();++j)
        if (const auto span=shared(cells[i],cells[j]); span && glm::distance(span->first,span->second)>=.8f)
            result.passages.push_back({i,j,world(span->first),world(span->second)});
    std::vector<bool> visited(cells.size()); visited[0]=true;
    for (std::size_t n=0;n<cells.size();++n) for (const auto& passage:result.passages)
        if (visited[passage.first] || visited[passage.second]) visited[passage.first]=visited[passage.second]=true;
    if (std::find(visited.begin(),visited.end(),false)!=visited.end()) return {};
    return result;
}
}
