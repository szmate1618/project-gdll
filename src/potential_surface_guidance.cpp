#include "potential_surface_guidance.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace viewer::potential_surface {
namespace {
bool finite(Point p) { return std::isfinite(p.x) && std::isfinite(p.z); }
bool finite(Bounds b) { return std::isfinite(b.minX) && std::isfinite(b.minZ) && std::isfinite(b.maxX) && std::isfinite(b.maxZ); }
Bounds outward(Bounds b) {
    const auto infinity=std::numeric_limits<double>::infinity();
    return {std::nextafter(b.minX,-infinity),std::nextafter(b.minZ,-infinity),
            std::nextafter(b.maxX,infinity),std::nextafter(b.maxZ,infinity)};
}
bool contains(Bounds b,Point p) { return p.x>=b.minX && p.x<=b.maxX && p.z>=b.minZ && p.z<=b.maxZ; }
bool overlaps(Bounds a,Bounds b) { return a.minX<=b.maxX && b.minX<=a.maxX && a.minZ<=b.maxZ && b.minZ<=a.maxZ; }
Bounds unite(Bounds a,Bounds b) {
    return {std::min(a.minX,b.minX),std::min(a.minZ,b.minZ),std::max(a.maxX,b.maxX),std::max(a.maxZ,b.maxZ)};
}
double ringArea(const std::vector<Point>& ring) {
    const auto origin=ring.front();
    double twice=0;
    for (std::size_t i=0;i<ring.size();++i) {
        const auto a=ring[i]-origin,b=ring[(i+1)%ring.size()]-origin;
        twice+=a.x*b.z-a.z*b.x;
    }
    return std::abs(twice)*.5;
}
void validateRing(const std::vector<Point>& ring) {
    if (ring.size()<3) throw std::invalid_argument("Guidance polygon ring requires three vertices");
    for (auto p:ring) if (!finite(p)) throw std::invalid_argument("Nonfinite guidance ring vertex");
    if (!std::isfinite(ringArea(ring)) || ringArea(ring)<=0) throw std::invalid_argument("Degenerate guidance ring");
}
Bounds ringBounds(const std::vector<Point>& ring) {
    Bounds bounds{ring[0].x,ring[0].z,ring[0].x,ring[0].z};
    for (auto p:ring) bounds=unite(bounds,{p.x,p.z,p.x,p.z});
    return outward(bounds);
}
// Return -1 outside, 0 on the closed boundary, +1 strictly inside. There is no
// geometric tolerance: certification/physical safety remain the baker/World's.
int ringRelation(Point p,const std::vector<Point>& ring) {
    bool inside=false;
    for (std::size_t i=0,j=ring.size()-1;i<ring.size();j=i++) {
        const auto a=ring[j],b=ring[i],edge=b-a,relative=p-a;
        const double cross=edge.x*relative.z-edge.z*relative.x;
        if (cross==0 && p.x>=std::min(a.x,b.x) && p.x<=std::max(a.x,b.x) &&
            p.z>=std::min(a.z,b.z) && p.z<=std::max(a.z,b.z)) return 0;
        if ((a.z>p.z)!=(b.z>p.z) && a.x+(p.z-a.z)*(b.x-a.x)/(b.z-a.z)>p.x) inside=!inside;
    }
    return inside?1:-1;
}
bool pocketContains(const Pocket& pocket,Point position,bool closed) {
    const int outer=ringRelation(position,pocket.outer);
    if (outer<0 || (!closed && outer==0)) return false;
    for (const auto& hole:pocket.holes) {
        const int inner=ringRelation(position,hole);
        if (inner>0 || (!closed && inner==0)) return false;
    }
    return true;
}
struct Index {
    struct Node { Bounds box;std::uint32_t begin=0,count=0,left=0,right=0; };
    std::vector<Bounds> boxes;
    std::vector<std::uint32_t> order;
    std::vector<Node> nodes;
    explicit Index(std::vector<Bounds> source):boxes(std::move(source)) {
        if (boxes.size()>std::numeric_limits<std::uint32_t>::max()/2) throw std::invalid_argument("Guidance index too large");
        order.resize(boxes.size());std::iota(order.begin(),order.end(),0u);
        std::size_t leaves=1;while (leaves*8<boxes.size()) leaves*=2;
        if (!boxes.empty()) { nodes.reserve(leaves*2-1);build(0,std::uint32_t(order.size())); }
    }
    std::uint32_t build(std::uint32_t begin,std::uint32_t end) {
        auto bounds=boxes[order[begin]];
        for (auto i=begin+1;i<end;++i) bounds=unite(bounds,boxes[order[i]]);
        const auto id=std::uint32_t(nodes.size());nodes.push_back({bounds,begin,end-begin,0,0});
        if (end-begin<=8) return id;
        const bool x=bounds.maxX-bounds.minX>=bounds.maxZ-bounds.minZ;
        const auto middle=begin+(end-begin)/2;
        std::nth_element(order.begin()+begin,order.begin()+middle,order.begin()+end,[&](auto a,auto b) {
            const auto ca=x?(boxes[a].minX+boxes[a].maxX)*.5:(boxes[a].minZ+boxes[a].maxZ)*.5;
            const auto cb=x?(boxes[b].minX+boxes[b].maxX)*.5:(boxes[b].minZ+boxes[b].maxZ)*.5;
            return ca<cb || (ca==cb && a<b);
        });
        const auto left=build(begin,middle),right=build(middle,end);
        nodes[id].count=0;nodes[id].left=left;nodes[id].right=right;return id;
    }
    template<class Callback> void query(Bounds region,GuidanceStats& stats,Callback callback) const {
        ++stats.queries;
        if (nodes.empty()) return;
        std::array<std::uint32_t,64> stack{};std::size_t count=1;
        while (count) {
            const auto& node=nodes[stack[--count]];++stats.nodes;
            if (!overlaps(node.box,region)) continue;
            if (node.count) {
                for (auto i=node.begin;i<node.begin+node.count;++i) {
                    const auto id=order[i];
                    if (!overlaps(boxes[id],region)) continue;
                    ++stats.candidates;callback(id);
                }
            } else { stack[count++]=node.right;stack[count++]=node.left; }
        }
    }
    template<class Callback> void query(Point p,GuidanceStats& stats,Callback callback) const {
        query(Bounds{p.x,p.z,p.x,p.z},stats,callback);
    }
    std::size_t bytes() const {
        return boxes.capacity()*sizeof(Bounds)+order.capacity()*sizeof(std::uint32_t)+nodes.capacity()*sizeof(Node);
    }
};
std::vector<Bounds> bumpBounds(const std::vector<CoarseBump>& bumps) {
    std::vector<Bounds> result;result.reserve(bumps.size());
    for (const auto& b:bumps) {
        if (!finite(b.center) || !finite(b.radii) || b.radii.x<=0 || b.radii.z<=0 ||
            !std::isfinite(b.weight) || b.weight<0 || !finite(b.box) || b.box.minX>b.box.maxX || b.box.minZ>b.box.maxZ)
            throw std::invalid_argument("Invalid coarse guidance bump");
        const auto box=outward({b.center.x-b.radii.x,b.center.z-b.radii.z,b.center.x+b.radii.x,b.center.z+b.radii.z});
        if (!finite(box)) throw std::invalid_argument("Coarse guidance support overflow");
        result.push_back(box);
    }
    return result;
}
std::vector<Bounds> pocketBounds(const std::vector<Pocket>& pockets) {
    std::vector<Bounds> result;result.reserve(pockets.size());
    for (const auto& p:pockets) {
        const auto b=ringBounds(p.outer);
        result.push_back(outward({b.minX-12,b.minZ-12,b.maxX+12,b.maxZ+12}));
    }
    return result;
}
constexpr double pocketSupport=12,pocketLength=2;
Point closestBoundary(Point position,const Pocket& pocket) {
    Point nearest;double best=std::numeric_limits<double>::infinity();
    const auto ring=[&](const std::vector<Point>& vertices) {
        for (std::size_t i=0;i<vertices.size();++i) {
            const auto a=vertices[i],edge=vertices[(i+1)%vertices.size()]-a;
            const double squared=dot(edge,edge);
            const double t=squared>0?std::clamp(dot(position-a,edge)/squared,0.0,1.0):0;
            const auto candidate=a+edge*t,offset=position-candidate;
            const double distance=dot(offset,offset);
            if (distance<best) { best=distance;nearest=candidate; }
        }
    };
    ring(pocket.outer);for (const auto& hole:pocket.holes) ring(hole);
    return nearest;
}
bool crossing(Point a,Point b,Point c,Point d) {
    const auto turn=[](Point p,Point q,Point r) { const auto u=q-p,v=r-p;return u.x*v.z-u.z*v.x; };
    const auto on=[](Point p,Point q,Point r) {
        return r.x>=std::min(p.x,q.x) && r.x<=std::max(p.x,q.x) &&
               r.z>=std::min(p.z,q.z) && r.z<=std::max(p.z,q.z);
    };
    const double x=turn(a,b,c),y=turn(a,b,d),u=turn(c,d,a),v=turn(c,d,b);
    if ((x==0 && on(a,b,c)) || (y==0 && on(a,b,d)) ||
        (u==0 && on(c,d,a)) || (v==0 && on(c,d,b))) return true;
    return ((x<0 && y>0)||(x>0 && y<0)) && ((u<0 && v>0)||(u>0 && v<0));
}
bool pocketCrossing(Point start,Point end,const Pocket& pocket) {
    if (pocketContains(pocket,start,true) || pocketContains(pocket,end,true)) return true;
    const auto crosses=[&](const std::vector<Point>& ring) {
        for (std::size_t i=0;i<ring.size();++i) if (crossing(start,end,ring[i],ring[(i+1)%ring.size()])) return true;
        return false;
    };
    if (crosses(pocket.outer)) return true;
    for (const auto& hole:pocket.holes) if (crosses(hole)) return true;
    return false;
}
}
struct Guidance::Impl {
    std::vector<CoarseBump> bumps;
    std::vector<Pocket> pockets;
    std::vector<double> areas;
    std::vector<Point> exits;
    double levelDivisor=1;
    Index hierarchy,pocketIndex;
    static std::vector<Pocket> prepare(std::vector<Pocket> source) {
        for (auto& p:source) {
            validateRing(p.outer);
            for (const auto& h:p.holes) validateRing(h);
            const double normal=std::hypot(p.outward.x,p.outward.z);
            if (!finite(p.mouth[0]) || !finite(p.mouth[1]) || !finite(p.outward) || normal<=0 ||
                !std::isfinite(normal) || !std::isfinite(p.depth) || p.depth<0 || !std::isfinite(p.width) || p.width<=0 ||
                std::hypot(p.mouth[1].x-p.mouth[0].x,p.mouth[1].z-p.mouth[0].z)<=0)
                throw std::invalid_argument("Invalid certified pocket data");
            p.outward=p.outward/normal;
        }
        return source;
    }
    Impl(std::vector<CoarseBump> b,std::vector<Pocket> p)
        :bumps(std::move(b)),pockets(prepare(std::move(p))),hierarchy(bumpBounds(bumps)),pocketIndex(pocketBounds(pockets)) {
        std::array<std::uint32_t,2> levels{};std::size_t count=0;
        for (const auto& bump:bumps) {
            bool found=false;for (std::size_t i=0;i<count;++i) found|=levels[i]==bump.level;
            if (!found) {
                if (count==2) throw std::invalid_argument("At most two coarse guidance levels are supported");
                levels[count++]=bump.level;
            }
        }
        levelDivisor=count?double(count):1;
        areas.reserve(pockets.size());exits.reserve(pockets.size());
        for (const auto& pocket:pockets) {
            double area=ringArea(pocket.outer);for (const auto& hole:pocket.holes) area-=ringArea(hole);
            const auto target=(pocket.mouth[0]+pocket.mouth[1])*.5+pocket.outward*2;
            if (area<=0 || !std::isfinite(area) || !finite(target)) throw std::invalid_argument("Invalid certified pocket area or exit");
            areas.push_back(area);exits.push_back(target);
        }
    }
};
Guidance::Guidance(std::vector<CoarseBump> bumps,std::vector<Pocket> pockets)
    :impl_(std::make_unique<Impl>(std::move(bumps),std::move(pockets))) {}
