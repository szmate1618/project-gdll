#include "potential_surface_simulation.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>
#include <limits>
#include <ostream>
#include <set>
#include <stdexcept>

namespace viewer::potential_surface_benchmark {
namespace {
using Clock=std::chrono::steady_clock;
using potential_surface::Point;
double length(Point point) { return std::hypot(point.x,point.z); }
bool same(Point a,Point b) { return a.x==b.x && a.z==b.z; }
struct State {
    Point anchor,lastHeading;
    double anchorTime=0,nextTrace=1;
    potential_surface::SteeringState steering;
    potential_surface::GuidanceState guidance;
    potential_surface::GuidanceStats guidanceStats;
};
struct Segment { std::uint32_t id=0;Point a,b; };
void encode(std::vector<char>& bytes,std::size_t& offset,std::uint64_t value,unsigned count) {
    for (unsigned byte=0;byte<count;++byte) bytes[offset++]=char((value>>(byte*8))&255);
}
void recordMoves(std::ostream& output,const std::vector<Segment>& segments,std::size_t count,std::vector<char>& bytes) {
    static_assert(sizeof(double)==8 && std::numeric_limits<double>::is_iec559,"Audit stream requires IEEE754 doubles");
    std::size_t offset=0;
    for (std::size_t index=0;index<count;++index) {
        const auto& segment=segments[index];
        if (same(segment.a,segment.b)) continue;
        encode(bytes,offset,segment.id,4);
        for (auto coordinate:{segment.a.x,segment.a.z,segment.b.x,segment.b.z}) {
            std::uint64_t word;std::memcpy(&word,&coordinate,sizeof(word));encode(bytes,offset,word,8);
        }
    }
    if (offset) output.write(bytes.data(),std::streamsize(offset));
    if (!output) throw std::runtime_error("Could not write complete movement audit stream");
}
}
const char* statusName(Status status) {
    switch (status) {
        case Status::active:return "active";
        case Status::arrived:return "arrived";
        case Status::stalled:return "stalled";
        case Status::timeout:return "timeout";
        case Status::invalidEndpoint:return "invalid_endpoint";
    }
    return "invalid_status";
}
Result simulate(const potential_surface::World& world,const std::vector<Request>& requests,
                const Config& config,std::ostream* moves) {
    if (!(config.timestep>0 && config.speed>0 && config.traceInterval>0) ||
        !std::isfinite(config.timestep) || !std::isfinite(config.speed) || !std::isfinite(config.traceInterval))
        throw std::invalid_argument("Simulation timestep, speed and trace interval must be finite and positive");
    Result result;
    result.agents.reserve(requests.size());
    std::vector<State> states(requests.size());
    std::vector<Segment> segments(requests.size());
    std::vector<char> moveBytes(moves?requests.size()*36:0);
    std::set<std::uint32_t> ids;
    double maximumBudget=0;
    std::size_t active=requests.size();
    for (std::size_t index=0;index<requests.size();++index) {
        const auto& request=requests[index];
        if (!ids.insert(request.id).second) throw std::invalid_argument("Duplicate agent ID");
        AgentResult agent;
        agent.id=request.id;agent.start=request.start;agent.goal=request.goal;agent.position=request.start;
        agent.initialDistance=length(request.goal-request.start);
        if (!std::isfinite(agent.initialDistance) || !std::isfinite(request.start.x) || !std::isfinite(request.start.z) ||
            !std::isfinite(request.goal.x) || !std::isfinite(request.goal.z))
            throw std::invalid_argument("Request coordinates must be finite");
        agent.budgetSeconds=std::max(120.0,3*agent.initialDistance/config.speed+60);
        if (!std::isfinite(agent.budgetSeconds)) throw std::invalid_argument("Nonfinite agent simulation budget");
        maximumBudget=std::max(maximumBudget,agent.budgetSeconds);
        if (!world.clear(request.start) || !world.clear(request.goal)) {
            agent.status=Status::invalidEndpoint;--active;
        } else if (agent.initialDistance<=.15) {
            agent.status=Status::arrived;agent.arrivalSeconds=0;--active;
        }
        agent.trace.push_back(agent.position);
        states[index].anchor=agent.position;states[index].nextTrace=config.traceInterval;
        result.agents.push_back(std::move(agent));
    }
    const double requestedSteps=std::ceil(maximumBudget/config.timestep);
    if (!std::isfinite(requestedSteps) || requestedSteps>10000000)
        throw std::invalid_argument("Simulation exceeds ten million timestep budget");
    const auto maximumSteps=std::uint64_t(requestedSteps);
    result.steps.reserve(std::size_t(std::min(maximumSteps,std::uint64_t(4096))));
    result.scratchBytes=states.size()*sizeof(State)+segments.size()*sizeof(Segment)+moveBytes.size();
    for (std::uint64_t step=0;step<maximumSteps && active;++step) {
        const double now=(step+1)*config.timestep;
        StepResult measured;measured.step=step;measured.active=active;
        std::size_t segmentCount=0;
        const auto begin=Clock::now();
        const auto cpuBegin=std::clock();
        for (std::size_t index=0;index<result.agents.size();++index) {
            auto& agent=result.agents[index];auto& state=states[index];
            if (agent.status!=Status::active) continue;
            const auto old=agent.position;
            const auto offset=agent.goal-old;
            const double distance=length(offset);
            const auto desired=distance>0?offset*(std::min(config.speed,distance/config.timestep)/distance):Point{};
            const auto steering=potential_surface::steerVelocity(config.steering,world,old,agent.goal,
                desired,config.timestep,state.steering,result.queries,config.guidance,config.guidanceMode,
                &state.guidance,&state.guidanceStats);
            const auto moved=world.move(old,old+steering.velocity*config.timestep,result.queries);
            agent.tangentActivations+=steering.activated;
            agent.tangentUpdates+=steering.tangentUpdate;
            agent.tangentReleases+=steering.released;
            agent.position=moved.position;
            ++agent.updates;++result.updates;
            if (moved.constrained) ++agent.clippedMoves;
            if (moved.invalidStart) ++agent.invalidMoves;
            const auto delta=agent.position-old;
            if (config.steering==SteeringMode::tangent && potential_surface::dot(delta,offset)<0)
                ++agent.retreatUpdates;
            const double travel=length(delta);
            agent.travel+=travel;agent.elapsedSeconds=now;
            if (travel>0) {
                ++result.movedSegments;
                const auto heading=delta/travel;
                if (potential_surface::dot(state.lastHeading,state.lastHeading)>0)
                    agent.headingRadians+=std::atan2(std::abs(heading.x*state.lastHeading.z-heading.z*state.lastHeading.x),
                        potential_surface::dot(heading,state.lastHeading));
                state.lastHeading=heading;
            }
            segments[segmentCount++]={agent.id,old,agent.position};
            if (length(agent.position-state.anchor)>=.05) {
                state.anchor=agent.position;state.anchorTime=now;
            }
            if (length(agent.goal-agent.position)<=.15) {
                agent.status=Status::arrived;agent.arrivalSeconds=now;--active;
            } else if (now-state.anchorTime>=10) {
                agent.status=Status::stalled;--active;
            } else if (now>=agent.budgetSeconds) {
                agent.status=Status::timeout;--active;
            }
        }
        const auto cpuEnd=std::clock();
        const auto end=Clock::now();
        if (cpuBegin==std::clock_t(-1) || cpuEnd==std::clock_t(-1)) throw std::runtime_error("Process CPU clock unavailable");
        measured.milliseconds=std::chrono::duration<double,std::milli>(end-begin).count();
        measured.cpuMilliseconds=double(cpuEnd-cpuBegin)*1000/CLOCKS_PER_SEC;
        result.milliseconds+=measured.milliseconds;result.cpuMilliseconds+=measured.cpuMilliseconds;
        result.steps.push_back(measured);
        // These checks and output deliberately occur after both timing intervals.
        for (std::size_t index=0;index<segmentCount;++index) {
            const auto& segment=segments[index];++result.validationSegments;
            if (!world.segmentClear(segment.a,segment.b)) ++result.validationViolations;
        }
        if (moves) recordMoves(*moves,segments,segmentCount,moveBytes);
        for (std::size_t index=0;index<result.agents.size();++index) {
            auto& agent=result.agents[index];auto& state=states[index];
            if (agent.elapsedSeconds!=now) continue;
            if (now>=state.nextTrace || agent.status!=Status::active) {
                if (!same(agent.trace.back(),agent.position)) agent.trace.push_back(agent.position);
                state.nextTrace=now+config.traceInterval;
            }
        }
    }
    for (std::size_t i=0;i<result.agents.size();++i) {
        auto& agent=result.agents[i];
        agent.guidanceStats=states[i].guidanceStats;
        const auto& stats=agent.guidanceStats;
        auto& total=result.guidanceStats;
        total.queries+=stats.queries;total.nodes+=stats.nodes;total.candidates+=stats.candidates;
        total.bumpContributors+=stats.bumpContributors;total.pocketChecks+=stats.pocketChecks;
        total.pocketContributors+=stats.pocketContributors;total.virtualProbes+=stats.virtualProbes;
        total.escapeActivations+=stats.escapeActivations;total.escapeUpdates+=stats.escapeUpdates;
        total.targetSwitches+=stats.targetSwitches;total.escapeReleases+=stats.escapeReleases;
        total.blockedTargets+=stats.blockedTargets;
        if (agent.status==Status::active) agent.status=Status::timeout;
        if (!same(agent.trace.back(),agent.position)) agent.trace.push_back(agent.position);
    }
    return result;
}
}
