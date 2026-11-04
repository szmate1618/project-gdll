#include "zombie_travel.hpp"
#include "zombie_navigation.hpp"
#include "zombie_crowd.hpp"
#include "collision_world.hpp"
#include "zombie_update_profile.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace viewer {
namespace {
constexpr float speed=1.2f, period=.1f, radius=ZombieCrowd::bodyRadius, height=ZombieCrowd::bodyHeight;
constexpr float minNormalY=.70710678f;
float distance(glm::vec3 a,glm::vec3 b) { return glm::length(glm::vec2(a.x-b.x,a.z-b.z)); }

std::optional<glm::vec3> terrainFeet(glm::vec3 target,glm::vec3 start,const CollisionWorld& world,
                                     WalkingGroundCache& ground) {
    const auto hit=world.walkingGroundAt(target.x,target.z,std::numeric_limits<float>::lowest(),start.y+.30f,&ground);
    if (!hit || !hit->terrain || hit->normal.y<minNormalY) return std::nullopt;
    target.y=hit->height;
    return target;
}

void seedFallbackNavigation(TravelerPlan& plan,const ZombieNavigation& navigation,std::size_t id,
                            glm::vec2 velocity,glm::vec2 target,float seconds) {
    plan.fallbackVelocity=velocity;
    plan.fallbackTarget=target;
    plan.fallbackNavigationSeconds=seconds;
    plan.fallbackPath=navigation.pathFor(id);
}

void refreshFallbackNavigation(TravelerPlan& plan,ZombieNavigation& navigation,std::size_t id,
                               glm::vec3 feet,float frameSeconds,const CollisionWorld& world,bool frameSized=false) {
    frameSized=frameSized || plan.fallbackFrameSized;
    float duration=frameSized?frameSeconds:period;
    auto velocity=navigation.follow(id,feet,speed,duration,world);
    // Float portal corners can admit a frame-sized step while rejecting the
    // longer sample. Such recovery commands expire after that short step.
    if (!frameSized && velocity==glm::vec2(0)) {
        plan.fallbackFrameSized=true;
        duration=frameSeconds;
        velocity=navigation.follow(id,feet,speed,duration,world);
    }
    const auto target=glm::vec2(feet.x,feet.z)+velocity*duration;
    seedFallbackNavigation(plan,navigation,id,velocity,target,velocity==glm::vec2(0)?period:duration);
}

TravelerMovement cachedFallbackStep(std::size_t id,glm::vec3 start,float seconds,const CollisionWorld& world,
    ZombieNavigation& navigation,ZombieCrowd& crowd,WalkingGroundCache& ground,ContactCandidates& contacts,
    TravelerPlan& plan) {
    TravelerMovement result{{start},glm::vec2(0)};
    float left=seconds;
    std::size_t crowdChecks=0;
    while (left>1e-7f) {
        const auto feet=result.movement.feet;
        if (plan.fallbackNavigationSeconds<=1e-7f || plan.fallbackPath!=navigation.pathFor(id))
            refreshFallbackNavigation(plan,navigation,id,feet,left,world);
        bool accepted=false;
        for (int attempt=0;attempt<2;++attempt) {
            if (attempt) refreshFallbackNavigation(plan,navigation,id,feet,left,world,true);
            const float slice=std::min(left,plan.fallbackNavigationSeconds);
            const float magnitude=glm::length(plan.fallbackVelocity);
            const auto direction=magnitude>0?plan.fallbackVelocity/magnitude:glm::vec2(0);
            // Never catch up distance lost to crowd/wall contact, and never
            // walk past the sampled corner/goal plane before the next refresh.
            const float ahead=std::max(0.f,glm::dot(plan.fallbackTarget-glm::vec2(feet.x,feet.z),direction));
            const float travel=std::min(std::min(speed,magnitude)*slice,ahead);
            auto desired=feet+glm::vec3(direction.x*travel,0,direction.y*travel);
            // A close corner/goal may be one float ULP away while each sampled
            // velocity step rounds to zero. Finish only within this frame's
            // speed budget; the normal physical and mesh guards still apply.
            const auto endpoint=plan.fallbackTarget-glm::vec2(feet.x,feet.z);
            if (glm::length(endpoint)<=speed*slice) {
                desired.x=plan.fallbackTarget.x;
                desired.z=plan.fallbackTarget.y;
            }
            auto movement=moveZombieBody(feet,desired,world,&crowd,id,&ground,&contacts,&crowdChecks);
            if (!navigation.movementClear(id,feet,movement.feet)) {
                plan.fallbackNavigationSeconds=0;
                plan.fallbackFrameSized=true;
                continue;
            }
            // Two individually covered legs do not prove their published
            // start-to-final chord safe when a sample expires near a corner.
            if (feet!=start && !navigation.movementClear(id,start,movement.feet)) {
                plan.fallbackNavigationSeconds=0;
                return result;
            }
            result.movement.feet=movement.feet;
            result.movement.travel+=movement.travel;
            result.velocity+=plan.fallbackVelocity*(slice/seconds);
            plan.fallbackNavigationSeconds=std::max(0.f,plan.fallbackNavigationSeconds-slice);
            left-=slice;
            accepted=true;
            break;
        }
        if (!accepted) break;
    }
    return result;
}

TravelerMovement fullStep(std::size_t id,glm::vec3 start,float seconds,const CollisionWorld& world,
    ZombieNavigation& navigation,ZombieCrowd& crowd,WalkingGroundCache& ground,ContactCandidates& contacts,
    zombie_profile::TravelerSolver reason,TravelerPlan* plan=nullptr) {
    auto* profile=zombie_profile::activeProfile;
    zombie_profile::PhaseTimer phase(&zombie_profile::Profile::travelerFullSeconds);
    const auto coverageBefore=profile?profile->stages[std::size_t(zombie_profile::Stage::coverage)].seconds:0;
    if (profile) ++profile->travelerSolverSteps[std::size_t(reason)];
    TravelerMovement result;
    if (plan) result=cachedFallbackStep(id,start,seconds,world,navigation,crowd,ground,contacts,*plan);
    else {
        result.velocity=navigation.follow(id,start,speed,seconds,world);
        const auto desired=start+glm::vec3(result.velocity.x*seconds,0,result.velocity.y*seconds);
        result.movement=moveZombieBody(start,desired,world,&crowd,id,&ground,&contacts);
        if (!navigation.movementClear(id,start,result.movement.feet)) result.movement={start};
    }
    if (profile) profile->travelerFullCoverageSeconds+=profile->stages[std::size_t(zombie_profile::Stage::coverage)].seconds-coverageBefore;
    return result;
}
}

