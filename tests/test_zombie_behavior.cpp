#include "simulation_clock.hpp"
#include "noise.hpp"
#include "zombie_layer.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using viewer::ZombieState;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

viewer::ZombieLayer crowd(std::initializer_list<glm::vec3> positions = {{0, 0, 0}}) {
    viewer::ZombieLayer layer;
    // Exercise original state-machine rules at every step. Separate tests below
    // exercise the production staggered clock with actual simulation advances.
    layer.setPerceptionStaggering(false);
    layer.assets.emplace_back();
    for (const auto feet : positions) {
        viewer::ZombieInstance instance;
        instance.restRoot = glm::translate(glm::mat4(1), feet);
        instance.transform = instance.restTransform = instance.restRoot;
        layer.instances.push_back(instance);
    }
    return layer;
}

viewer::Model wall(float height) {
    viewer::Model model;
    viewer::Primitive p;
    for (auto point : {glm::vec3(-20, 0, 5), glm::vec3(20, 0, 5),
                       glm::vec3(20, height, 5), glm::vec3(-20, height, 5)}) {
        viewer::Vertex vertex;
        vertex.position = point;
        p.vertices.push_back(vertex);
    }
    p.indices = {0, 1, 2, 0, 2, 3};
    model.primitives.push_back(std::move(p));
    model.draws.push_back({0, glm::mat4(1), "wall"});
    return model;
}

ZombieState state(const viewer::ZombieLayer& layer, std::size_t id = 0) {
    const auto* behavior = layer.behaviors.find(id);
    require(behavior != nullptr, "Expected a behavior object");
    return behavior->state;
}

void clockAndPool() {
    auto& clock = viewer::SimulationClock::instance();
    clock.reset();
    clock.advance(2.5);
    clock.advance(0);
    clock.advance(-1);
    clock.advance(std::numeric_limits<double>::quiet_NaN());
    clock.advance(std::numeric_limits<double>::infinity());
    require(clock.seconds() == 2.5, "The singleton tracks only valid simulated time");
    viewer::ZombieBehaviorPool pool;
    pool.create(10, clock.seconds());
    pool.create(20, clock.seconds());
    pool.create(30, clock.seconds()).target = {1, 2, 3};
    pool.create(20, 100);
    require(pool.entries().size() == 3 && pool.find(20)->createdSeconds == 2.5,
            "Creation is idle, clock-stamped and unique per ID");
    require(pool.find(20)->state == ZombieState::idle, "New behavior starts idle");
    require(pool.remove(20) && !pool.contains(20), "Remove membership with the object");
    require(pool.entries()[1].zombie == 30 && pool.find(30)->target == glm::vec3(1, 2, 3),
            "Middle removal moves the last object and repairs its index");
    require(pool.remove(30) && pool.remove(10) && !pool.remove(10) && pool.entries().empty(),
            "Removing last and sole entries leaves no stale membership");
}

void fovAndActivation() {
    viewer::SimulationClock::instance().reset();
    require(viewer::zombiePlayerInFov({0, 0, 0}, {0, 0, 1}, {0, 90, 10}), "FOV ignores height");
    for (float degrees : {-50.0f, 50.0f}) {
        const float radians = glm::radians(degrees);
        require(viewer::zombiePlayerInFov({0, 0, 0}, {0, 0, 1}, {std::sin(radians) * 10, 0, std::cos(radians) * 10}),
                "Both 50-degree edges belong to the 100-degree cone");
    }
    require(!viewer::zombiePlayerInFov({0, 0, 0}, {0, 0, 1}, {8, 0, 6}), "Outside FOV is rejected");
    require(!viewer::zombiePlayerInFov({0, 0, 0}, {0, 0, 1}, {0, 0, -10}), "Behind is rejected");
    const viewer::CollisionWorld world(viewer::Model{});
    auto layer = crowd({{0, 0, 100}, {0, 0, 100.01f}, {0, 100.01f, 0}, {0, 0, 50}});
    layer.instances[3].ragdoll = true;
    layer.chase(0.1f, {0, 0, 0}, world);
    require(layer.behaviors.entries().size() == 1 && state(layer) == ZombieState::idle,
            "100 m includes boundary; outside the 3D radius and ragdolls stay inactive");
    for (const auto& instance : layer.instances) require(!instance.walking, "Inactive/idle zombies only play idle");
    viewer::SimulationClock::instance().advance(9.999);
    layer.chase(0.1f, {0, 0, 0}, world);
    require(layer.behaviors.contains(0), "Idle object survives before ten seconds");
    viewer::SimulationClock::instance().advance(0.001);
    layer.chase(0.1f, {0, 0, 0}, world);
    require(!layer.behaviors.contains(0), "Idle object expires at ten seconds despite proximity");
    layer.chase(0.1f, {0, 0, 0}, world);
    require(state(layer) == ZombieState::idle && layer.behaviors.find(0)->createdSeconds == 10,
            "A nearby zombie can receive a fresh idle behavior on the next step");
}

