#include "potential_surface_guidance.hpp"
#include "potential_surface_steering.hpp"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <random>
#include <stdexcept>

namespace {
using namespace viewer::potential_surface;
std::atomic<std::size_t> allocations{0};
void require(bool condition,const char* message) { if (!condition) throw std::runtime_error(message); }
bool near(double a,double b,double tolerance=1e-8) { return std::abs(a-b)<=tolerance*(1+std::max(std::abs(a),std::abs(b))); }
double length(Point p) { return std::hypot(p.x,p.z); }
Pocket rectanglePocket() {
    return {{{-5,-2},{0,-2},{0,2},{-5,2}}, {}, {{{-5,-2},{-5,2}}}, {-1,0},5,4};
}
void analyticHierarchy() {
    const CoarseBump bump{{2,-1},{4,2},.75,{1,-1.5,3,-.5},0};
    const Guidance one({bump},{});
    CoarseBump upper=bump;upper.weight=1.5;upper.level=1;
    const Guidance two({bump,upper},{});
    GuidanceStats stats;
    constexpr double step=1e-5;
    for (auto p:{Point{1,-.5},Point{4,-.8},Point{2,-1},Point{-.5,-1.1}}) {
        const auto actual=two.field(p,{20,20},stats);
        const double dx=(two.field({p.x+step,p.z},{20,20},stats).potential-
                         two.field({p.x-step,p.z},{20,20},stats).potential)/(2*step);
        const double dz=(two.field({p.x,p.z+step},{20,20},stats).potential-
                         two.field({p.x,p.z-step},{20,20},stats).potential)/(2*step);
        require(near(actual.gradient.x,dx,2e-7) && near(actual.gradient.z,dz,2e-7),
                "Analytical coarse-bump derivative differs from independent central differences");
    }
    require(one.field({2,-1},{20,20},stats).potential==.75 &&
            two.field({2,-1},{20,20},stats).potential==1.125,
            "Coarse level budget must be divided exactly once by the present level count");
    const auto boundary=one.field({6,-1},{20,20},stats),nearBoundary=one.field({6-1e-5,-1},{20,20},stats);
    require(boundary.potential==0 && length(boundary.gradient)==0 && length(nearBoundary.gradient)<1e-9,
            "Coarse compact support is not smoothly flat");
    for (auto goal:{Point{2,-1},Point{1,-1.5}}) {
        const auto suppressed=one.field({4,-1},goal,stats);
        require(suppressed.potential==0 && length(suppressed.gradient)==0,
                "Goal-inside original AABB failed to suppress a virtual coarse obstacle");
    }
    bool extraLevel=false;
    try { auto third=upper;third.level=2;Guidance bad({bump,upper,third},{}); }
    catch (const std::invalid_argument&) { extraLevel=true; }
    require(extraLevel,"Unbounded ancestor levels were accepted");
}
void virtualPocketFieldAndProbes() {
    const Guidance guidance({}, {rectanglePocket()});
    GuidanceStats stats;
    constexpr double step=1e-5;
    for (auto p:{Point{-6,0},Point{-6,3},Point{2,0},Point{1,3}}) {
        const auto value=guidance.pocketField(p,{10,10},UINT32_MAX,stats);
        const double dx=(guidance.pocketField({p.x+step,p.z},{10,10},UINT32_MAX,stats).potential-
                         guidance.pocketField({p.x-step,p.z},{10,10},UINT32_MAX,stats).potential)/(2*step);
        const double dz=(guidance.pocketField({p.x,p.z+step},{10,10},UINT32_MAX,stats).potential-
                         guidance.pocketField({p.x,p.z-step},{10,10},UINT32_MAX,stats).potential)/(2*step);
        require(near(value.gradient.x,dx,2e-7) && near(value.gradient.z,dz,2e-7),
                "Virtual pocket analytic closest-boundary gradient is wrong");
    }
    const auto inside=guidance.pocketField({-1,0},{10,10},UINT32_MAX,stats);
    const auto boundary=guidance.pocketField({-5,0},{10,10},UINT32_MAX,stats);
    const auto outside=guidance.pocketField({-5-1e-6,0},{10,10},UINT32_MAX,stats);
    const auto far=guidance.pocketField({-17,0},{10,10},UINT32_MAX,stats);
    require(inside.potential==1 && length(inside.gradient)==0 && boundary.potential==1 &&
            length(boundary.gradient)==0 && length(outside.gradient)<1e-6 && far.potential==0 && length(far.gradient)==0,
            "Filled pocket interior/boundary/support is not C1 with the outside profile");
    require(!guidance.pocketSegmentClear({-7,0},{-1,0},{5,0},UINT32_MAX,stats) &&
            guidance.pocketSegmentClear({-7,3},{3,3},{5,0},UINT32_MAX,stats),
            "Virtual segment probes failed to reject re-entry or blocked a clear outside route");
    require(guidance.pocketSegmentClear({-7,0},{-1,0},{-2,0},UINT32_MAX,stats) &&
            guidance.pocketField({-7,0},{-2,0},UINT32_MAX,stats).potential==0,
            "Goal-inside gate failed to suppress virtual pocket boundary and cost");
    require(guidance.pocketSegmentClear({-1,0},{-7,0},{5,0},0,stats) &&
            guidance.pocketField({-1,0},{5,0},0,stats).potential==0,
            "Active escaping pocket obstructed its own local exit target");
    const World empty({},{-30,-30,30,30});
    require(empty.segmentClear({-7,0},{-1,0}),"Virtual pocket geometry leaked into physical collision");
}
void pocketGeometryAndPersistence() {
    auto pocket=rectanglePocket();
    const Guidance guidance({}, {pocket});
    const World empty({},{-30,-30,30,30});
    GuidanceStats stats;QueryStats physical;GuidanceState state;
    auto target=guidance.selectTarget({-1,0},{5,0},empty,state,stats,physical);
    require(target.activated && target.active && target.changed && target.position.x==-7 && target.position.z==0 &&
            stats.escapeActivations==1 && stats.targetSwitches==1,
            "Verified pocket did not select entrance midpoint plus 2 m outward");
    target=guidance.selectTarget({-5.5,0},{5,0},empty,state,stats,physical);
    require(target.active && !target.changed && target.position.x==-7,
            "Pocket target released before its one-meter exit hysteresis");
    target=guidance.selectTarget({-6.1,0},{5,0},empty,state,stats,physical);
    require(target.released && !target.active && target.changed && state.activePocket==UINT32_MAX &&
            target.position.x==5 && stats.escapeReleases==1,
            "Pocket guidance failed to release after crossing its mouth outward");
    for (auto goal:{Point{-2,0},Point{-5,0}}) {
        GuidanceState insideGoal;
        target=guidance.selectTarget({-1,0},goal,empty,insideGoal,stats,physical);
        require(!target.active && !target.activated,"Legitimate goal inside/on pocket boundary triggered escape");
    }
    auto withHole=pocket;withHole.holes={{{-3,-.5},{-2,-.5},{-2,.5},{-3,.5}}};
    Guidance holes({}, {withHole});GuidanceState inHole;
    require(!holes.selectTarget({-2.5,0},{5,0},empty,inHole,stats,physical).active,
            "Pocket hole was incorrectly treated as its navigable interior");
    const World blockedExit({{1,{-7,0},{1,0},.5,.5}},{-30,-30,30,30});
    GuidanceState blocked;
    require(!guidance.selectTarget({-1,0},{5,0},blockedExit,blocked,stats,physical).active && stats.blockedTargets==1,
            "Tree-occupied local escape target was accepted");
}
void integrationAndNoAllocation() {
    auto pocket=rectanglePocket();
    Guidance guidance({{{-1,0},{8,5},.75,{-4,-1,0,1},0}},{pocket});
    const World world({{1,{1,3},{1,0},.3,.3}},{-30,-30,30,30});
    const auto before=allocations.load();
    for (auto mode:{SteeringMode::filtered,SteeringMode::normalized,SteeringMode::tangent}) {
        SteeringState a,b;GuidanceState ignored;GuidanceStats untouched;QueryStats qa,qb;
        const Point pos{-8,-3},goal{8,0},desired=(goal-pos)*(2/length(goal-pos));
        const auto baseline=steerVelocity(mode,world,pos,goal,desired,.05,a,qa);
        const auto none=steerVelocity(mode,world,pos,goal,desired,.05,b,qb,&guidance,GuidanceMode::none,&ignored,&untouched);
        require(baseline.velocity.x==none.velocity.x && baseline.velocity.z==none.velocity.z &&
                baseline.activated==none.activated && baseline.tangentUpdate==none.tangentUpdate &&
                qa.fieldNodes==qb.fieldNodes && qa.fieldCandidates==qb.fieldCandidates &&
                qa.fieldContributors==qb.fieldContributors && qa.collisionNodes==qb.collisionNodes &&
                qa.collisionCandidates==qb.collisionCandidates && untouched.queries==0 && untouched.targetSwitches==0,
                "Guidance mode none altered baseline motion or query counts");
    }
    SteeringState old;old.initialized=true;old.active=true;old.side=-1;old.slowSeconds=9;
    QueryStats physical;GuidanceStats stats;GuidanceState state;
    const auto decision=steerVelocity(SteeringMode::normalized,world,{-1,0},{5,0},{2,0},.05,old,physical,
                                      &guidance,GuidanceMode::pockets,&state,&stats);
    require(decision.guidanceActivated && decision.guidanceUpdate && decision.velocity.x<0 &&
            !old.initialized && !old.active && old.side==0 && old.slowSeconds==0 && length(decision.velocity)<=2,
            "Escape target switch did not reset tangent state or preserve the speed cap");
    for (unsigned i=0;i<100;++i) {
        GuidanceStats work;QueryStats query;GuidanceState agent;SteeringState steer;
        const auto value=steerVelocity(SteeringMode::tangent,world,{-1,.01},{5,0},{2,0},.05,steer,query,
                                       &guidance,GuidanceMode::combined,&agent,&work);
        require(std::isfinite(value.velocity.x) && std::isfinite(value.velocity.z) && length(value.velocity)<=2.00000000001,
                "Combined guidance produced invalid or excessive velocity");
        const auto moved=world.move({-1,.01},Point{-1,.01}+value.velocity*.05,query);
        require(world.segmentClear({-1,.01},moved.position),"Guidance bypassed the strict physical guard");
    }
    require(allocations.load()==before,"Runtime guidance or steering allocated after construction");
    require(guidance.bumpCount()==1 && guidance.pocketCount()==1 && guidance.memoryBytes()>sizeof(Pocket),
            "Guidance memory or geometry counts omit persistent data");
}
void indexedHierarchy() {
    std::vector<CoarseBump> bumps;
    for (int x=-8;x<=8;++x) for (int z=-8;z<=8;++z)
        bumps.push_back({{x*8.,z*8.},{6,4},.75,{x*8.-1,z*8.-1,x*8.+1,z*8.+1},0});
    const Guidance guidance(bumps,{});std::mt19937 random(1234);
    const auto before=allocations.load();
    for (unsigned i=0;i<400;++i) {
        const Point p{-70+140*(double(random())/4294967296.),-70+140*(double(random())/4294967296.)};
        GuidanceStats stats;const auto indexed=guidance.field(p,{1000,1000},stats);
        FieldValue expected;
        for (const auto& b:bumps) {
            const double x=(p.x-b.center.x)/b.radii.x,z=(p.z-b.center.z)/b.radii.z,q=x*x+z*z;
            if (q>=1) continue;
            const double t=1-q,s=-6*b.weight*t*t;
            expected.potential+=b.weight*t*t*t;
            expected.gradient=expected.gradient+Point{s*x/b.radii.x,s*z/b.radii.z};
        }
        require(near(indexed.potential,expected.potential,1e-12) &&
                near(indexed.gradient.x,expected.gradient.x,1e-12) && near(indexed.gradient.z,expected.gradient.z,1e-12),
                "Sparse hierarchy query omitted an analytical bump");
        require(stats.candidates<bumps.size()/4,"Hierarchy query scans almost every proxy");
    }
    require(allocations.load()==before,"Sparse guidance queries allocated");
}
void strictPhysicalPocketExit() {
    for (bool tuned:{false,true}) {
        Config config;config.exactBuildings=true;
        if (tuned) { config.weight=1448;config.lengthScale=2;config.support=12; }
        const World world({{2,{-2.5,2.502},{1,0},2.5,.2},{2,{-2.5,-2.502},{1,0},2.5,.2},
                           {2,{.502,0},{1,0},.2,2.5}},{-30,-30,30,30},config);
        Pocket pocket={{{-5.302,-2},{0,-2},{0,2},{-5.302,2}}, {}, {{{-5.302,-2},{-5.302,2}}}, {-1,0},5.302,4};
        const Guidance guidance({}, {pocket});GuidanceState agent;GuidanceStats stats;QueryStats queries;SteeringState steering;
        Point p{-1,0},goal{5,0};bool arrived=false;unsigned retreats=0;double releaseX=0;
        for (unsigned step=0;step<2400;++step) {
            const auto delta=goal-p;const double distance=length(delta);
            if (distance<=.15) { arrived=true;break; }
            const auto desired=delta*(std::min(2.,distance/.05)/distance);
            const auto d=steerVelocity(SteeringMode::tangent,world,p,goal,desired,.05,steering,queries,
                                      &guidance,GuidanceMode::pockets,&agent,&stats);
            if (d.guidanceReleased) releaseX=p.x;
            const auto moved=world.move(p,p+d.velocity*.05,queries);
            require(!moved.invalidStart && world.segmentClear(p,moved.position),"Pocket exit crossed a certified physical wall");
            require(length(moved.position-p)<=.100000000001,"Guided physical step exceeded the common speed cap");
            retreats+=dot(moved.position-p,goal-p)<0;p=moved.position;
        }
        require(arrived && retreats>0 && releaseX<=-6.302 && stats.escapeActivations==1 && stats.escapeReleases==1 &&
                stats.virtualProbes>0 && stats.pocketContributors>0,
                "Guided U escape failed to arrive or repeatedly re-entered its certified pocket");
    }
}
}
[[gnu::noinline]] void* operator new(std::size_t size) {
    ++allocations;
    if (void* p=std::malloc(size?size:1)) return p;
    throw std::bad_alloc();
}
[[gnu::noinline]] void* operator new[](std::size_t size) { return ::operator new(size); }
[[gnu::noinline]] void operator delete(void* p) noexcept { std::free(p); }
[[gnu::noinline]] void operator delete[](void* p) noexcept { std::free(p); }
[[gnu::noinline]] void operator delete(void* p,std::size_t) noexcept { std::free(p); }
[[gnu::noinline]] void operator delete[](void* p,std::size_t) noexcept { std::free(p); }
int main() try {
    analyticHierarchy();virtualPocketFieldAndProbes();pocketGeometryAndPersistence();integrationAndNoAllocation();indexedHierarchy();strictPhysicalPocketExit();
    std::cout<<"Analytical hierarchy, goal gates, pocket hysteresis, baseline identity, strict safety and no-allocation checks passed\n";
    return 0;
} catch (const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
