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

TravelerMovement fullStep(std::size_t id,glm::vec3 start,float seconds,const CollisionWorld& world,
    ZombieNavigation& navigation,ZombieCrowd& crowd,WalkingGroundCache& ground,ContactCandidates& contacts) {
    const auto velocity=navigation.follow(id,start,speed,seconds,world);
    const auto desired=start+glm::vec3(velocity.x*seconds,0,velocity.y*seconds);
    auto movement=moveZombieBody(start,desired,world,&crowd,id,&ground,&contacts);
    if (!navigation.movementClear(id,start,movement.feet)) movement={start};
    return {movement,velocity};
}
}

TravelerMovement moveScenarioTraveler(std::size_t id,glm::vec3 start,float seconds,
    const CollisionWorld& world,ZombieNavigation& navigation,ZombieCrowd& crowd,
    WalkingGroundCache& ground,ContactCandidates& contacts,TravelerPlan& plan,
    bool distant) {
    const bool reduced=distant && seconds<=.05f;
    if (!reduced) {
        if (plan.distant) plan={};
        return fullStep(id,start,seconds,world,navigation,crowd,ground,contacts);
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
        const auto step=fullStep(id,start,seconds,world,navigation,crowd,ground,contacts);
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
            if(desired.x!=feet.x || desired.z!=feet.z) target=terrainFeet(desired,feet,world,ground);
            if (target && !world.simpleTravelerClear(feet,*target,radius,height,contacts)) target.reset();
            if (!target || !navigation.movementClear(id,feet,*target)) {
                plan.remaining=0;plan.fallbackSeconds=.25f;
                const auto fallback=fullStep(id,feet,left,world,navigation,crowd,ground,contacts);
                if(feet!=start && !navigation.movementClear(id,start,fallback.movement.feet)) break;
                result.movement.travel+=fallback.movement.travel;
                result.movement.feet=fallback.movement.feet;
                result.velocity+=fallback.velocity*(left/seconds);
                if (zombie_profile::activeProfile) ++zombie_profile::activeProfile->travelerFallbacks;
                break;
            }
            plan.target=*target;plan.remaining=duration;plan.startHeight=feet.y;
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
        if (!supported || accepted.x!=desired.x || accepted.z!=desired.z ||
            !navigation.movementClear(id,feet,accepted)) {
            plan.remaining=0;plan.fallbackSeconds=.25f;
            const auto fallback=fullStep(id,feet,left,world,navigation,crowd,ground,contacts);
            if(feet!=start && !navigation.movementClear(id,start,fallback.movement.feet)) break;
            result.movement.travel+=fallback.movement.travel;
            result.movement.feet=fallback.movement.feet;
            result.velocity+=fallback.velocity*(left/seconds);
            if (zombie_profile::activeProfile) ++zombie_profile::activeProfile->travelerFallbacks;
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