TravelerMovement moveScenarioTraveler(std::size_t id,glm::vec3 start,float seconds,
    const CollisionWorld& world,ZombieNavigation& navigation,ZombieCrowd& crowd,
    WalkingGroundCache& ground,ContactCandidates& contacts,TravelerPlan& plan,
    bool distant) {
    const bool reduced=distant && seconds<=.05f;
    if (!reduced) {
        if (plan.distant) plan={};
        return fullStep(id,start,seconds,world,navigation,crowd,ground,contacts,zombie_profile::TravelerSolver::direct);
    }
    const auto worldId=world.identity();
    if (plan.worldId!=worldId || (plan.distant && plan.lastFeet!=start)) plan={};
    plan.worldId=worldId;
    plan.lastFeet=start;
    if (!plan.distant) {
        plan={}; plan.distant=true;plan.worldId=worldId;plan.lastFeet=start;
    }
    plan.fallbackSeconds=std::max(0.f,plan.fallbackSeconds-seconds);
    if (plan.fallbackSeconds>0) {
        const auto step=fullStep(id,start,seconds,world,navigation,crowd,ground,contacts,zombie_profile::TravelerSolver::cooldown,&plan);
        plan.lastFeet=step.movement.feet;
        return step;
    }
    TravelerMovement result{{start},glm::vec2(0)};
    float left=seconds;
    while (left>1e-7f) {
        const auto feet=result.movement.feet;
        if (plan.remaining<=1e-7f) {
            zombie_profile::Timer timer(zombie_profile::Stage::travelerPlanning);
            if (zombie_profile::activeProfile) ++zombie_profile::activeProfile->travelerPlans;
            // Stable phase on the first plan; subsequent plans use a full 100ms.
            const float duration=plan.phased?period:.05f+.05f*float((id*2654435761ull)%997)/997.f;
            plan.phased=true;
            plan.velocity=navigation.follow(id,feet,speed,duration,world);
            const auto desired=feet+glm::vec3(plan.velocity.x*duration,0,plan.velocity.y*duration);
            // A rejected long navigation step can return zero even though the
            // ordinary frame step is valid. Do not cache a stationary segment.
            std::optional<glm::vec3> target;
            using Failure=zombie_profile::TravelerFailure;
            auto failure=Failure::count;
            if(desired.x!=feet.x || desired.z!=feet.z) {
                target=terrainFeet(desired,feet,world,ground);
                if (!target) failure=Failure::targetSupport;
                else if (!world.simpleTravelerClear(feet,*target,radius,height,contacts)) failure=Failure::sceneClearance;
                else if (!navigation.movementClear(id,feet,*target)) failure=Failure::targetCoverage;
            } else failure=Failure::zeroVelocity;
            if (failure!=Failure::count) {
                plan.fallbackNavigationSeconds=0;
                plan.fallbackFrameSized=failure==Failure::zeroVelocity || failure==Failure::targetCoverage;
                if (failure!=Failure::zeroVelocity && failure!=Failure::targetCoverage)
                    seedFallbackNavigation(plan,navigation,id,plan.velocity,{desired.x,desired.z},duration);
                plan.remaining=0;plan.fallbackSeconds=.25f;
                if (zombie_profile::activeProfile) {
                    ++zombie_profile::activeProfile->travelerFailures[std::size_t(failure)];
                    ++zombie_profile::activeProfile->travelerFallbacks;
                }
                const auto fallback=fullStep(id,feet,left,world,navigation,crowd,ground,contacts,zombie_profile::TravelerSolver::fallback,&plan);
                if(feet!=start && !navigation.movementClear(id,start,fallback.movement.feet)) break;
                result.movement.travel+=fallback.movement.travel;
                result.movement.feet=fallback.movement.feet;
                result.velocity+=fallback.velocity*(left/seconds);
                break;
            }
            plan.target=*target;plan.remaining=duration;plan.startHeight=feet.y;
            plan.fallbackFrameSized=false;
        }
        const float slice=std::min(left,plan.remaining);
        const bool endpoint=slice>=plan.remaining;
        auto desired=endpoint?plan.target:feet+(plan.target-feet)*(slice/plan.remaining);
        bool supported=true;
        if (!endpoint) {
            const auto grounded=terrainFeet(desired,feet,world,ground);
            // Stay in the height envelope certified by the swept body query.
            // Sharp crests, dips and changed support resume full movement now.
            if (grounded && grounded->y>=std::min(plan.startHeight,plan.target.y)-.001f &&
                grounded->y<=std::max(plan.startHeight,plan.target.y)+.001f) desired=*grounded;
            else supported=false;
        }
        std::size_t checks=0;
        auto accepted=supported?crowd.constrainFrom(id,feet,desired,checks):feet;
        using Failure=zombie_profile::TravelerFailure;
        const auto failure=!supported?Failure::sliceSupport:
            accepted.x!=desired.x || accepted.z!=desired.z?Failure::crowdCorrection:
            !navigation.movementClear(id,feet,accepted)?Failure::sliceCoverage:Failure::count;
        if (failure!=Failure::count) {
            plan.fallbackNavigationSeconds=0;
            plan.fallbackFrameSized=failure==Failure::sliceCoverage;
            if (failure!=Failure::sliceCoverage)
                seedFallbackNavigation(plan,navigation,id,plan.velocity,{plan.target.x,plan.target.z},plan.remaining);
            plan.remaining=0;plan.fallbackSeconds=.25f;
            if (zombie_profile::activeProfile) {
                ++zombie_profile::activeProfile->travelerFailures[std::size_t(failure)];
                ++zombie_profile::activeProfile->travelerFallbacks;
            }
            const auto fallback=fullStep(id,feet,left,world,navigation,crowd,ground,contacts,zombie_profile::TravelerSolver::fallback,&plan);
            if(feet!=start && !navigation.movementClear(id,start,fallback.movement.feet)) break;
            result.movement.travel+=fallback.movement.travel;
            result.movement.feet=fallback.movement.feet;
            result.velocity+=fallback.velocity*(left/seconds);
            break;
        }
        if(feet!=start && !navigation.movementClear(id,start,accepted)) break;
        result.movement.travel+=distance(feet,accepted);
        result.movement.feet=accepted;
        result.velocity+=plan.velocity*(slice/seconds);
        plan.remaining-=slice;
        left-=slice;
        if (zombie_profile::activeProfile) ++zombie_profile::activeProfile->travelerInterpolations;
    }
    plan.lastFeet=result.movement.feet;
    return result;
}
} // namespace viewer