Guidance::~Guidance()=default;
FieldValue Guidance::field(Point position,Point goal,GuidanceStats& stats) const {
    if (!finite(position) || !finite(goal)) throw std::invalid_argument("Invalid guidance field query");
    FieldValue result;
    impl_->hierarchy.query(position,stats,[&](std::uint32_t id) {
        const auto& b=impl_->bumps[id];
        const auto offset=position-b.center;
        const double x=offset.x/b.radii.x,z=offset.z/b.radii.z,q=x*x+z*z;
        if (q>=1 || contains(b.box,goal)) return;
        const double amplitude=b.weight/impl_->levelDivisor,t=1-q;
        result.potential+=amplitude*t*t*t;
        const double scale=-6*amplitude*t*t;
        result.gradient=result.gradient+Point{scale*x/b.radii.x,scale*z/b.radii.z};
        ++stats.bumpContributors;
    });
    return result;
}
FieldValue Guidance::pocketField(Point position,Point goal,std::uint32_t ignorePocket,GuidanceStats& stats) const {
    if (!finite(position) || !finite(goal)) throw std::invalid_argument("Invalid virtual pocket field query");
    FieldValue result;
    impl_->pocketIndex.query(position,stats,[&](std::uint32_t id) {
        if (id==ignorePocket) return;
        const auto& pocket=impl_->pockets[id];++stats.pocketChecks;
        if (pocketContains(pocket,goal,true)) return;
        if (pocketContains(pocket,position,true)) { result.potential+=1;++stats.pocketContributors;return; }
        const auto offset=position-closestBoundary(position,pocket);
        const double distance=std::hypot(offset.x,offset.z);
        if (distance>=pocketSupport) return;
        const double taper=1-distance*distance/(pocketSupport*pocketSupport);
        const double denominator=1+distance*distance/(pocketLength*pocketLength);
        const double value=taper*taper*taper/denominator;
        const double derivative=(-6*distance/(pocketSupport*pocketSupport)*taper*taper)/denominator-
            (taper*taper*taper)*(2*distance/(pocketLength*pocketLength))/(denominator*denominator);
        result.potential+=value;
        if (distance>0) result.gradient=result.gradient+offset*(derivative/distance);
        ++stats.pocketContributors;
    });
    return result;
}
bool Guidance::pocketSegmentClear(Point start,Point end,Point goal,std::uint32_t ignorePocket,GuidanceStats& stats) const {
    if (!finite(start) || !finite(end) || !finite(goal)) throw std::invalid_argument("Invalid virtual pocket probe");
    ++stats.virtualProbes;
    bool clear=true;
    impl_->pocketIndex.query(Bounds{std::min(start.x,end.x),std::min(start.z,end.z),
                                   std::max(start.x,end.x),std::max(start.z,end.z)},stats,[&](std::uint32_t id) {
        if (!clear || id==ignorePocket) return;
        const auto& pocket=impl_->pockets[id];++stats.pocketChecks;
        if (!pocketContains(pocket,goal,true) && pocketCrossing(start,end,pocket)) clear=false;
    });
    return clear;
}
GuidanceTarget Guidance::selectTarget(Point position,Point goal,const World& world,GuidanceState& state,
                                      GuidanceStats& stats,QueryStats& physicalStats) const {
    if (!finite(position) || !finite(goal)) throw std::invalid_argument("Invalid pocket guidance query");
    if (state.activePocket!=UINT32_MAX) {
        if (state.activePocket>=impl_->pockets.size()) throw std::invalid_argument("Stale pocket guidance state");
        const auto& pocket=impl_->pockets[state.activePocket];++stats.pocketChecks;
        const auto mouth=(pocket.mouth[0]+pocket.mouth[1])*.5;
        const bool outside=!pocketContains(pocket,position,true);
        if (pocketContains(pocket,goal,true) || (outside && dot(position-mouth,pocket.outward)>=1)) {
            state.activePocket=UINT32_MAX;state.target=goal;
            ++stats.escapeReleases;++stats.targetSwitches;
            return {goal,false,true,false,true};
        }
        ++stats.escapeUpdates;
        return {state.target,false,false,true,false};
    }
    std::uint32_t selected=UINT32_MAX;
    impl_->pocketIndex.query(position,stats,[&](std::uint32_t id) {
        const auto& pocket=impl_->pockets[id];++stats.pocketChecks;
        if (!pocketContains(pocket,position,false) || pocketContains(pocket,goal,true)) return;
        if (selected==UINT32_MAX || impl_->areas[id]<impl_->areas[selected] ||
            (impl_->areas[id]==impl_->areas[selected] && id<selected)) selected=id;
    });
    if (selected==UINT32_MAX) return {goal};
    const auto target=impl_->exits[selected];
    // The offline structural pocket certificate omits trees; a static trunk
    // may occupy the local target. Reject such guidance rather than steering
    // toward a physically invalid point. This is not a target search.
    if (!world.segmentClear(target,target,physicalStats)) { ++stats.blockedTargets;return {goal}; }
    state.activePocket=selected;state.target=target;
    ++stats.escapeActivations;++stats.escapeUpdates;++stats.targetSwitches;
    return {target,true,false,true,true};
}
std::size_t Guidance::bumpCount() const { return impl_->bumps.size(); }
std::size_t Guidance::pocketCount() const { return impl_->pockets.size(); }
std::size_t Guidance::memoryBytes() const {
    std::size_t bytes=sizeof(Guidance)+sizeof(Impl)+impl_->bumps.capacity()*sizeof(CoarseBump)+
        impl_->pockets.capacity()*sizeof(Pocket)+impl_->areas.capacity()*sizeof(double)+impl_->exits.capacity()*sizeof(Point)+
        impl_->hierarchy.bytes()+impl_->pocketIndex.bytes();
    for (const auto& p:impl_->pockets) {
        bytes+=p.outer.capacity()*sizeof(Point)+p.holes.capacity()*sizeof(std::vector<Point>);
        for (const auto& hole:p.holes) bytes+=hole.capacity()*sizeof(Point);
    }
    return bytes;
}
} // namespace viewer::potential_surface
