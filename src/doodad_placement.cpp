#include "doodad_placement.hpp"
#include "collision_world.hpp"
#include "forest_layer.hpp"

#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace viewer {
namespace {
std::uint64_t mix(std::uint64_t value) {
    value ^= value >> 30; value *= 0xbf58476d1ce4e5b9ull;
    value ^= value >> 27; value *= 0x94d049bb133111ebull;
    return value ^ (value >> 31);
}
float unit(std::uint64_t value) { return float(mix(value) >> 40)*(1.f/16777216.f); }
double cross(glm::dvec2 a, glm::dvec2 b) { return a.x*b.y-a.y*b.x; }
bool onSegment(glm::dvec2 point, glm::dvec2 a, glm::dvec2 b) {
    return std::abs(cross(point-a,b-a)) < 1e-9 && glm::dot(point-a,point-b) <= 1e-9;
}
bool segmentsIntersect(glm::dvec2 a, glm::dvec2 b, glm::dvec2 c, glm::dvec2 d) {
    return (cross(b-a,c-a)*cross(b-a,d-a)<0 && cross(d-c,a-c)*cross(d-c,b-c)<0) ||
        onSegment(a,c,d) || onSegment(b,c,d) || onSegment(c,a,b) || onSegment(d,a,b);
}
bool overlaps(const ForestPolygon& polygon, DoodadCell cell) {
    const double x=double(cell.x)*doodadCellSize, north=-double(cell.z+1)*doodadCellSize;
    const std::array<glm::dvec2,4> corners{{{x,north},{x+doodadCellSize,north},
        {x+doodadCellSize,north+doodadCellSize},{x,north+doodadCellSize}}};
    for (const auto point:corners) if (forestContains(polygon,point)) return true;
    const auto insideCell=[&](glm::dvec2 p) {
        return p.x>x && p.x<x+doodadCellSize && p.y>north && p.y<north+doodadCellSize;
    };
    for (const auto point:polygon.exterior) if (insideCell(point)) return true;
    const auto crossing=[&](const std::vector<glm::dvec2>& ring) {
        for (std::size_t i=1;i<ring.size();++i)
            for (std::size_t edge=0;edge<corners.size();++edge)
                if (segmentsIntersect(ring[i-1],ring[i],corners[edge],corners[(edge+1)%4])) return true;
        return false;
    };
    if (crossing(polygon.exterior)) return true;
    // A concave clearing can cover all four cell corners while leaving a
    // woodland sliver in the middle; its boundary must participate too.
    for (const auto& hole:polygon.holes) if (crossing(hole)) return true;
    return false;
}
}

DoodadCell doodadCellAt(glm::vec3 position) {
    if (!std::isfinite(position.x)||!std::isfinite(position.y)||!std::isfinite(position.z))
        throw std::invalid_argument("Doodad position must be finite");
    const double x=std::floor(double(position.x)/doodadCellSize);
    const double z=std::floor(double(position.z)/doodadCellSize);
    // Leave headroom for bounds calculations in the neighboring cell.
    constexpr double limit=std::numeric_limits<int>::max()-2.0;
    if (std::abs(x)>limit || std::abs(z)>limit) throw std::invalid_argument("Doodad position exceeds cell range");
    return {static_cast<int>(x),static_cast<int>(z)};
}

DoodadPlacementGenerator::DoodadPlacementGenerator(const ForestLayer& forests,
        const CollisionWorld& world, std::uint32_t seed) : world_(&world),seed_(seed) {
    for (const auto& patch:forests.patches) {
        const auto& polygon=patch.footprint;
        if (polygon.exterior.empty()) continue;
        validateForestPolygon(polygon);
        auto low=polygon.exterior.front(),high=low;
        for (const auto p:polygon.exterior) {low=glm::min(low,p);high=glm::max(high,p);}
        const auto first=doodadCellAt({float(low.x),0,float(-high.y)});
        const auto last=doodadCellAt({float(high.x),0,float(-low.y)});
        const std::int64_t count=(std::int64_t(last.x)-first.x+1)*(std::int64_t(last.z)-first.z+1);
        if (count>1000000) throw std::invalid_argument("Forest footprint exceeds doodad index limit");
        for (int x=first.x;x<=last.x;++x) for (int z=first.z;z<=last.z;++z)
            patches_[{x,z}].push_back(&polygon);
    }
}

bool DoodadPlacementGenerator::intersectsForest(DoodadCell cell) const {
    const auto entry=patches_.find(cell);
    if (entry==patches_.end()) return false;
    for (const auto* polygon:entry->second) if (overlaps(*polygon,cell)) return true;
    return false;
}

std::vector<DoodadPlacement> DoodadPlacementGenerator::generate(DoodadCell cell) const {
    std::vector<DoodadPlacement> result;
    if (!intersectsForest(cell)) return result;
    const auto& polygons=patches_.at(cell);
    const auto cellHash=mix((std::uint64_t(std::uint32_t(cell.x))<<32)|std::uint32_t(cell.z));
    result.reserve(64);
    for (unsigned candidate=0;candidate<64;++candidate) {
        const auto key=mix(cellHash^(std::uint64_t(seed_)<<16)^candidate);
        const float x=float(cell.x)*doodadCellSize+(float(candidate%8)+.15f+.7f*unit(key^0x73a5ull))*2;
        const float z=float(cell.z)*doodadCellSize+(float(candidate/8)+.15f+.7f*unit(key^0x17b9ull))*2;
        const glm::dvec2 eastNorth(x,-double(z));
        bool forest=false;
        for (const auto* polygon:polygons) if (forestContains(*polygon,eastNorth)) {forest=true;break;}
        if (!forest) continue;
        const auto ground=world_->groundAt(x,z,-1000000,1000000,true);
        if (!ground || !std::isfinite(ground->height) || ground->normal.y<.65f) continue;
        const glm::vec3 position(x,ground->height,z);
        if (world_->insideBuilding(position,.4f,.3f)) continue;
        const float type=unit(key^0x63a7ull);
        const auto kind=type<.48f?DoodadKind::Branch:type<.98f?DoodadKind::Stone:DoodadKind::BeerBottle;
        const auto yaw=glm::angleAxis(unit(key^0x8df9ull)*glm::two_pi<float>(),glm::vec3(0,1,0));
        const auto normal=glm::normalize(ground->normal);
        // Align the broad flat undersides to the rendered terrain triangle.
        // Applying local yaw afterwards retains that plane at every heading.
        const auto tilt=glm::normalize(glm::quat(1+normal.y,normal.z,0,-normal.x));
        const auto rotation=kind==DoodadKind::BeerBottle?yaw:tilt*yaw;
        const float scale=kind==DoodadKind::BeerBottle?1.f:.75f+.5f*unit(key^0xe8c3ull);
        result.push_back({key,kind,position,rotation,scale});
    }
    return result;
}
} // namespace viewer