void perceptionAndStates() {
    viewer::SimulationClock::instance().reset();
    for (const auto height : {1.0f, 1.5f, 2.0f}) {
        const viewer::CollisionWorld world(wall(height));
        auto layer = crowd();
        layer.chase(0.1f, {0, 0, 10}, world);
        const auto expected = height == 2 ? ZombieState::idle : ZombieState::investigating;
        require(state(layer) == expected, "One/two visible rays investigate; zero stays idle");
        if (height < 2) {
            require(layer.behaviors.find(0)->target == glm::vec3(0, 0, 10), "Partial sight stores current player feet");
            require(std::abs(layer.instances[0].restRoot[3].z - 0.12f) < 1e-4f, "Investigating walks at 1.2 m/s");
            const viewer::CollisionWorld clear(viewer::Model{});
            layer.chase(0.1f, {0, 0, 10}, clear);
            require(state(layer) == ZombieState::chasing, "Three clear rays upgrade investigation to chasing");
            require(std::abs(layer.instances[0].restRoot[3].z - 0.42f) < 1e-4f, "Chasing runs at 3 m/s");
            layer.chase(0.1f, {0, 0, 11}, world);
            require(state(layer) == ZombieState::chasing && layer.behaviors.find(0)->target.z == 11,
                    "Partial sight refreshes target without downgrading chasing");
        }
    }
    const viewer::CollisionWorld clear(viewer::Model{});
    auto layer = crowd();
    layer.chase(0.1f, {0, 0, 10}, clear);
    require(state(layer) == ZombieState::chasing, "Three rays upgrade idle directly to chasing");
    require(layer.instances[0].chaseRevision == 1, "Initial chase acquisition publishes one presentation notification");
    layer.chase(5, {0, 0, 10}, clear);
    require(state(layer) == ZombieState::attacking && !layer.instances[0].walking,
            "Attack range stops locomotion and plays idle even with a large step");
    require(std::abs(layer.instances[0].restRoot[3].z - 9.f) < 1e-4f, "Approach to 1 m before winding up");
    layer.chase(0.1f, {0, 0, 20}, clear);
    require(state(layer) == ZombieState::chasing && layer.instances[0].walking, "Resume pursuit when player leaves attack range");
    require(layer.instances[0].chaseRevision == 1, "Attack-range transitions do not spam new acquisition grunts");
}

void targetMemoryAndEveryIdleTimeout() {
    auto& clock = viewer::SimulationClock::instance();
    clock.reset();
    const viewer::CollisionWorld clear(viewer::Model{});
    auto layer = crowd();
    // Keep the unseen player inside 150 m to isolate the ordinary idle timer.
    layer.chase(0.1f, {0, 0, 10}, clear);
    clock.advance(20);
    layer.chase(4, {0, 0, -120}, clear);
    require(state(layer) == ZombieState::idle && layer.instances[0].restRoot[3].z == 10,
            "Lost sight follows last seen feet and returns to idle on arrival");
    require(layer.behaviors.find(0)->idleSinceSeconds == 20 && !layer.instances[0].walking,
            "Return to idle starts a fresh timer and uses idle animation");
    clock.advance(9);
    layer.chase(0.1f, {0, 0, -120}, clear);
    require(layer.behaviors.contains(0), "Object age does not expire a newly idle state");
    layer.chase(0.1f, {0, 0, 20}, clear);
    require(state(layer) == ZombieState::chasing, "Reacquisition cancels the idle countdown");
    clock.advance(2);
    layer.chase(4, {0, 0, -120}, clear);
    require(state(layer) == ZombieState::idle && layer.behaviors.find(0)->idleSinceSeconds == 31,
            "Every subsequent return to idle resets the countdown");
    clock.advance(9.9);
    layer.chase(0.1f, {0, 0, -120}, clear);
    require(layer.behaviors.contains(0), "Second idle timer survives before its deadline");
    clock.advance(0.1);
    layer.chase(0.1f, {0, 0, -120}, clear);
    require(layer.behaviors.entries().empty(), "Second idle period expires after ten seconds");
}

void removalAndInvalidInputs() {
    auto& clock = viewer::SimulationClock::instance();
    clock.reset();
    const viewer::CollisionWorld clear(viewer::Model{});
    auto layer = crowd({{0, 0, 10}, {0, 0, 20}, {0, 0, 30}});
    layer.chase(0.1f, {0, 0, 0}, clear);
    clock.advance(10);
    layer.chase(0.1f, {0, 0, 0}, clear);
    require(layer.behaviors.entries().empty(), "Expiration must process every swapped-in entry");
    const float nan = std::numeric_limits<float>::quiet_NaN();
    for (float seconds : {0.0f, -1.0f, nan}) layer.chase(seconds, {0, 0, 0}, clear);
    layer.chase(1, {nan, 0, 0}, clear);
    require(layer.behaviors.entries().empty(), "Invalid input must not create objects or corrupt transforms");
    layer.chase(0.1f, {0, 0, 0}, clear);
    layer.instances[1].ragdoll = true;
    layer.chase(0.1f, {0, 0, 0}, clear);
    require(!layer.behaviors.contains(1) && layer.behaviors.entries().size() == 2,
            "Ragdolls are removed without skipping the swapped member");
}

