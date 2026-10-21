#pragma once
#include "potential_surface_steering.hpp"
#include <cstdint>
#include <iosfwd>
#include <vector>

namespace viewer::potential_surface_benchmark {
using potential_surface::Point;
using potential_surface::SteeringMode;
struct Request { std::uint32_t id=0;Point start,goal; };
struct Config {
    double timestep=.05,speed=2,traceInterval=1;
    SteeringMode steering=SteeringMode::filtered;
};
enum class Status { active,arrived,stalled,timeout,invalidEndpoint };
const char* statusName(Status);
struct AgentResult {
    std::uint32_t id=0;
    Point start,goal,position;
    Status status=Status::active;
    double arrivalSeconds=-1,travel=0,initialDistance=0,budgetSeconds=0,headingRadians=0,elapsedSeconds=0;
    std::uint64_t updates=0,clippedMoves=0,invalidMoves=0;
    std::uint64_t tangentActivations=0,tangentUpdates=0,tangentReleases=0,retreatUpdates=0;
    std::vector<Point> trace;
};
struct StepResult {
    std::uint64_t step=0;
    std::size_t active=0;
    double milliseconds=0,cpuMilliseconds=0;
};
struct Result {
    std::vector<AgentResult> agents;
    std::vector<StepResult> steps;
    potential_surface::QueryStats queries;
    std::uint64_t updates=0,movedSegments=0,validationSegments=0,validationViolations=0;
    double milliseconds=0,cpuMilliseconds=0;
    std::size_t scratchBytes=0;
};
// Timing contains only the update batch. Two wall-clock and two process-CPU
// probes per batch; no per-agent timers. Native checks and optional full moved
// segment logging occur after each timed batch. The stream receives 36-byte
// little-endian records; its PSSMOV01 header is written by the CLI beforehand.
Result simulate(const potential_surface::World&,const std::vector<Request>&,const Config&,std::ostream* moves=nullptr);
}
