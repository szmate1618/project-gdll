#include "potential_surface.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace viewer::potential_surface {
namespace {
bool finite(Point p) { return std::isfinite(p.x) && std::isfinite(p.z); }
bool finite(Bounds b) {
    return std::isfinite(b.minX) && std::isfinite(b.minZ) &&
           std::isfinite(b.maxX) && std::isfinite(b.maxZ);
}
Point perpendicular(Point p) { return {-p.z,p.x}; }
Bounds unite(Bounds a,Bounds b) {
    return {std::min(a.minX,b.minX),std::min(a.minZ,b.minZ),
            std::max(a.maxX,b.maxX),std::max(a.maxZ,b.maxZ)};
}
Bounds segmentBounds(Point a,Point b) {
    return {std::min(a.x,b.x),std::min(a.z,b.z),std::max(a.x,b.x),std::max(a.z,b.z)};
}
bool overlaps(Bounds a,Bounds b) {
    return a.minX<=b.maxX && b.minX<=a.maxX && a.minZ<=b.maxZ && b.minZ<=a.maxZ;
}
bool contains(Bounds b,Point p) {
    return p.x>b.minX && p.x<b.maxX && p.z>b.minZ && p.z<b.maxZ;
}
void validateConfig(const Config& c) {
    if (!std::isfinite(c.radius) || c.radius<=0 || !std::isfinite(c.skin) || c.skin<0 ||
        !std::isfinite(c.buildingExtra) || c.buildingExtra<0 || !std::isfinite(c.support) || c.support<=0 ||
        !std::isfinite(c.lengthScale) || c.lengthScale<=0 || !std::isfinite(c.weight) || c.weight<0)
        throw std::invalid_argument("Invalid potential surface configuration");
}
Obstacle normalized(Obstacle obstacle) {
    const double length=std::hypot(obstacle.axis.x,obstacle.axis.z);
    if (obstacle.kind>2 || !finite(obstacle.center) || !finite(obstacle.axis) ||
        !std::isfinite(obstacle.halfX) || !std::isfinite(obstacle.halfZ) ||
        obstacle.halfX<0 || obstacle.halfZ<0 || !std::isfinite(length) || length<=0)
        throw std::invalid_argument("Invalid potential surface obstacle");
    obstacle.axis=obstacle.axis/length;
    return obstacle;
}
double padding(const Obstacle& o,const Config& c) {
    return c.radius+c.skin+(o.kind==0?c.buildingExtra:0);
}
struct Primitive { Point a,b;double radius; };
Primitive primitive(const Obstacle& o,const Config& c) {
    if (o.kind==0) return {o.center,o.center,std::hypot(o.halfX,o.halfZ)+padding(o,c)};
    if (o.kind==1) return {o.center,o.center,std::max(o.halfX,o.halfZ)+padding(o,c)};
    const bool xLong=o.halfX>=o.halfZ;
    const auto axis=xLong?o.axis:perpendicular(o.axis);
    const double halfLength=xLong?o.halfX:o.halfZ;
    return {o.center-axis*halfLength,o.center+axis*halfLength,
            (xLong?o.halfZ:o.halfX)+padding(o,c)};
}
Bounds outward(Bounds b) {
    const double infinity=std::numeric_limits<double>::infinity();
    return {std::nextafter(b.minX,-infinity),std::nextafter(b.minZ,-infinity),
            std::nextafter(b.maxX,infinity),std::nextafter(b.maxZ,infinity)};
}
Bounds fieldBounds(const Primitive& p,const Config& c) {
    const double r=p.radius+c.support;
    return outward({std::min(p.a.x,p.b.x)-r,std::min(p.a.z,p.b.z)-r,
                    std::max(p.a.x,p.b.x)+r,std::max(p.a.z,p.b.z)+r});
}
Bounds physicalBounds(const Obstacle& o,const Config& c) {
    if (o.kind==1) {
        const double r=std::max(o.halfX,o.halfZ)+padding(o,c);
        return outward({o.center.x-r,o.center.z-r,o.center.x+r,o.center.z+r});
    }
    const double x=o.halfX+padding(o,c),z=o.halfZ+padding(o,c);
    const double extentX=std::abs(o.axis.x)*x+std::abs(o.axis.z)*z;
    const double extentZ=std::abs(o.axis.z)*x+std::abs(o.axis.x)*z;
    return outward({o.center.x-extentX,o.center.z-extentZ,o.center.x+extentX,o.center.z+extentZ});
}
FieldValue evaluate(Point position,const Primitive& p,const Config& c) {
    const auto axis=p.b-p.a;
    const double length2=dot(axis,axis);
    const double t=length2>0?std::clamp(dot(position-p.a,axis)/length2,0.0,1.0):0;
    const auto offset=position-(p.a+axis*t);
    const double distance=std::hypot(offset.x,offset.z),clearance=distance-p.radius;
    if (clearance>=c.support) return {};
    double value,derivative;
    if (clearance<0) {
        value=1-clearance/c.lengthScale;
        derivative=-1/c.lengthScale;
    } else {
        const double q=1+clearance/c.lengthScale;
        const double denominator=1+q*q;
        const double taper=1-clearance*clearance/(c.support*c.support);
        const double taper2=taper*taper,taper3=taper2*taper;
        const double taperDerivative=-6*clearance/(c.support*c.support)*taper2;
        value=2*taper3/denominator;
        derivative=2*(taperDerivative/denominator-taper3*(2*q/c.lengthScale)/(denominator*denominator));
    }
    // At the center/centerline the radial direction is undefined. Symmetry
    // supplies zero; valid physical starts cannot lie on these centerlines.
    return {value,distance>0?offset*(derivative/distance):Point{}};
}

// The indexes stream every leaf candidate directly to the caller. Balanced
// median splits bound depth to 32 for the accepted uint32-sized scene; the
// fixed traversal stack therefore cannot truncate a query or allocate.
struct Index {
    struct Node { Bounds bounds;std::uint32_t begin=0,count=0,left=0,right=0; };
    std::vector<Node> nodes;
    std::vector<std::uint32_t> order;
    template<class Box> Index(const std::vector<Obstacle>& obstacles,Box box) {
        order.resize(obstacles.size());
        std::iota(order.begin(),order.end(),0u);
        std::size_t leaves=1;
        while (leaves*8<obstacles.size()) leaves*=2;
        if (!obstacles.empty()) {
            nodes.reserve(leaves*2-1);
            build(0,std::uint32_t(order.size()),obstacles,box);
        }
    }
    template<class Box> std::uint32_t build(std::uint32_t begin,std::uint32_t end,
                                          const std::vector<Obstacle>& obstacles,Box box) {
        Bounds bounds=box(order[begin]);
        for (auto i=begin+1;i<end;++i) bounds=unite(bounds,box(order[i]));
        const auto index=std::uint32_t(nodes.size());
        nodes.push_back({bounds,begin,end-begin,0,0});
        if (end-begin<=8) return index;
        const bool xAxis=bounds.maxX-bounds.minX>=bounds.maxZ-bounds.minZ;
        const auto middle=begin+(end-begin)/2;
        std::nth_element(order.begin()+begin,order.begin()+middle,order.begin()+end,
            [&](std::uint32_t a,std::uint32_t b) {
                const double ca=xAxis?obstacles[a].center.x:obstacles[a].center.z;
                const double cb=xAxis?obstacles[b].center.x:obstacles[b].center.z;
                return ca<cb || (ca==cb && a<b);
            });
        const auto left=build(begin,middle,obstacles,box);
        const auto right=build(middle,end,obstacles,box);
        nodes[index].count=0;nodes[index].left=left;nodes[index].right=right;
        return index;
    }
    template<class Callback> bool query(Bounds region,std::uint64_t& visited,
                                       std::uint64_t& candidates,Callback callback) const {
        if (nodes.empty()) return true;
        std::array<std::uint32_t,64> stack{};
        std::size_t size=1;
        while (size) {
            const auto& node=nodes[stack[--size]];
            ++visited;
            if (!overlaps(node.bounds,region)) continue;
            if (node.count) {
                for (auto i=node.begin;i<node.begin+node.count;++i) {
                    ++candidates;
                    if (!callback(order[i])) return false;
                }
            } else {
                stack[size++]=node.right;
                stack[size++]=node.left;
            }
        }
        return true;
    }
    std::size_t bytes() const { return nodes.capacity()*sizeof(Node)+order.capacity()*sizeof(std::uint32_t); }
};

bool boxEntry(Point start,Point end,const Obstacle& o,const Config& c,double& entry) {
    const auto offset=start-o.center,delta=end-start;
    const Point axes[]={o.axis,perpendicular(o.axis)};
    const double extents[]={o.halfX+padding(o,c),o.halfZ+padding(o,c)};
    double first=0,last=1;
    for (unsigned i=0;i<2;++i) {
        const double p=dot(offset,axes[i]),v=dot(delta,axes[i]);
        if (v==0) {
            if (p<-extents[i] || p>extents[i]) return false;
            continue;
        }
        double a=(-extents[i]-p)/v,b=(extents[i]-p)/v;
        if (a>b) std::swap(a,b);
        first=std::max(first,a);last=std::min(last,b);
        if (first>last) return false;
    }
    entry=first;
    return true;
}
bool circleEntry(Point start,Point end,const Obstacle& o,const Config& c,double& entry) {
    const auto offset=start-o.center,delta=end-start;
    const double radius=std::max(o.halfX,o.halfZ)+padding(o,c);
    const double a=dot(delta,delta),b=dot(offset,delta),constant=dot(offset,offset)-radius*radius;
    if (constant<=0) { entry=0;return true; }
    if (a==0) return false;
    const double closest=std::clamp(-b/a,0.0,1.0);
    const auto nearest=offset+delta*closest;
    if (dot(nearest,nearest)>radius*radius) return false;
    const double root=std::sqrt(std::max(0.0,b*b-a*constant));
    const double denominator=-b+root;
    entry=denominator>0?std::clamp(constant/denominator,0.0,1.0):0;
    return true;
}
bool collisionEntry(Point start,Point end,const Obstacle& o,const Config& c,double& entry) {
    return o.kind==1?circleEntry(start,end,o,c,entry):boxEntry(start,end,o,c,entry);
}
}