void hearingThresholdAndEligibility() {
    viewer::SimulationClock::instance().reset();
    const viewer::CollisionWorld clear(viewer::Model{});
    auto layer = crowd({{0, 0, 10}, {0, 0, 10.01f}, {0, 10, 6},
                        {0, 0, 3}, {0, 0, 4}, {0, 0, 5}, {0, 0, 6}, {0, 0, 7}});
    layer.chase(0.1f, {0, 0, -20}, clear); // Create idle behaviors; player behind all zombies.
    layer.behaviors.find(3)->state = ZombieState::investigating;
    layer.behaviors.find(4)->state = ZombieState::chasing;
    layer.behaviors.find(5)->state = ZombieState::attacking;
    layer.instances[6].ragdoll = true;
    layer.behaviors.remove(7);
    layer.makeNoise({0, 0, 0}, viewer::walkingFootstepDecibels);
    require(state(layer, 0) == ZombieState::looking, "Walking noise is audible at exactly 10 m");
    require(layer.instances[0].heardNoiseRevision == 1 && layer.instances[1].heardNoiseRevision == 0 &&
            layer.instances[6].heardNoiseRevision == 0, "Only accepted living hearing reactions publish audio notifications");
    require(state(layer, 1) == ZombieState::idle && state(layer, 2) == ZombieState::idle,
            "Walking noise beyond 10 m is inaudible; propagation includes height");
    require(state(layer, 3) == ZombieState::investigating && state(layer, 4) == ZombieState::chasing &&
            state(layer, 5) == ZombieState::attacking && state(layer, 6) == ZombieState::idle,
            "Only idle, living zombies react to noise");
    require(!layer.behaviors.contains(7), "Noise cannot create a behavior object");
    require(layer.behaviors.find(0)->lookDirection == glm::vec2(0, -1) &&
            layer.behaviors.find(0)->target == glm::vec3(0), "Noise stores direction, not a seen-player target");
    layer.makeNoise({10, 0, 10}, 80);
    require(layer.behaviors.find(0)->lookDirection == glm::vec2(0, -1), "A looking zombie ignores new noises");
    require(layer.instances[0].heardNoiseRevision == 1, "Ignored follow-up noise cannot replay a hearing reaction");

    auto invalid = crowd();
    invalid.chase(0.1f, {0, 0, -20}, clear);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    invalid.makeNoise({nan, 0, 0}, 100);
    invalid.makeNoise({10, 0, 0}, nan);
    invalid.makeNoise({0, 0, 0}, 100);
    require(state(invalid) == ZombieState::idle, "Invalid or horizontally coincident noises cannot set a direction");

    auto farther = crowd({{0, 0, 30}, {0, 0, 50}});
    farther.chase(0.1f, {0, 0, -20}, clear);
    farther.makeNoise({0, 0, 0}, viewer::runningFootstepDecibels);
    require(state(farther, 0) == ZombieState::looking && state(farther, 1) == ZombieState::idle,
            "Running wakes a zombie at 30 m but not 50 m");
    farther.makeNoise({0, 0, 0}, viewer::landingDecibels);
    require(state(farther, 1) == ZombieState::looking, "Landing wakes a zombie at 50 m");
}

void lookingTurnAndIdleTimer() {
    auto& clock = viewer::SimulationClock::instance();
    clock.reset();
    const viewer::CollisionWorld blocked(wall(2));
    auto layer = crowd();
    const auto normalization = glm::scale(glm::translate(glm::mat4(1), {0.2f, -0.1f, 0.3f}), glm::vec3(1.3f));
    layer.instances[0].transform = layer.instances[0].restTransform = normalization;
    layer.chase(0.1f, {0, 0, 10}, blocked);
    clock.advance(9);
    layer.makeNoise({0, 0, -10}, viewer::walkingFootstepDecibels);
    clock.advance(1);
    layer.chase(1, {0, 0, 10}, blocked);
    require(state(layer) == ZombieState::looking && !layer.instances[0].walking,
            "Turning stays in looking, plays idle and cancels the previous idle deadline");
    require(glm::length(glm::vec3(layer.instances[0].restRoot[2]) - glm::vec3(1, 0, 0)) < 2e-4f,
            "One second turns 90 degrees toward a source directly behind");
    clock.advance(1);
    layer.chase(1, {0, 0, 10}, blocked);
    const auto& instance = layer.instances[0];
    require(glm::length(glm::vec3(instance.restRoot[2]) - glm::vec3(0, 0, -1)) < 2e-4f,
            "Two seconds complete the 180-degree turn without overshooting");
    require(glm::vec3(instance.restRoot[3]) == glm::vec3(0), "Looking turns in place");
    const auto retained = glm::inverse(instance.restRoot) * instance.restTransform;
    for (int column = 0; column < 4; ++column)
        require(glm::length(retained[column] - normalization[column]) < 2e-4f,
                "Looking preserves asset scale and origin");
    require(state(layer) == ZombieState::idle && layer.behaviors.find(0)->idleSinceSeconds == 11,
            "Finished looking without sight returns to idle with a fresh timer");
    clock.advance(9.9);
    layer.chase(0.1f, {0, 0, 10}, blocked);
    require(layer.behaviors.contains(0), "Looking-to-idle timer survives before ten seconds");
    clock.advance(0.1);
    layer.chase(0.1f, {0, 0, 10}, blocked);
    require(!layer.behaviors.contains(0), "Looking-to-idle expires after ten seconds");
}

