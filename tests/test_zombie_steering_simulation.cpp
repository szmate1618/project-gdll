#include "../tools/zombie_steering_simulation.hpp"
#include "../tools/zombie_steering_safety.hpp"
#include "zombie_crowd.hpp"
#include <cmath>
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
using namespace viewer::steering_benchmark;
using viewer::steering::Mode;
void require(bool condition,const char* message) { if (!condition) throw std::runtime_error(message); }
const Scenario& named(const std::vector<Scenario>& scenes,const char* name) {
    for (const auto& scene:scenes) if (scene.name==name) return scene;
    throw std::runtime_error("Missing fixture");
}
void safe(const SimulationResult& result) {
    require(result.queryOverflows==0,"Nearby trunk query overflowed");
    for (const auto& agent:result.agents) require(agent.invalidSweepStarts==0,"Safety guard received an invalid start");
    require(result.frames*result.agents.size()==result.positions.size(),"Full accepted trajectory size is wrong");
    require(result.validation.endpointPenetrations==0,"Actual position penetrated independent physical trunk circle");
    require(result.validation.sweptPenetrations==0,"Accepted movement tunneled through independent physical trunk circle");
    require(result.validation.meshViolations==0,"Accepted movement left tree-free navmesh");
}
void sharedSweepGuard() {
    const viewer::steering::Obstacle obstacle{1,{0,0},.2f};
    constexpr float body=viewer::ZombieCrowd::bodyRadius;
    // Both endpoints clear the radius, but this chord cuts through the circle.
    const glm::vec2 start{-.08f,.496f},proposed{.08f,.496f};
    const auto guarded=constrainTrunkSweep(start,proposed,&obstacle,1,body);
    require(guarded.constrained && !guarded.invalidStart,"Guard missed a diagonal contact chord");
    require(guarded.position.x<proposed.x && guarded.position.x>=start.x,"Guard did not clip forward movement");
    const long double dx=static_cast<long double>(guarded.position.x)-start.x;
    const long double dz=static_cast<long double>(guarded.position.y)-start.y;
    const long double t=std::clamp(-(static_cast<long double>(start.x)*dx+static_cast<long double>(start.y)*dz)/(dx*dx+dz*dz),0.L,1.L);
    const long double x=start.x+t*dx,z=start.y+t*dz;
    require(std::sqrt(x*x+z*z)>=static_cast<long double>(body)+obstacle.radius,
        "Clipped diagonal chord still penetrates under independent long-double calculation");
    const auto away=constrainTrunkSweep({-1,0},{-2,0},&obstacle,1,body);
    require(!away.constrained && away.position==glm::vec2(-2,0),"Clear movement was changed");
    const auto stationary=constrainTrunkSweep({-1,0},{-1,0},&obstacle,1,body);
    require(!stationary.constrained,"Clear stationary start was rejected");
    const auto invalid=constrainTrunkSweep({0,0},{-1,0},&obstacle,1,body);
    require(invalid.invalidStart && invalid.constrained && invalid.position==glm::vec2(0),
        "Invalid penetrated start silently passed sweep validation");
    const viewer::steering::Obstacle exact{1,{0,0},.5f};
    const auto tangent=constrainTrunkSweep({-1,1},{1,1},&exact,1,.5f);
    require(!tangent.constrained && tangent.position==glm::vec2(1,1),"Exact nonpenetrating tangent was rejected");
    const auto inward=constrainTrunkSweep({-1,0},{-.9f,0},&exact,1,.5f);
    const auto outward=constrainTrunkSweep({-1,0},{-1.1f,0},&exact,1,.5f);
    require(inward.constrained && !inward.invalidStart && inward.position==glm::vec2(-1,0),
        "Boundary start moving inward was not stopped");
    require(!outward.constrained,"Boundary start moving outward was stopped");
    const viewer::steering::Obstacle mapScale{1,{3000,3000},.2f};
    const auto large=constrainTrunkSweep({2999.92f,3000.496f},{3000.08f,3000.496f},&mapScale,1,body);
    require(large.constrained && !large.invalidStart,"Map-scale chord was not constrained");
    require(large.position.x<3000 && std::hypot(double(large.position.x)-3000,double(large.position.y)-3000)>=double(body)+mapScale.radius,
        "Float-rounded map-scale result reentered a trunk");
    const viewer::steering::Obstacle two[]={{1,{0,0},.2f},{2,{-1,0},.2f}};
    const auto earliest=constrainTrunkSweep({-2,0},{1,0},two,2,body);
    require(earliest.constrained && !earliest.invalidStart && earliest.position.x<-1.5f,
        "Multiple circles did not choose the earliest hit");
}
void fixturesAndRepeatability() {
    const auto first=makeScenarios(24,1234),again=makeScenarios(24,1234),other=makeScenarios(24,1235);
    require(first.size()==10 && again.size()==first.size(),"Wrong controlled fixture coverage");
    const auto& woodland=named(first,"woodland");
    const auto& repeated=named(again,"woodland");
    const auto& changed=named(other,"woodland");
    require(woodland.requests.size()==24 && woodland.trunks.size()==500,"Woodland must have real independent agent requests and a shared forest");
    bool differs=false;
    for (std::size_t id=0;id<woodland.requests.size();++id) {
        const auto& request=woodland.requests[id];
        require(request.start==repeated.requests[id].start && request.goal==repeated.requests[id].goal,
            "Fixed seed changed agent endpoints");
        differs|=request.start!=changed.requests[id].start;
        require(request.path.front()==viewer::navmesh_path::Point{request.start.x,request.start.z} &&
            request.path.back()==request.goal,"Stored g+8h path changed endpoints");
        const double distance=std::hypot(request.goal.x-request.start.x,request.goal.z-request.start.z);
        require(distance>=14.999 && distance<=30.001,"Woodland endpoint distance outside advertised range");
        for (auto endpoint:{viewer::navmesh_path::Point{request.start.x,request.start.z},request.goal})
            for (const auto& trunk:woodland.trunks)
                require(std::hypot(endpoint.x-trunk.base.x,endpoint.z-trunk.base.z)>=trunk.radius+.35,
                    "Random woodland endpoint is inside a physical trunk clearance");
    }
    require(differs,"Changing seed failed to change benchmark requests");
    for (std::size_t id=0;id<woodland.trunks.size();++id)
        require(woodland.trunks[id].base==repeated.trunks[id].base &&
                woodland.trunks[id].radius==repeated.trunks[id].radius,"Fixed seed changed trunk geometry");
}
void referenceMovementAndUnreachableGap() {
    const auto scenes=makeScenarios(8,1234);
    SimulationConfig config; config.duration=8;
    viewer::steering::Config controller;
    const auto& empty=named(scenes,"empty");
    const viewer::CollisionWorld emptyWorld(empty.terrain,empty.trunks);
    auto mismatch=controller; mismatch.radius=.2f;
    bool rejected=false;
    try { (void)simulate(empty,emptyWorld,Mode::collision_only,config,mismatch); }
    catch (const std::invalid_argument&) { rejected=true; }
    require(rejected,"Steering radius may not diverge from the actual fixed physical capsule");
    auto excessive=config; excessive.duration=1e30f; rejected=false;
    try { (void)simulate(empty,emptyWorld,Mode::collision_only,excessive,controller); }
    catch (const std::invalid_argument&) { rejected=true; }
    require(rejected,"Excessive step count must reject before integer conversion");
    for (auto mode:{Mode::collision_only,Mode::gradient,Mode::gradient_side,Mode::perpendicular_push,Mode::detour}) {
        const auto result=simulate(empty,emptyWorld,mode,config,controller);
        safe(result);
        require(result.arrived==1 && result.stalled==0,"Unobstructed route did not arrive");
        require(std::abs(result.agents[0].travel-10)<.16,"Unobstructed movement changed route length");
        require(result.agents[0].arrivalSeconds>=4.9 && result.agents[0].arrivalSeconds<=5.1,
            "Unobstructed agent exceeded speed or failed to maintain speed");
        require(result.agents[0].collisionInterventions==0,"Empty terrain unexpectedly intervened in movement");
    }
    const auto& head=named(scenes,"head_on");
    const viewer::CollisionWorld headWorld(head.terrain,head.trunks);
    const auto stopped=simulate(head,headWorld,Mode::collision_only,config,controller);
    safe(stopped);
    require(stopped.arrived==0 && stopped.agents[0].collisionInterventions>0,
        "Exact head-on physical-only movement did not expose the stopping baseline");
    const auto& sealed=named(scenes,"sealed_gap");
    const viewer::CollisionWorld sealedWorld(sealed.terrain,sealed.trunks);
    for (auto mode:{Mode::gradient_side,Mode::perpendicular_push,Mode::detour}) {
        const auto result=simulate(sealed,sealedWorld,mode,config,controller);
        safe(result);
        require(result.arrived==0,"Local controller crossed a genuinely sealed corridor");
    }
}
}
int main() try {
    sharedSweepGuard(); fixturesAndRepeatability(); referenceMovementAndUnreachableGap();
    std::cout<<"Steering simulation geometry, deterministic inputs, speed and common collision tests passed\n";
    return 0;
} catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
