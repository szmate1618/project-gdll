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
                               Point desired,double dt,SteeringState& state,QueryStats& stats) {
    if (!finite(position) || !finite(goal) || !finite(desired) || !std::isfinite(dt) || dt<=0)
        throw std::invalid_argument("Invalid local steering input");
    const auto field=world.field(position,stats);
    const auto filtered=filterVelocity(desired,field.gradient,world.config().weight);
    if (mode==SteeringMode::filtered) return {filtered};
    const double speed=length(desired);
    if (speed==0) return {};
    const auto goalDirection=desired/speed;
    const auto normalized=unit(filtered,goalDirection)*speed;
    if (mode==SteeringMode::normalized) return {normalized};
    if (mode!=SteeringMode::tangent) throw std::invalid_argument("Unknown local steering mode");

    const double distance=length(goal-position);
    if (state.initialized) {
        if (state.previousDistance-distance<slowProgressFraction*speed*dt)
            state.slowSeconds+=dt;
        else state.slowSeconds=0;
    }
    state.initialized=true;
    state.previousDistance=distance;
    const double probe=std::min(probeLength,distance);
    const bool goalClear=world.segmentClear(position,position+goalDirection*probe,stats);
    const auto gradientDirection=unit(field.gradient,{});
    const bool hasGradient=length(gradientDirection)>0;
    if (hasGradient) state.outward=gradientDirection*(-1);
    else if (length(state.outward)==0) state.outward=goalDirection*(-1);

    SteeringDecision result;
    if (state.active && distance<=state.entryDistance-releaseImprovement && goalClear &&
        world.segmentClear(position,position+normalized*(probe/speed),stats)) {
        state.active=false;
        state.side=0;
        state.slowSeconds=0;
        result.velocity=normalized;
        result.released=true;
        return result;
    }
    if (!state.active) {
        const bool filteredClear=world.segmentClear(position,position+normalized*(probe/speed),stats);
        if (goalClear && filteredClear && state.slowSeconds<slowProgressSeconds)
            return {normalized};
        const auto tangent=left(state.outward)*(-1);
        const auto plus=unit(tangent+state.outward*.1,tangent);
        const auto minus=unit(tangent*(-1)+state.outward*.1,tangent*(-1));
        const bool plusClear=world.segmentClear(position,position+plus*probe,stats);
        const bool minusClear=world.segmentClear(position,position+minus*probe,stats);
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
    return result;
}

} // namespace viewer::potential_surface