void staggeredObservationsAndContinuousMovement() {
    auto& clock = viewer::SimulationClock::instance();
    clock.reset();
    const viewer::CollisionWorld clear(viewer::Model{}), blocked(wall(2));
    auto layer = crowd();
    layer.setPerceptionStaggering(true);
    layer.chase(0.01f, {0, 0, 50}, clear);
    require(state(layer) == ZombieState::chasing && layer.behaviorStats().perceptionChecks == 1,
            "Production staggering gives activation an immediate observation");
    const auto oldFeet = glm::vec3(layer.instances[0].restRoot[3]);
    clock.advance(0.05);
    layer.chase(0.05f, {5, 0, 50}, clear);
    require(layer.behaviorStats().perceptionChecks == 0 && layer.behaviors.find(0)->target == glm::vec3(0, 0, 50),
            "Cached sight must not update the target from the current player position");
    require(layer.instances[0].restRoot[3].z > oldFeet.z && layer.instances[0].restRoot[3].x == 0,
            "Movement continues between observations toward the last seen target");
    clock.advance(0.95);
    layer.chase(0.01f, {5, 0, 50}, clear);
    require(layer.behaviorStats().perceptionChecks == 1 && layer.behaviors.find(0)->target == glm::vec3(5, 0, 50),
            "A scheduled observation refreshes the last seen feet");
    clock.advance(0.1);
    layer.chase(0.01f, {20, 0, 60}, blocked);
    require(layer.behaviorStats().perceptionChecks == 0 && layer.behaviors.find(0)->target == glm::vec3(5, 0, 50),
            "An unsampled player behind cover cannot leak into the remembered target");
    clock.advance(0.9);
    layer.chase(0.01f, {20, 0, 60}, blocked);
    require(layer.behaviorStats().perceptionChecks == 1 && layer.behaviors.find(0)->sight == viewer::ZombieSight::none &&
            layer.behaviors.find(0)->target == glm::vec3(5, 0, 50), "Losing sight preserves target memory");
    clock.advance(0.01);
    const auto feet = glm::vec3(layer.instances[0].restRoot[3]);
    layer.chase(0.01f, feet + glm::vec3(0, 0, 0.5f), clear);
    require(layer.behaviorStats().perceptionChecks == 0 && state(layer) == ZombieState::attacking &&
            !layer.instances[0].walking, "Attack range is checked immediately between perception ticks");
}

