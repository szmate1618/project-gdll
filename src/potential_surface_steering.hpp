#pragma once
#include "potential_surface.hpp"
#include "potential_surface_guidance.hpp"

namespace viewer::potential_surface {

enum class SteeringMode { filtered, normalized, tangent };

// Fixed per-agent state for a local contour-following episode. Reset when the
// agent or its destination changes. No obstacle list, route or search is stored.
struct SteeringState {
    bool initialized=false, active=false;
    int side=0; // Deterministic positive/negative X/Z orbit side; fixed per episode.
    Point outward;
    double previousDistance=0, slowSeconds=0, entryDistance=0, targetPotential=0;
};
struct SteeringDecision {
    Point velocity;
    bool activated=false, released=false, tangentUpdate=false;
    bool guidanceActivated=false,guidanceReleased=false,guidanceUpdate=false;
};

// Desired velocity already respects speed and distance-to-goal/timestep caps.
// filtered preserves the original calculation and query count exactly.
// normalized only rescales that vector; it adds no recovery or extra queries.
// tangent adds fixed local probes <=1.5 m and persistent contour following.
// All modes return velocity only; World::move remains the strict physical guard.
SteeringDecision steerVelocity(SteeringMode mode,const World&,Point position,Point goal,
                               Point desired,double dt,SteeringState&,QueryStats&,
                               const Guidance* guidance=nullptr,GuidanceMode guidanceMode=GuidanceMode::none,
                               GuidanceState* guidanceState=nullptr,GuidanceStats* guidanceStats=nullptr);

} // namespace viewer::potential_surface
