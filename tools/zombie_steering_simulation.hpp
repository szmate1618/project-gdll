#pragma once
#include "zombie_steering_scenarios.hpp"
#include "zombie_steering.hpp"
#include <cstdint>
#include <vector>

namespace viewer::steering_benchmark {
struct SimulationConfig {
    float timestep = .05f;
    float speed = 2;
    float duration = 60;
    std::size_t neighborCapacity = 256;
    unsigned traceEvery = 5;
};
struct StepMeasurement {
    unsigned step = 0;
    std::size_t active = 0;
    double totalMilliseconds = 0, queryMilliseconds = 0, controllerMilliseconds = 0;
    double movementMilliseconds = 0, sweepMilliseconds = 0, navmeshMilliseconds = 0;
};
struct AgentMeasurement {
    bool arrived = false, stalled = false;
    double arrivalSeconds = -1, travel = 0, headingRadians = 0, finalDistance = 0;
    double noProgressSeconds = 0;
    std::uint64_t updates = 0, collisionInterventions = 0, sweptTrunkInterventions = 0, invalidSweepStarts = 0, navmeshRejections = 0;
    std::vector<glm::vec2> trace;
};
struct Validation {
    std::uint64_t endpointPenetrations = 0, sweptPenetrations = 0, meshViolations = 0;
    std::uint64_t checkedSegments = 0;
    double minimumEndpointClearance = 1e30, minimumSweptClearance = 1e30;
};
struct SimulationResult {
    steering::Mode mode = steering::Mode::collision_only;
    std::vector<StepMeasurement> steps;
    std::vector<AgentMeasurement> agents;
    std::vector<glm::vec2> positions; // Full frame-major accepted float32 X/Z trajectory.
    unsigned frames = 0;
    steering::Stats steeringStats;
    Validation validation;
    std::uint64_t updates = 0, queryOverflows = 0;
    std::size_t maximumNearby = 0, arrived = 0, stalled = 0;
    double totalMilliseconds = 0, queryMilliseconds = 0, controllerMilliseconds = 0;
    double movementMilliseconds = 0, sweepMilliseconds = 0, navmeshMilliseconds = 0;
    std::size_t controllerWorkspaceBytes = 0, historyBytes = 0;
};
// Construction, planning and independent geometric validation are outside these
// timers. Each update uses the shared follower, BVH, physical capsule movement,
// and tree-free navmesh guard. No controller receives a private recovery path.
SimulationResult simulate(const Scenario&,const CollisionWorld&,steering::Mode,
                          const SimulationConfig&,const steering::Config&);
}