void distantPursuitRetainsTargetAndEvictsOnArrival() {
    auto& clock = viewer::SimulationClock::instance();
    const viewer::CollisionWorld clear(viewer::Model{}), partial(wall(1));
    for (const bool staggered : {false, true}) {
        for (const auto* initialWorld : {&clear, &partial}) {
            clock.reset();
            auto layer = crowd();
            layer.setPerceptionStaggering(staggered);
            layer.chase(0.01f, {0, 0, 10}, *initialWorld);
            const auto expected = initialWorld == &clear ? ZombieState::chasing : ZombieState::investigating;
            require(state(layer) == expected, "Fixture must acquire a chasing or investigating target");
            const auto oldFeet = glm::vec3(layer.instances[0].restRoot[3]);
            const auto oldTime = layer.instances[0].walkSeconds;
            clock.advance(.01);
            // Player is ahead and unobstructed: any unsuspended observation
            // would overwrite the old target. The clock isn't due yet either,
            // so cached clear sight must also be discarded on leaving range.
            layer.chase(0.1f, {0, 0, 300}, clear);
            const auto* behavior = layer.behaviors.find(0);
            require(behavior && behavior->perceptionSuspended && behavior->sight == viewer::ZombieSight::none &&
                    behavior->target == glm::vec3(0, 0, 10) && behavior->state == expected,
                    "Distant pursuit must lose cached visibility while retaining its target and state");
            require(layer.behaviorStats().perceptionChecks == 0 && layer.behaviorStats().sightRays == 0,
                    "Suspension must skip both observations and sight rays, including unstaggered mode");
            const float speed = expected == ZombieState::chasing ? 3.0f : 1.2f;
            require(std::abs(layer.instances[0].restRoot[3].z - oldFeet.z - speed * .1f) < 1e-4f &&
                    layer.instances[0].walking && layer.instances[0].running == (expected == ZombieState::chasing) &&
                    layer.instances[0].walkSeconds > oldTime,
                    "Suspended pursuers keep their speed and advancing locomotion animation");
            clock.advance(2);
            layer.chase(.1f, {100, 0, 350}, clear);
            require(layer.behaviorStats().perceptionChecks == 0 && layer.behaviorStats().sightRays == 0 &&
                    layer.behaviors.find(0)->target == glm::vec3(0, 0, 10),
                    "Elapsed deadlines and a moving distant player cannot refresh the frozen target");
            layer.chase(20, {0, 0, 300}, clear);
            require(layer.instances[0].restRoot[3].z == 10 && !layer.instances[0].walking &&
                    !layer.behaviors.contains(0),
                    "Distant arrival reaches the actual last target and evicts idle behavior immediately");
            // The corpse/removed flags are untouched and spatial membership was
            // kept current even when arrival removed the behavior pool entry.
            require(!layer.instances[0].removed && !layer.instances[0].ragdoll,
                    "Behavior eviction must preserve the living zombie");
            layer.chase(.01f, {0, 0, 60}, clear);
            require(layer.behaviorStats().activationCandidates == 1 && layer.behaviorStats().perceptionChecks == 1 &&
                    state(layer) == ZombieState::chasing && layer.behaviors.find(0)->target == glm::vec3(0, 0, 60) &&
                    !layer.behaviors.find(0)->perceptionSuspended,
                    "Evicted arrivals must reactivate through their updated spatial cell with a fresh observation");
        }
    }
    clock.reset();
    auto moved = crowd();
    moved.chase(.01f, {0, 0, 50}, clear);
    moved.chase(.01f, {0, 0, 150}, clear); // Existing entry still observes inside the outer boundary.
    moved.chase(100, {0, 0, 600}, clear);
    require(!moved.behaviors.contains(0) && moved.instances[0].restRoot[3].z == 150,
            "Arrival can evict a pursuer after travelling across several spatial cells");
    moved.chase(.01f, {0, 0, 180}, clear);
    require(moved.behaviorStats().activationCandidates == 1 && moved.behaviors.contains(0) &&
            moved.behaviors.find(0)->target == glm::vec3(0, 0, 180),
            "Evicted arrivals must be indexed at their destination, beyond the original activation-query cells");
}

void perceptionRangeHysteresisAndImmediateResume() {
    auto& clock = viewer::SimulationClock::instance();
    const viewer::CollisionWorld clear(viewer::Model{});
    clock.reset();
    auto layer = crowd();
    layer.setPerceptionStaggering(true);
    layer.chase(.01f, {0, 0, 50}, clear);
    layer.chase(.01f, {0, 0, 300}, clear);
    require(layer.behaviors.find(0)->perceptionSuspended, "Far player suspends an active pursuer");
    clock.advance(2);
    layer.chase(.01f, {0, 0, 120}, clear);
    require(layer.behaviors.find(0)->perceptionSuspended && layer.behaviorStats().perceptionChecks == 0 &&
            layer.behaviors.find(0)->target == glm::vec3(0, 0, 50),
            "Returning through 100-150 m must retain suspension even after an observation deadline");
    layer.setPerceptionStaggering(false);
    layer.chase(.01f, {0, 0, 120}, clear);
    require(layer.behaviorStats().perceptionChecks == 0 && layer.behaviors.find(0)->perceptionSuspended,
            "Disabling staggering must not bypass range suspension");
    layer.setPerceptionStaggering(true);
    layer.behaviors.find(0)->perception.initialized = true;
    layer.behaviors.find(0)->perception.nextSeconds = 10000;
    const auto nearPlayer = glm::vec3(layer.instances[0].restRoot[3]) + glm::vec3(0, 0, 99);
    layer.chase(.01f, nearPlayer, clear);
    require(!layer.behaviors.find(0)->perceptionSuspended && layer.behaviorStats().perceptionChecks == 1 &&
            layer.behaviorStats().sightRays == 3 && layer.behaviors.find(0)->target == nearPlayer &&
            layer.behaviors.find(0)->perception.period == 1,
            "Reentry immediately observes the actual player and starts the normal distant schedule");
    clock.advance(.01);
    layer.chase(.01f, nearPlayer + glm::vec3(1, 0, 0), clear);
    require(layer.behaviorStats().perceptionChecks == 0 && layer.behaviors.find(0)->target == nearPlayer,
            "Immediate resume must be followed by scheduled rather than every-step observations");

    // Exact boundaries use untouched feet so locomotion doesn't perturb the
    // distance predicate before it is tested. Both histories at 150 m matter.
    for (const bool suspended : {false, true}) {
        auto boundary = crowd();
        auto& behavior = boundary.behaviors.create(0, clock.seconds());
        behavior.state = ZombieState::chasing;
        behavior.target = {0, 0, 50};
        behavior.perceptionSuspended = suspended;
        boundary.chase(.001f, {0, 0, 150}, clear);
        require(boundary.behaviors.find(0)->perceptionSuspended == suspended &&
                boundary.behaviorStats().perceptionChecks == (suspended ? 0u : 1u),
                "Exactly 150 m preserves the previous suspension state");
    }
    auto boundary = crowd();
    boundary.setPerceptionStaggering(true);
    auto& behavior = boundary.behaviors.create(0, clock.seconds());
    behavior.state = ZombieState::chasing;
    behavior.target = {0, 0, 50};
    behavior.perceptionSuspended = true;
    behavior.perception.initialized = true;
    behavior.perception.nextSeconds = 10000;
    boundary.chase(.001f, {0, 0, 100}, clear);
    require(!boundary.behaviors.find(0)->perceptionSuspended && boundary.behaviorStats().perceptionChecks == 1 &&
            boundary.behaviors.find(0)->target == glm::vec3(0, 0, 100),
            "Exactly 100 m resumes sight immediately");
}

