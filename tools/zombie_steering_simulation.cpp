#include "zombie_steering_simulation.hpp"
#include "zombie_path_follow.hpp"
#include "zombie_movement.hpp"
#include "zombie_crowd.hpp"
#include "zombie_steering_safety.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>

namespace viewer::steering_benchmark {
namespace {
using Clock=std::chrono::steady_clock;
double milliseconds(Clock::time_point a,Clock::time_point b) {
    return std::chrono::duration<double,std::milli>(b-a).count();
}
navmesh_path::Point point(glm::vec2 value) { return {value.x,value.y}; }
glm::vec2 horizontal(glm::vec3 value) { return {value.x,value.z}; }
struct Agent {
    glm::vec3 feet{};
    steering::State steering;
    steering::FollowState following;
    glm::vec2 lastHeading{};
    double bestDistance = 0, lastProgress = 0;
};
// Independent uniform buckets deliberately do not use the collision BVH. Query
// each accepted segment's expanded AABB, then use double-precision circle tests.
class TrunkValidator {
public:
    explicit TrunkValidator(const std::vector<TrunkCollider>& trunks):trunks_(trunks) {
        for (std::size_t id=0;id<trunks.size();++id) {
            const auto& trunk=trunks[id];
            for (int x=cell(double(trunk.base.x)-trunk.radius);x<=cell(double(trunk.base.x)+trunk.radius);++x)
                for (int z=cell(double(trunk.base.z)-trunk.radius);z<=cell(double(trunk.base.z)+trunk.radius);++z)
                    bins_[{x,z}].push_back(id);
        }
        generation_.resize(trunks.size());
    }
    double clearance(glm::vec2 a,glm::vec2 b) {
        if (++current_==0) { std::fill(generation_.begin(),generation_.end(),0); ++current_; }
        double minimum=1e30;
        constexpr double radius=double(ZombieCrowd::bodyRadius);
        const double dx=double(b.x)-a.x,dz=double(b.y)-a.y,squared=dx*dx+dz*dz;
        for (int x=cell(std::min(double(a.x),double(b.x))-radius);x<=cell(std::max(double(a.x),double(b.x))+radius);++x)
            for (int z=cell(std::min(double(a.y),double(b.y))-radius);z<=cell(std::max(double(a.y),double(b.y))+radius);++z) {
                const auto bin=bins_.find({x,z});
                if (bin==bins_.end()) continue;
                for (auto id:bin->second) {
                    if (generation_[id]==current_) continue;
                    generation_[id]=current_;
                    const auto& trunk=trunks_[id];
                    const double t=squared>0?std::clamp(((trunk.base.x-double(a.x))*dx+
                        (trunk.base.z-double(a.y))*dz)/squared,0.0,1.0):0;
                    minimum=std::min(minimum,std::hypot(a.x+t*dx-trunk.base.x,
                        a.y+t*dz-trunk.base.z)-trunk.radius-radius);
                }
            }
        return minimum;
    }
private:
    static int cell(double value) { return int(std::floor(value/2)); }
    const std::vector<TrunkCollider>& trunks_;
    std::map<std::pair<int,int>,std::vector<std::size_t>> bins_;
    std::vector<std::uint32_t> generation_;
    std::uint32_t current_ = 0;
};
void validate(SimulationResult& result,const Scenario& scenario,const std::vector<glm::vec2>& history,
              unsigned stepCount,const SimulationConfig& config) {
    TrunkValidator trunks(scenario.trunks);
    const std::size_t population=scenario.requests.size();
    for (std::size_t id=0;id<population;++id) {
        auto& agent=result.agents[id];
        agent.trace.reserve(stepCount/config.traceEvery+2);
        for (unsigned step=0;step<=stepCount;++step) {
            const auto position=history[std::size_t(step)*population+id];
            if (step%config.traceEvery==0 || step==stepCount) agent.trace.push_back(position);
            const double endpoint=trunks.clearance(position,position);
            result.validation.minimumEndpointClearance=std::min(result.validation.minimumEndpointClearance,endpoint);
            if (endpoint < 0) ++result.validation.endpointPenetrations;
            if (!step) continue;
            const auto previous=history[std::size_t(step-1)*population+id];
            if (previous==position) continue;
            ++result.validation.checkedSegments;
            const double swept=trunks.clearance(previous,position);
            result.validation.minimumSweptClearance=std::min(result.validation.minimumSweptClearance,swept);
            if (swept < 0) ++result.validation.sweptPenetrations;
            if (!scenario.mesh.segmentClear(point(previous),point(position))) ++result.validation.meshViolations;
        }
    }
}
}
SimulationResult simulate(const Scenario& scenario,const CollisionWorld& world,steering::Mode mode,
                          const SimulationConfig& config,const steering::Config& steeringConfig) {
    if (!(config.timestep>0 && config.duration>0 && config.speed>0) || !config.neighborCapacity ||
        !config.traceEvery || !std::isfinite(config.timestep+config.duration+config.speed))
        throw std::invalid_argument("Invalid steering simulation configuration");
    if (steeringConfig.radius!=ZombieCrowd::bodyRadius)
        throw std::invalid_argument("Steering radius must match the fixed physical zombie body radius");
    const double requestedSteps=std::ceil(double(config.duration)/config.timestep);
    if (!std::isfinite(requestedSteps) || requestedSteps>100000)
        throw std::invalid_argument("Simulation exceeds 100000 step safety limit");
    const unsigned steps=unsigned(requestedSteps);
    const float nearbyQueryRange=std::max(steeringConfig.influenceRange+steeringConfig.radius+steeringConfig.skin,
        float(double(config.speed)*config.timestep+ZombieCrowd::bodyRadius+.01));
    if (!std::isfinite(nearbyQueryRange)) throw std::invalid_argument("Nonfinite step query range");
    const std::size_t population=scenario.requests.size();
    if (population>100000000/std::size_t(steps+1)) throw std::invalid_argument("Simulation history exceeds 100 million points");
    SimulationResult result; result.mode=mode; result.steps.reserve(steps); result.agents.resize(population);
    std::vector<Agent> agents(population);
    std::vector<NearbyTrunk> nearby(config.neighborCapacity);
    std::vector<steering::Obstacle> obstacles(config.neighborCapacity);
    std::vector<glm::vec2> history(std::size_t(steps+1)*population);
    result.historyBytes=history.size()*sizeof(glm::vec2);
    steering::Controller controller(steeringConfig,config.neighborCapacity);
    result.controllerWorkspaceBytes=controller.workspaceBytes();
    for (std::size_t id=0;id<population;++id) {
        auto& agent=agents[id]; agent.feet=scenario.requests[id].start;
        const auto& goal=scenario.requests[id].goal;
        agent.bestDistance=std::hypot(goal.x-agent.feet.x,goal.z-agent.feet.z);
        history[id]=horizontal(agent.feet);
    }
    unsigned executed=0;
    for (unsigned step=0;step<steps;++step) {
        StepMeasurement measurement; measurement.step=step;
        const double now=double(step)*config.timestep;
        const auto begin=Clock::now();
        for (std::size_t id=0;id<population;++id) {
            auto& agent=agents[id]; auto& metrics=result.agents[id];
            const auto old=horizontal(agent.feet);
            if (!metrics.arrived) {
                ++measurement.active; ++metrics.updates; ++result.updates;
                const auto& request=scenario.requests[id];
                const auto desired=steering::followPath(request.path.data(),request.path.size(),old,
                    config.speed,config.timestep,agent.following,&scenario.mesh);
                bool overflow=false;
                const auto queryBegin=Clock::now();
                const auto count=world.nearbyTrunks(agent.feet,1.8f,nearbyQueryRange,
                    nearby.data(),nearby.size(),overflow);
                for (std::size_t neighbor=0;neighbor<count;++neighbor) {
                    const auto& trunk=nearby[neighbor];
                    obstacles[neighbor]={trunk.id,{trunk.collider.base.x,trunk.collider.base.z},trunk.collider.radius};
                }
                const auto controllerBegin=Clock::now();
                const auto velocity=overflow?glm::vec2(0):controller.steer(mode,old,desired,
                    config.timestep,obstacles.data(),count,agent.steering,result.steeringStats);
                const auto movementBegin=Clock::now();
                const auto proposed=agent.feet+glm::vec3(velocity.x,0,velocity.y)*config.timestep;
                const auto movement=moveZombieBody(agent.feet,proposed,world);
                const auto guardBegin=Clock::now();
                const auto swept=constrainTrunkSweep(old,horizontal(movement.feet),obstacles.data(),count,
                    ZombieCrowd::bodyRadius);
                auto accepted=glm::vec3(swept.position.x,movement.feet.y,swept.position.y);
                if (swept.constrained) ++metrics.sweptTrunkInterventions;
                if (swept.invalidStart) ++metrics.invalidSweepStarts;
                const auto sweepEnd=Clock::now();
                if (!scenario.mesh.segmentClear(point(old),{accepted.x,accepted.z})) {
                    accepted=agent.feet; ++metrics.navmeshRejections;
                }
                const auto guardEnd=Clock::now();
                measurement.queryMilliseconds+=milliseconds(queryBegin,controllerBegin);
                measurement.controllerMilliseconds+=milliseconds(controllerBegin,movementBegin);
                measurement.movementMilliseconds+=milliseconds(movementBegin,guardBegin);
                measurement.sweepMilliseconds+=milliseconds(guardBegin,sweepEnd);
                measurement.navmeshMilliseconds+=milliseconds(sweepEnd,guardEnd);
                if (overflow) ++result.queryOverflows;
                result.maximumNearby=std::max(result.maximumNearby,count);
                if (glm::length(horizontal(movement.feet)-horizontal(proposed))>.0001f)
                    ++metrics.collisionInterventions;
                agent.feet=accepted;
                const auto delta=horizontal(accepted)-old;
                const float travel=glm::length(delta);
                metrics.travel+=travel;
                agent.steering.velocity=delta/config.timestep;
                if (travel>1e-5f) {
                    const auto heading=delta/travel;
                    if (glm::dot(agent.lastHeading,agent.lastHeading)>0)
                        metrics.headingRadians+=std::atan2(std::abs(double(heading.x)*agent.lastHeading.y-
                            double(heading.y)*agent.lastHeading.x),double(heading.x)*agent.lastHeading.x+
                            double(heading.y)*agent.lastHeading.y);
                    agent.lastHeading=heading;
                }
                const double distance=std::hypot(request.goal.x-accepted.x,request.goal.z-accepted.z);
                if (distance<agent.bestDistance-.05) { agent.bestDistance=distance; agent.lastProgress=now+config.timestep; }
                if (distance<=.15) {
                    metrics.arrived=true; metrics.arrivalSeconds=now+config.timestep; ++result.arrived;
                }
            }
            history[std::size_t(step+1)*population+id]=horizontal(agent.feet);
        }
        measurement.totalMilliseconds=milliseconds(begin,Clock::now());
        result.totalMilliseconds+=measurement.totalMilliseconds;
        result.queryMilliseconds+=measurement.queryMilliseconds;
        result.controllerMilliseconds+=measurement.controllerMilliseconds;
        result.movementMilliseconds+=measurement.movementMilliseconds;
        result.sweepMilliseconds+=measurement.sweepMilliseconds;
        result.navmeshMilliseconds+=measurement.navmeshMilliseconds;
        result.steps.push_back(measurement); executed=step+1;
        if (result.arrived==population) break;
    }
    for (std::size_t id=0;id<population;++id) {
        auto& metrics=result.agents[id]; const auto& agent=agents[id]; const auto& goal=scenario.requests[id].goal;
        metrics.finalDistance=std::hypot(goal.x-agent.feet.x,goal.z-agent.feet.z);
        metrics.noProgressSeconds=std::max(0.0,double(executed)*config.timestep-agent.lastProgress);
        metrics.stalled=!metrics.arrived && metrics.noProgressSeconds>=5;
        if (metrics.stalled) ++result.stalled;
    }
    validate(result,scenario,history,executed,config);
    history.resize(std::size_t(executed+1)*population);
    result.positions=std::move(history);
    result.frames=executed+1;
    return result;
}
}