FieldValue obstacleField(Point position,const Obstacle& obstacle,const Config& config) {
    validateConfig(config);
    if (!finite(position)) throw std::invalid_argument("Nonfinite potential query position");
    return evaluate(position,primitive(normalized(obstacle),config),config);
}
Point filterVelocity(Point desired,Point gradient,double weight) {
    if (!finite(desired) || !finite(gradient) || !std::isfinite(weight) || weight<0)
        throw std::invalid_argument("Invalid potential velocity filter input");
    return desired-gradient*(weight*std::max(0.0,dot(gradient,desired))/(1+weight*dot(gradient,gradient)));
}
struct World::Impl {
    std::vector<Obstacle> obstacles;
    Bounds original,allowed;
    Config config;
    std::vector<Primitive> primitives;
    Index fields,physical;
    static std::vector<Obstacle> prepare(std::vector<Obstacle> source,Bounds bounds,const Config& config) {
        validateConfig(config);
        const double inset=config.radius+config.skin;
        if (!finite(bounds) || bounds.minX+inset>=bounds.maxX-inset || bounds.minZ+inset>=bounds.maxZ-inset ||
            source.size()>std::numeric_limits<std::uint32_t>::max()/2)
            throw std::invalid_argument("Invalid potential world bounds or obstacle count");
        for (auto& o:source) {
            o=normalized(o);
            if (!finite(fieldBounds(primitive(o,config),config)) || !finite(physicalBounds(o,config)))
                throw std::invalid_argument("Potential obstacle extent overflow");
        }
        return source;
    }
    static std::vector<Primitive> preparePrimitives(const std::vector<Obstacle>& source,const Config& config) {
        std::vector<Primitive> result;result.reserve(source.size());
        for (const auto& o:source) result.push_back(primitive(o,config));
        return result;
    }
    Impl(std::vector<Obstacle> source,Bounds bounds,Config settings)
        :obstacles(prepare(std::move(source),bounds,settings)),original(bounds),
         allowed{bounds.minX+settings.radius+settings.skin,bounds.minZ+settings.radius+settings.skin,
                 bounds.maxX-settings.radius-settings.skin,bounds.maxZ-settings.radius-settings.skin},
         config(settings),primitives(preparePrimitives(obstacles,config)),
         fields(obstacles,[&](std::uint32_t id){return fieldBounds(primitives[id],config);}),
         physical(obstacles,[&](std::uint32_t id){return physicalBounds(obstacles[id],config);}) {}
    bool segmentClear(Point start,Point end,QueryStats& stats) const {
        if (!finite(start) || !finite(end) || !contains(allowed,start) || !contains(allowed,end)) return false;
        return physical.query(segmentBounds(start,end),stats.collisionNodes,stats.collisionCandidates,
            [&](std::uint32_t id) { double entry;return !collisionEntry(start,end,obstacles[id],config,entry); });
    }
};
World::World(std::vector<Obstacle> obstacles,Bounds bounds,Config config)
    :impl_(std::make_unique<Impl>(std::move(obstacles),bounds,config)) {}