void movingPursuerResumesAndIdleBandRetainsTimeout() {
    auto& clock = viewer::SimulationClock::instance();
    clock.reset();
    const viewer::CollisionWorld clear(viewer::Model{});
    auto moving = crowd();
    moving.setPerceptionStaggering(true);
    moving.chase(.01f, {0, 0, 50}, clear);
    moving.chase(.01f, {0, 0, 300}, clear);
    moving.chase(7, {0, 0, 120}, clear);
    require(moving.behaviors.find(0)->perceptionSuspended && moving.behaviorStats().perceptionChecks == 0 &&
            moving.instances[0].restRoot[3].z > 20,
            "A suspended pursuer moves back within 100 m of a stationary player using its old target");
    moving.chase(.01f, {0, 0, 120}, clear);
    require(!moving.behaviors.find(0)->perceptionSuspended && moving.behaviorStats().perceptionChecks == 1 &&
            moving.behaviors.find(0)->target == glm::vec3(0, 0, 120),
            "Pursuer movement itself can resume perception on the first update inside 100 m");

    clock.reset();
    auto idle = crowd();
    idle.setPerceptionStaggering(true);
    idle.chase(.01f, {0, 0, 20}, clear);
    idle.chase(.01f, {0, 0, 300}, clear);
    clock.advance(2);
    idle.chase(10, {0, 0, 130}, clear);
    require(state(idle) == ZombieState::idle && idle.behaviors.find(0)->perceptionSuspended &&
            !idle.instances[0].walking && idle.instances[0].restRoot[3].z == 20,
            "Arrival inside the 100-150 m band keeps a suspended idle entry with its normal timeout");
    clock.advance(9.9);
    idle.chase(.01f, {0, 0, 130}, clear);
    require(idle.behaviors.contains(0) && idle.behaviorStats().perceptionChecks == 0,
            "Idle hysteresis-band entries neither observe nor expire before ten seconds");
    clock.advance(.1);
    idle.chase(.01f, {0, 0, 130}, clear);
    require(!idle.behaviors.contains(0), "Normal ten-second idle expiration also applies to suspended band entries");
}

void distantIdleEvictionUsesThreeDimensionalDistance() {
    auto& clock = viewer::SimulationClock::instance();
    clock.reset();
    const viewer::CollisionWorld clear(viewer::Model{});
    auto layer = crowd({{0, 0, 0}, {10, 0, 0}, {20, 0, 0}});
    layer.chase(.01f, {0, 0, -20}, clear);
    require(layer.behaviors.entries().size() == 3, "Fixture starts with three idle entries");
    layer.chase(.01f, {0, 0, -150}, clear);
    require(layer.behaviors.entries().size() == 1 && layer.behaviors.contains(0) &&
            !layer.behaviors.contains(1) && !layer.behaviors.contains(2),
            "Idle entries beyond 150 m are evicted, including swapped-in entries; the boundary remains active");
    layer.chase(.01f, {0, 151, 0}, clear);
    require(layer.behaviors.entries().empty() && layer.behaviorStats().perceptionChecks == 0,
            "Vertical separation counts and distant idle entries cast no sight checks before eviction");
    for (const auto& instance : layer.instances)
        require(!instance.walking && !instance.ragdoll && !instance.removed,
                "Idle eviction doesn't move, kill or remove any living instance");
    clock.advance(.01);
    layer.chase(.01f, {0, 0, -100}, clear);
    require(layer.behaviors.entries().size() == 1 && layer.behaviors.contains(0) &&
            layer.behaviorStats().perceptionChecks == 1 && layer.behaviors.find(0)->createdSeconds == clock.seconds(),
            "An evicted idle entry reactivates at the inclusive 100 m boundary");
}

