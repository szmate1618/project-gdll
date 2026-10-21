#include "zombie_steering.hpp"
#include "DetourObstacleAvoidance.h"

#include <algorithm>
#include <cmath>
#include <climits>
#include <new>
#include <stdexcept>
#include <glm/geometric.hpp>

namespace viewer::steering {
namespace {
bool finite(glm::vec2 v) { return std::isfinite(v.x) && std::isfinite(v.y); }
glm::vec2 left(glm::vec2 v) { return {-v.y,v.x}; }
glm::vec2 cap(glm::vec2 v,float maximum) {
    const float n2=glm::dot(v,v);
    return n2>maximum*maximum ? v*(maximum/std::sqrt(n2)) : v;
}
float combinedRadius(const Obstacle& obstacle,const Config& config) {
    return obstacle.radius+config.radius+config.skin;
}
struct Field { float value=0; glm::vec2 derivative{}; };
Field field(glm::vec2 position,const Obstacle* obstacles,std::size_t count,const Config& config) {
    Field result;
    for (std::size_t i=0;i<count;++i) {
        const auto& obstacle=obstacles[i];
        const auto offset=position-obstacle.center;
        const float distance2=glm::dot(offset,offset);
        const float scale=combinedRadius(obstacle,config);
        const float range=scale+config.influenceRange;
        const float range2=range*range;
        if (distance2>=range2) continue;
        const float inverseScale2=1/(scale*scale);
        const float inverseBase=1/(1+distance2*inverseScale2);
        const float taper=1-distance2/range2;
        const float taper2=taper*taper, taper3=taper2*taper;
        result.value+=taper3*inverseBase;
        // Product rule for (1-r²/R²)^3 / (1+r²/s²), inside R.
        result.derivative-=2.f*offset*(taper3*inverseBase*inverseBase*inverseScale2+
                                     3*taper2*inverseBase/range2);
    }
    return result;
}
glm::vec2 filtered(glm::vec2 desired,glm::vec2 gradient,float weight) {
    const float uphill=std::max(0.f,glm::dot(gradient,desired));
    return desired-(weight*uphill/(1+weight*glm::dot(gradient,gradient)))*gradient;
}

// Retain a chosen passing side until the selected trunk is behind us. Only
// select obstacles whose inflated disk threatens our desired forward ray.
const Obstacle* passingObstacle(glm::vec2 position,glm::vec2 direction,float speed,
        const Obstacle* obstacles,std::size_t count,const Config& config,
        State& state,Stats& stats) {
    const Obstacle* selected=nullptr;
    for (std::size_t i=0;i<count;++i) {
        const auto& candidate=obstacles[i];
        if (candidate.id==state.obstacle) {
            const auto delta=candidate.center-position;
            if (glm::dot(delta,direction)>=-combinedRadius(candidate,config)) selected=&candidate;
            break;
        }
    }
    if (selected) return selected;
    state.obstacle=UINT32_MAX;
    state.side=0;
    float best=INFINITY;
    for (std::size_t i=0;i<count;++i) {
        const auto& candidate=obstacles[i];
        const auto delta=candidate.center-position;
        const float forward=glm::dot(delta,direction);
        const float radius=combinedRadius(candidate,config);
        const float lateral=std::abs(glm::dot(delta,left(direction)));
        if (forward<=0 || forward>radius+speed*config.horizon || lateral>radius+.15f) continue;
        if (forward<best || (forward==best && selected && candidate.id<selected->id)) {
            best=forward;
            selected=&candidate;
        }
    }
    if (selected) {
        state.obstacle=selected->id;
        // Exact head-on ties always go left. No random perturbation hides the
        // gradient-only head-on deadlock or changes replay reproducibility.
        state.side=glm::dot(position-selected->center,left(direction))>=0 ? 1 : -1;
        ++stats.sideChanges;
    }
    return selected;
}
}

float potential(glm::vec2 position,const Obstacle* obstacles,std::size_t count,const Config& config) {
    return field(position,obstacles,count,config).value;
}
glm::vec2 gradient(glm::vec2 position,const Obstacle* obstacles,std::size_t count,const Config& config) {
    return field(position,obstacles,count,config).derivative;
}
const char* modeName(Mode mode) noexcept {
    switch (mode) {
    case Mode::collision_only:return "collision_only";
    case Mode::gradient:return "gradient";
    case Mode::gradient_side:return "gradient_side";
    case Mode::perpendicular_push:return "perpendicular_push";
    case Mode::detour:return "detour";
    }
    return "unknown";
}
struct Controller::Impl {
    Config config;
    std::size_t maxObstacles;
    dtObstacleAvoidanceQuery query;
    dtObstacleAvoidanceParams params{};
    Impl(Config settings,std::size_t maximum):config(settings),maxObstacles(maximum) {
        if (!std::isfinite(config.radius) || config.radius<=0 ||
            !std::isfinite(config.skin) || config.skin<0 ||
            !std::isfinite(config.influenceRange) || config.influenceRange<=0 ||
            !std::isfinite(config.gradientWeight) || config.gradientWeight<0 ||
            !std::isfinite(config.horizon) || config.horizon<=0 ||
            !std::isfinite(config.tangentWeight) || config.tangentWeight<0 ||
            !maximum || maximum>INT_MAX) throw std::invalid_argument("invalid steering configuration");
        // One unused segment slot avoids relying on malloc(0) semantics in the
        // unmodified upstream init(). This experiment supplies trunk circles.
        if (!query.init(static_cast<int>(maximum),1)) throw std::bad_alloc();
        // Fixed adaptive sampling parameters, explicit for replay.
        params.velBias=.4f;
        params.weightDesVel=2.f;
        params.weightCurVel=.75f;
        params.weightSide=.75f;
        params.weightToi=2.5f;
        params.horizTime=config.horizon;
        params.gridSize=33;
        params.adaptiveDivs=7;
        params.adaptiveRings=2;
        params.adaptiveDepth=3;
    }
};
Controller::Controller(Config config,std::size_t maxObstacles):impl_(std::make_unique<Impl>(config,maxObstacles)) {}
Controller::~Controller()=default;
const Config& Controller::config() const noexcept { return impl_->config; }
std::size_t Controller::workspaceBytes() const noexcept {
    return sizeof(Impl)+impl_->maxObstacles*sizeof(dtObstacleCircle)+sizeof(dtObstacleSegment);
}
glm::vec2 Controller::steer(Mode mode,glm::vec2 position,glm::vec2 desiredVelocity,float dt,
        const Obstacle* obstacles,std::size_t count,State& state,Stats& stats) {
    if (count>impl_->maxObstacles) throw std::length_error("steering obstacle capacity exceeded");
    if (!finite(position) || !finite(desiredVelocity) || !std::isfinite(dt) || dt<=0 ||
        (count && !obstacles)) throw std::invalid_argument("invalid steering step");
    const float speed=glm::length(desiredVelocity);
    if (mode==Mode::collision_only || speed<=1e-8f || !count) return desiredVelocity;
    stats.obstacleTests+=count;
    const auto& config=impl_->config;
    if (mode==Mode::gradient) return cap(filtered(desiredVelocity,gradient(position,obstacles,count,config),config.gradientWeight),speed);
    if (mode==Mode::gradient_side) {
        auto output=filtered(desiredVelocity,gradient(position,obstacles,count,config),config.gradientWeight);
        const auto direction=desiredVelocity/speed;
        const auto* selected=passingObstacle(position,direction,speed,obstacles,count,config,state,stats);
        if (selected) {
            const auto outward=position-selected->center;
            const float distance=glm::length(outward);
            const float radius=combinedRadius(*selected,config);
            // Smooth urgency fades at the common influence limit; do not
            // restore speed suppressed by the gradient filter.
            const float urgency=std::clamp(1-(distance-radius)/config.influenceRange,0.f,1.f);
            const auto tangent=distance>1e-8f ? -static_cast<float>(state.side)*left(outward/distance) :
                                              static_cast<float>(state.side)*left(direction);
            output+=tangent*(config.tangentWeight*speed*urgency);
        }
        return cap(output,speed);
    }
    if (mode==Mode::perpendicular_push) {
        const auto direction=desiredVelocity/speed;
        const auto perpendicular=left(direction);
        passingObstacle(position,direction,speed,obstacles,count,config,state,stats);
        glm::vec2 correction{};
        const auto proposed=position+desiredVelocity*dt;
        for (std::size_t i=0;i<count;++i) {
            const auto& obstacle=obstacles[i];
            const float distance=glm::length(position-obstacle.center);
            if (distance>combinedRadius(obstacle,config)+config.influenceRange) continue;
            const float closer=std::max(0.f,distance-glm::length(proposed-obstacle.center));
            const float away=glm::dot(position-obstacle.center,perpendicular);
            const int side=obstacle.id==state.obstacle && state.side ? state.side : (away>=0 ? 1 : -1);
            correction+=perpendicular*(closer*side);
        }
        // Exact user rule: one meter of sideways displacement for each meter
        // closer during this proposed frame; no extra avoidance gain.
        return cap(desiredVelocity+correction/dt,speed);
    }
    if (mode==Mode::detour) {
        impl_->query.reset();
        constexpr float stationary[3]={0,0,0};
        for (std::size_t i=0;i<count;++i) {
            const float center[3]={obstacles[i].center.x,0,obstacles[i].center.y};
            impl_->query.addCircle(center,obstacles[i].radius,stationary,stationary);
        }
        const float location[3]={position.x,0,position.y};
        const float current[3]={state.velocity.x,0,state.velocity.y};
        const float desired[3]={desiredVelocity.x,0,desiredVelocity.y};
        float output[3]{};
        stats.velocitySamples+=impl_->query.sampleVelocityAdaptive(location,config.radius+config.skin,
            speed,current,desired,output,&impl_->params);
        return cap(glm::vec2(output[0],output[2]),speed);
    }
    throw std::invalid_argument("unknown steering mode");
}
} // namespace viewer::steering