World::~World()=default;
FieldValue World::field(Point position,QueryStats& stats) const {
    if (!finite(position)) throw std::invalid_argument("Nonfinite potential query position");
    FieldValue result;
    impl_->fields.query({position.x,position.z,position.x,position.z},stats.fieldNodes,stats.fieldCandidates,
        [&](std::uint32_t id) {
            const auto value=evaluate(position,impl_->primitives[id],impl_->config);
            result.potential+=value.potential;result.gradient=result.gradient+value.gradient;
            if (value.potential>0) ++stats.fieldContributors;
            return true;
        });
    return result;
}
bool World::clear(Point position) const { QueryStats stats;return impl_->segmentClear(position,position,stats); }
bool World::segmentClear(Point start,Point end) const { QueryStats stats;return impl_->segmentClear(start,end,stats); }
MoveResult World::move(Point start,Point proposed,QueryStats& stats) const {
    if (!impl_->segmentClear(start,start,stats)) return {start,true,true};
    if (!finite(proposed)) return {start,true,true};
    const auto delta=proposed-start;
    double first=1;
    bool constrained=false;
    const auto allowed=impl_->allowed;
    if (!contains(allowed,proposed)) {
        if (delta.x>0) first=std::min(first,(allowed.maxX-start.x)/delta.x);
        if (delta.x<0) first=std::min(first,(allowed.minX-start.x)/delta.x);
        if (delta.z>0) first=std::min(first,(allowed.maxZ-start.z)/delta.z);
        if (delta.z<0) first=std::min(first,(allowed.minZ-start.z)/delta.z);
        constrained=true;
    }
    impl_->physical.query(segmentBounds(start,proposed),stats.collisionNodes,stats.collisionCandidates,
        [&](std::uint32_t id) {
            double entry;
            if (collisionEntry(start,proposed,impl_->obstacles[id],impl_->config,entry)) {
                first=std::min(first,entry);constrained=true;
            }
            return true;
        });
    if (!constrained) return {proposed,false,false};
    const double length=std::hypot(delta.x,delta.z);
    if (length==0) return {start,true,false};
    // Positive clearance backoff, followed by a complete strict recheck. No
    // physical extent or collision test is reduced by a tolerance.
    double t=std::max(0.0,first-1e-6/length);
    for (unsigned attempt=0;attempt<32;++attempt) {
        const auto result=start+delta*t;
        if (impl_->segmentClear(start,result,stats)) return {result,true,false};
        t*=.5;
    }
    return {start,true,false};
}
const std::vector<Obstacle>& World::obstacles() const { return impl_->obstacles; }
Bounds World::bounds() const { return impl_->original; }
const Config& World::config() const { return impl_->config; }
std::size_t World::memoryBytes() const {
    return sizeof(World)+sizeof(Impl)+impl_->obstacles.capacity()*sizeof(Obstacle)+
           impl_->primitives.capacity()*sizeof(Primitive)+impl_->fields.bytes()+impl_->physical.bytes();
}
} // namespace viewer::potential_surface