void distantLookingFinishesBeforeEviction() {
    auto& clock = viewer::SimulationClock::instance();
    clock.reset();
    const viewer::CollisionWorld clear(viewer::Model{});
    auto layer = crowd();
    layer.chase(.01f, {0, 0, -20}, clear);
    layer.makeNoise({10, 0, 0}, viewer::walkingFootstepDecibels);
    require(state(layer) == ZombieState::looking, "Fixture is turning toward an audible noise");
    layer.chase(.1f, {0, 0, 300}, clear);
    require(state(layer) == ZombieState::looking && layer.behaviors.find(0)->perceptionSuspended &&
            layer.behaviorStats().perceptionChecks == 0 && !layer.instances[0].walking,
            "A distant looking entry finishes its turn without further sight checks");
    layer.chase(1, {0, 0, 300}, clear);
    require(!layer.behaviors.contains(0) && !layer.instances[0].walking &&
            std::abs(layer.instances[0].restRoot[2].x - 1) < 1e-4f,
            "Completion of a distant looking turn returns to idle and evicts in the same step");
}

void indexedMovementAndImmediateHearing() {
    auto& clock = viewer::SimulationClock::instance();
    clock.reset();
    const viewer::CollisionWorld clear(viewer::Model{});
    auto layer = crowd({{19.9f, 0, 0}});
    layer.setPerceptionStaggering(true);
    layer.chase(1, {40, 0, 40}, clear);
    const auto feet = glm::vec3(layer.instances[0].restRoot[3]);
    require(feet.x > 20, "The moving fixture crosses a spatial cell boundary");
    layer.behaviors.find(0)->state = ZombieState::idle;
    layer.makeNoise(feet + glm::vec3(9.9f, 0, 0), viewer::walkingFootstepDecibels);
    require(state(layer) == ZombieState::looking, "Hearing immediately uses the updated post-movement grid cell");
    // Repositioning a public instance explicitly rebuilds the index.
    layer.instances[0].restRoot[3] = glm::vec4(-100, 0, -100, 1);
    layer.behaviors.find(0)->state = ZombieState::idle;
    layer.rebuildSpatialIndex();
    layer.makeNoise({-100, 0, -90}, viewer::walkingFootstepDecibels);
    require(state(layer) == ZombieState::looking, "External placement edits can rebuild spatial membership");

    auto listener = crowd();
    listener.setPerceptionStaggering(true);
    listener.chase(0.01f, {0, 0, -20}, clear);
    listener.makeNoise({10, 0, 0}, viewer::walkingFootstepDecibels);
    require(state(listener) == ZombieState::looking, "Hearing is event-driven rather than perception-rate limited");
    clock.advance(0.01);
    listener.chase(0.01f, {0, 0, -20}, clear);
    require(listener.behaviorStats().perceptionChecks == 0 && listener.instances[0].restRoot[2].x > 0,
            "Looking turns on every simulation step between sight observations");
}

void largePopulationUsesLocalCandidates() {
    auto& clock = viewer::SimulationClock::instance();
    clock.reset();
    const viewer::CollisionWorld clear(viewer::Model{});
    viewer::ZombieLayer layer; // Production default: staggered sight.
    layer.assets.emplace_back();
    for (int z = -50; z < 50; ++z)
        for (int x = -50; x < 50; ++x) {
            viewer::ZombieInstance instance;
            instance.restRoot = glm::translate(glm::mat4(1), {x * 20.0f + 0.25f, 0, z * 20.0f - 0.25f});
            instance.transform = instance.restTransform = instance.restRoot;
            layer.instances.push_back(instance);
        }
    layer.chase(0.01f, {}, clear);
    const auto active = layer.behaviors.entries().size();
    require(active > 50 && active < 100 && layer.behaviorStats().activationCandidates < 200,
            "A 10,000-zombie population activates only local exact-radius candidates");
    std::size_t checks = 0;
    for (int step = 0; step < 60; ++step) {
        clock.advance(1.0 / 60.0);
        layer.chase(1.0f / 60.0f, {}, clear);
        checks += layer.behaviorStats().perceptionChecks;
        require(layer.behaviorStats().activationCandidates < 200, "Movement keeps subsequent activation queries local");
    }
    require(checks < active * 4, "Scheduled perception avoids every-frame checks across the nearby active pool");
    layer.makeNoise({}, viewer::walkingFootstepDecibels);
    require(layer.behaviorStats().hearingCandidates < 20, "Walking noise queries local cells rather than the entire behavior pool");
}

