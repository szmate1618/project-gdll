#include "potential_surface_steering.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace viewer::potential_surface {
namespace {
double length(Point p) { return std::hypot(p.x,p.z); }
Point left(Point p) { return {-p.z,p.x}; }
Point unit(Point p,Point fallback) {
    const double magnitude=length(p);
    return magnitude>1e-14?p/magnitude:fallback;
}
bool finite(Point p) { return std::isfinite(p.x) && std::isfinite(p.z); }
// These are fixed experiment constants, not workload-tuned parameters.
constexpr double probeLength=1.5;
constexpr double slowProgressFraction=.1, slowProgressSeconds=.5;
constexpr double releaseImprovement=.25;
constexpr double minimumContourPotential=.02, contourFraction=.9, maximumRadialBias=.6;
}

SteeringDecision steerVelocity(SteeringMode mode,const World& world,Point position,Point goal,
                               Point desired,double dt,SteeringState& state,QueryStats& stats,
                               const Guidance* guidance,GuidanceMode guidanceMode,
                               GuidanceState* guidanceState,GuidanceStats* guidanceStats) {
    if (!finite(position) || !finite(goal) || !finite(desired) || !std::isfinite(dt) || dt<=0)
        throw std::invalid_argument("Invalid local steering input");
    GuidanceTarget target{goal};
    Point intended=desired;
    GuidanceStats unusedStats;
    auto& guidanceWork=guidanceStats?*guidanceStats:unusedStats;
    const bool hierarchy=guidanceMode==GuidanceMode::hierarchy || guidanceMode==GuidanceMode::combined;
    const bool pockets=guidanceMode==GuidanceMode::pockets || guidanceMode==GuidanceMode::combined;
    if (guidanceMode!=GuidanceMode::none) {
        if (!guidance || (!hierarchy && !pockets)) throw std::invalid_argument("Missing or invalid guidance configuration");
        if (pockets) {
            if (!guidanceState) throw std::invalid_argument("Pocket guidance requires persistent per-agent state");
            target=guidance->selectTarget(position,goal,world,*guidanceState,guidanceWork,stats);
            if (target.changed) state=SteeringState{};
            if (target.active) {
                const auto offset=target.position-position;
                const double distance=length(offset),speed=length(desired);
                intended=distance>0?offset*(std::min(speed,distance/dt)/distance):Point{};
            }
        }
    }
    const auto ignorePocket=guidanceState?guidanceState->activePocket:UINT32_MAX;
    const auto probeClear=[&](Point end) {
        return world.segmentClear(position,end,stats) &&
            (!pockets || guidance->pocketSegmentClear(position,end,goal,ignorePocket,guidanceWork));
    };
    const auto finish=[&](SteeringDecision decision) {
        decision.guidanceActivated=target.activated;
        decision.guidanceReleased=target.released;
        decision.guidanceUpdate=target.active;
        return decision;
    };
    auto field=world.field(position,stats);
    if (hierarchy) {
        const auto coarse=guidance->field(position,goal,guidanceWork);
        field.potential+=coarse.potential;field.gradient=field.gradient+coarse.gradient;
    }
    if (pockets) {
        const auto virtualField=guidance->pocketField(position,goal,ignorePocket,guidanceWork);
        field.potential+=virtualField.potential;field.gradient=field.gradient+virtualField.gradient;
    }
    const auto filtered=filterVelocity(intended,field.gradient,world.config().weight);
    if (mode==SteeringMode::filtered) return finish({filtered});
    const double speed=length(intended);
    if (speed==0) return finish({});
    const auto goalDirection=intended/speed;
    const auto normalized=unit(filtered,goalDirection)*speed;
    if (mode==SteeringMode::normalized) return finish({normalized});
    if (mode!=SteeringMode::tangent) throw std::invalid_argument("Unknown local steering mode");

    const double distance=length(target.position-position);
    if (state.initialized) {
        if (state.previousDistance-distance<slowProgressFraction*speed*dt)
            state.slowSeconds+=dt;
        else state.slowSeconds=0;
    }
    state.initialized=true;
    state.previousDistance=distance;
    const double probe=std::min(probeLength,distance);
    const bool goalClear=probeClear(position+goalDirection*probe);
    const auto gradientDirection=unit(field.gradient,{});
    const bool hasGradient=length(gradientDirection)>0;
    if (hasGradient) state.outward=gradientDirection*(-1);
    else if (length(state.outward)==0) state.outward=goalDirection*(-1);

    SteeringDecision result;
    if (state.active && distance<=state.entryDistance-releaseImprovement && goalClear &&
        probeClear(position+normalized*(probe/speed))) {
        state.active=false;
        state.side=0;
        state.slowSeconds=0;
        result.velocity=normalized;
        result.released=true;
        return finish(result);
    }
    if (!state.active) {
        const bool filteredClear=probeClear(position+normalized*(probe/speed));
        if (goalClear && filteredClear && state.slowSeconds<slowProgressSeconds)
            return finish({normalized});
        const auto tangent=left(state.outward)*(-1);
        const auto plus=unit(tangent+state.outward*.1,tangent);
        const auto minus=unit(tangent*(-1)+state.outward*.1,tangent*(-1));
        const bool plusClear=probeClear(position+plus*probe);
        const bool minusClear=probeClear(position+minus*probe);
        state.side=plusClear!=minusClear?(plusClear?1:-1):
            (dot(plus,goalDirection)>=dot(minus,goalDirection)?1:-1);
        state.active=true;
        state.entryDistance=distance;
        state.targetPotential=std::max(minimumContourPotential,contourFraction*field.potential);
        state.slowSeconds=0;
        result.activated=true;
    }
    // A fixed positive outward bias would drift out of the field and leave an
    // agent marching indefinitely along its last normal. Instead keep a local
    // potential contour: outside it the radial term points back toward the field.
    const double radial=std::clamp((field.potential-state.targetPotential)/state.targetPotential,
                                   -maximumRadialBias,maximumRadialBias);
    const auto tangent=left(state.outward)*(-double(state.side));
    result.velocity=unit(tangent+state.outward*radial,tangent)*speed;
    result.tangentUpdate=true;
    return finish(result);
}

} // namespace viewer::potential_surface
