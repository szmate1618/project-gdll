#include "potential_surface_steering.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>

namespace {
using namespace viewer::potential_surface;
std::atomic<std::size_t> allocations{0};
void require(bool condition,const char* message) { if (!condition) throw std::runtime_error(message); }
double length(Point p) { return std::hypot(p.x,p.z); }
bool near(double a,double b) { return std::abs(a-b)<1e-11; }

void filteringAndNormalization() {
    Config config;config.weight=1448;config.lengthScale=2;config.support=12;
    const World world({{1,{0,0},{1,0},.35,.35}},{-20,-20,20,20},config);
    const World empty({},{-20,-20,20,20});
    const auto before=allocations.load();
    for (Point position : {Point{-2,.2},Point{-1,0},Point{1,-.3}}) {
        const Point goal{5,0}, desired=(goal-position)*(2/length(goal-position));
        QueryStats referenceStats,filteredStats,normalizedStats;
        const auto field=world.field(position,referenceStats);
        const auto expected=filterVelocity(desired,field.gradient,config.weight);
        SteeringState filteredState,normalizedState;
        const auto filtered=steerVelocity(SteeringMode::filtered,world,position,goal,desired,.05,filteredState,filteredStats);
        const auto normalized=steerVelocity(SteeringMode::normalized,world,position,goal,desired,.05,normalizedState,normalizedStats);
        require(filtered.velocity.x==expected.x && filtered.velocity.z==expected.z,
                "Default filtered mode changed the original arithmetic");
        require(referenceStats.fieldNodes==filteredStats.fieldNodes &&
                referenceStats.fieldCandidates==filteredStats.fieldCandidates &&
                referenceStats.fieldContributors==filteredStats.fieldContributors &&
                filteredStats.collisionNodes==0 && normalizedStats.collisionNodes==0,
                "Filtered/normalized mode introduced extra queries or recovery");
        require(near(length(normalized.velocity),length(desired)) &&
                std::abs(expected.x*normalized.velocity.z-expected.z*normalized.velocity.x)<1e-12 &&
                dot(normalized.velocity,desired)>0 && !normalizedState.active,
                "Normalization changed filtered direction, maximum speed, or side state");
    }
    for (auto mode : {SteeringMode::filtered,SteeringMode::normalized,SteeringMode::tangent}) {
        SteeringState state;QueryStats stats;
        const Point desired{.03,.04};
        const auto open=steerVelocity(mode,empty,{0,0},{3,4},desired,.05,state,stats);
        require(near(open.velocity.x,desired.x) && near(open.velocity.z,desired.z) && !open.activated,
                "Zero field changed direct low-speed movement");
        const auto stopped=steerVelocity(mode,empty,{0,0},{0,0},{},.05,state,stats);
        require(stopped.velocity.x==0 && stopped.velocity.z==0,
                "Zero desired speed acquired motion or a nonfinite velocity");
    }
    require(allocations.load()==before,"Local steering allocated after world construction");
}

void persistentSideAndRelease() {
    const World world({{1,{0,0},{1,0},.35,.35}},{-20,-20,20,20});
    SteeringState state;QueryStats stats;
    const auto before=allocations.load();
    auto decision=steerVelocity(SteeringMode::tangent,world,{-1.5,0},{5,0},{2,0},.05,state,stats);
    require(decision.activated && decision.tangentUpdate && state.active && state.side==1 &&
            decision.velocity.z>0 && decision.velocity.x<0 && near(length(decision.velocity),2),
            "Head-on tangent mode must choose a stable positive side and permit temporary retreat");
    require(stats.collisionNodes>0,"Local steering probes must use the counted spatial query");
    for (unsigned index=0;index<20;++index) {
        const double offset=index%2?1e-8:-1e-8;
        decision=steerVelocity(SteeringMode::tangent,world,{-1.5,offset},{5,0},{2,0},.05,state,stats);
        require(!decision.activated && !decision.released && state.side==1 && decision.velocity.z>0,
                "Tiny offsets changed the chosen side within an avoidance episode");
    }
    state.entryDistance=10;
    decision=steerVelocity(SteeringMode::tangent,world,{-1,0},{5,0},{2,0},.05,state,stats);
    require(!decision.released && state.active,"Goal improvement released through a blocked local goal segment");
    state.entryDistance=1;
    decision=steerVelocity(SteeringMode::tangent,world,{2,2},{5,0},{2,0},.05,state,stats);
    require(!decision.released && state.active,"Clear local sight released before goalward improvement");
    state.entryDistance=10;
    decision=steerVelocity(SteeringMode::tangent,world,{2,2},{5,0},{2,0},.05,state,stats);
    require(decision.released && !decision.tangentUpdate && !state.active && state.side==0,
            "Clear improved goal approach did not release tangent following");
    require(allocations.load()==before,"Persistent tangent episodes allocated");

    // Two equal opposite gradients cancel at the center; the right-hand trunk
    // still blocks the desired local ray, so gradient magnitude cannot certify safety.
    const World cancellation({{1,{-1,0},{1,0},.2,.2},{1,{1,0},{1,0},.2,.2}},{-20,-20,20,20});
    SteeringState cancelledState;QueryStats cancelledStats;
    const auto field=cancellation.field({0,0},cancelledStats);
    require(field.gradient.x==0 && field.gradient.z==0,"Cancellation fixture must have exactly zero gradient");
    decision=steerVelocity(SteeringMode::tangent,cancellation,{0,0},{5,0},{2,0},.05,cancelledState,cancelledStats);
    require(decision.activated && decision.velocity.z>0 && std::isfinite(decision.velocity.x),
            "Blocked zero-gradient approach did not activate deterministic fallback steering");

    // The long building's conservative field bends motion toward a nearby
    // trunk even though the direct goal ray is free. Releasing must check the
    // normalized velocity that will actually be returned, as well as the goal.
    Config settings;settings.weight=1448.1546878700494;settings.lengthScale=2;settings.support=12;
    const World bent({{0,{5,5},{1,0},10,.1},{1,{.6,-1},{1,0},.2,.2}},
                     {-20,-20,20,20},settings);
    SteeringState normalizedState;QueryStats bentStats;
    const auto normalized=steerVelocity(SteeringMode::normalized,bent,{0,0},{5,0},{2,0},.05,
                                       normalizedState,bentStats);
    require(bent.segmentClear({0,0},{1.5,0}) &&
            !bent.segmentClear({0,0},normalized.velocity*.75),
            "Release fixture must have a free goal ray and blocked normalized-motion ray");
    SteeringState passing;passing.active=true;passing.side=1;passing.entryDistance=10;
    passing.targetPotential=1;
    decision=steerVelocity(SteeringMode::tangent,bent,{0,0},{5,0},{2,0},.05,passing,bentStats);
    require(passing.active && !decision.released && decision.tangentUpdate,
            "Tangent episode released into a blocked normalized-motion ray");
}

void independentCircleSafetyAndBypass() {
    const World world({{1,{0,0},{1,0},.35,.35}},{-20,-20,20,20});
    SteeringState state;QueryStats stats;
    Point position{-4,0},goal{4,0};
    unsigned activations=0,releases=0,retreats=0;
    bool arrived=false;
    const auto before=allocations.load();
    for (unsigned step=0;step<2400;++step) {
        const auto offset=goal-position;
        const auto desired=offset*(std::min(2.,length(offset)/.05)/length(offset));
        const auto decision=steerVelocity(SteeringMode::tangent,world,position,goal,desired,.05,state,stats);
        const auto moved=world.move(position,position+decision.velocity*.05,stats);
        const auto delta=moved.position-position;
        // Independent swept circle distance, including every accepted frame.
        const long double dx=delta.x,dz=delta.z,squared=dx*dx+dz*dz;
        const long double t=squared>0?std::clamp(-(position.x*dx+position.z*dz)/squared,0.L,1.L):0;
        const long double clearance=std::hypotl(position.x+t*dx,position.z+t*dz)-(.35L+.3L+.002L);
        require(clearance>0 && !moved.invalidStart,"Tangent bypass crossed the independent physical circle");
        require(length(delta)<=.100000000001,"Accepted tangent movement exceeded the common speed cap");
        activations+=decision.activated;releases+=decision.released;retreats+=dot(delta,offset)<0;
        position=moved.position;
        if (length(goal-position)<=.15) { arrived=true;break; }
    }
    require(arrived && activations>0 && releases>0 && retreats>0,
            "Tangent mode did not bypass a centered convex obstacle with temporary retreat");
    require(allocations.load()==before,"Complete local tangent bypass allocated after construction");
}
}

void* operator new(std::size_t size) {
    ++allocations;
    if (void* memory=std::malloc(size?size:1)) return memory;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory,std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory,std::size_t) noexcept { std::free(memory); }

int main() try {
    filteringAndNormalization();persistentSideAndRelease();independentCircleSafetyAndBypass();
    std::cout<<"Potential steering speed, persistence, cancellation, retreat, safety and no-allocation tests passed\n";
    return 0;
} catch (const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