void lookingPerceptionAndShortestTurn() {
    viewer::SimulationClock::instance().reset();
    const viewer::CollisionWorld clear(viewer::Model{});
    const viewer::CollisionWorld partial(wall(1));
    for (const auto* world : {&clear, &partial}) {
        auto layer = crowd();
        layer.chase(0.1f, {0, 0, -20}, clear);
        layer.makeNoise({10, 0, 0}, viewer::walkingFootstepDecibels);
        layer.chase(0.1f, {0, 0, 10}, *world);
        require(state(layer) == (world == &clear ? ZombieState::chasing : ZombieState::investigating),
                "Visibility interrupts an unfinished look: three rays chase; partial sight investigates");
        require(layer.instances[0].walking && layer.behaviors.find(0)->target == glm::vec3(0, 0, 10),
                "A seen player becomes the movement target during looking");
    }

    auto once = crowd(), split = crowd();
    for (auto* layer : {&once, &split}) {
        auto& instance = layer->instances[0];
        instance.transform = instance.restTransform = instance.restRoot =
            glm::rotate(glm::mat4(1), glm::radians(170.0f), {0, 1, 0});
        layer->chase(0.1f, {0, 0, 200}, clear); // Not nearby; add the idle object explicitly.
        layer->behaviors.create(0, 0);
        const float angle = glm::radians(-170.0f);
        layer->makeNoise({std::sin(angle) * 10, 0, std::cos(angle) * 10}, 50);
    }
    once.chase(0.1f, {0, 0, 200}, clear);
    for (int i = 0; i < 10; ++i) split.chase(0.01f, {0, 0, 200}, clear);
    const auto forward = glm::vec3(once.instances[0].restRoot[2]);
    require(std::abs(glm::degrees(std::atan2(forward.x, forward.z)) - 179.0f) < 1e-3f,
            "Yaw wrapping chooses the short arc across 180 degrees");
    for (int column = 0; column < 4; ++column)
        require(glm::length(once.instances[0].transform[column] - split.instances[0].transform[column]) < 2e-4f,
                "Looking rotation is independent of frame rate");
}
void perceptionDisabled() {
    auto& clock = viewer::SimulationClock::instance();
    clock.reset();
    const viewer::CollisionWorld world(viewer::Model{});
    auto layer = crowd({{0, 0, 0}, {2, 0, 0}});
    layer.setPerceptionEnabled(false);
    for (int step = 0; step < 20; ++step) {
        clock.advance(.1);
        layer.chase(.1f, {0, 0, 10}, world);
        layer.makeNoise({-4, 0, 2}, 100);
        layer.updateBehaviorForPlayers(.1f, {{{0, 0, 10}}, {{0, 0, -10}}}, world, clock.seconds());
        const auto& stats = layer.behaviorStats();
        require(layer.behaviors.entries().empty() && stats.activationCandidates == 0 && stats.perceptionChecks == 0 &&
                stats.sightRays == 0 && stats.hearingCandidates == 0 && stats.attackChecks == 0,
                "Disabled perception must bypass activation, sight, hearing and attack work");
    }
    for (const auto& instance : layer.instances)
        require(!instance.walking && instance.restRoot[3].z == 0 && instance.heardNoiseRevision == 0 && instance.chaseRevision == 0,
                "Disabled perception must leave ordinary zombies idle without reaction notifications");
    require(layer.shoot({0, .9f, 3}, {0, 0, -1}) && layer.damage().injury(0)->health == 9,
            "Disabling perception must not disable shooting or damage");
    layer.setPerceptionEnabled(true);
    layer.chase(.1f, {0, 0, 10}, world);
    require(state(layer) == ZombieState::chasing && layer.instances[0].walking,
            "Reenabling perception must allow fresh nearby activation");
    layer.setPerceptionEnabled(false);
    require(layer.behaviors.entries().empty() && !layer.instances[0].walking && layer.instances[0].locomotionVelocity == glm::vec3(0),
            "Disabling an active pool must discard its behaviors and stop pursuit");
}
}

int main() {
    try {
        clockAndPool();
        perceptionDisabled();
        fovAndActivation();
        perceptionAndStates();
        targetMemoryAndEveryIdleTimeout();
        removalAndInvalidInputs();
        hearingThresholdAndEligibility();
        lookingTurnAndIdleTimer();
        lookingPerceptionAndShortestTurn();
        staggeredObservationsAndContinuousMovement();
        distantPursuitRetainsTargetAndEvictsOnArrival();
        perceptionRangeHysteresisAndImmediateResume();
        movingPursuerResumesAndIdleBandRetainsTimeout();
        distantIdleEvictionUsesThreeDimensionalDistance();
        distantLookingFinishesBeforeEviction();
        indexedMovementAndImmediateHearing();
        largePopulationUsesLocalCandidates();
        std::cout << "Zombie behavior tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Zombie behavior test failed: " << error.what() << '\n';
        return 1;
    }
}
