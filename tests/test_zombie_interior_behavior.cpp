#include "zombie_layer.hpp"
#include "day_night_clock.hpp"
#include "simulation_clock.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using viewer::ZombieState;
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }

viewer::ZombieLayer spawn(ZombieState state, double wake = 0) {
    viewer::ZombieLayer layer;
    layer.assets.emplace_back();
    layer.instances.resize(1);
    layer.instances[0].removed = true;
    layer.rebuildSpatialIndex();
    layer.activateInteriorZombie(0, glm::mat4(1), state, wake);
    layer.setPerceptionStaggering(false);
    require(layer.behaviors.contains(0), "Every activated interior actor must immediately own a behavior entry");
    return layer;
}
ZombieState state(const viewer::ZombieLayer& layer) {
    const auto* behavior = layer.behaviors.find(0);
    require(behavior != nullptr, "Interior actor lost its behavior entry");
    return behavior->state;
}
viewer::Model partialWall() {
    viewer::Model model;
    viewer::Primitive wall;
    for (const auto p : {glm::vec3(-20, 0, 5), glm::vec3(20, 0, 5),
                         glm::vec3(20, 1, 5), glm::vec3(-20, 1, 5)}) {
        viewer::Vertex vertex; vertex.position = p; wall.vertices.push_back(vertex);
    }
    wall.indices = {0, 1, 2, 0, 2, 3};
    model.primitives.push_back(std::move(wall));
    model.draws.push_back({0, glm::mat4(1), "wall"});
    return model;
}

void hidingAndOrdinary() {
    auto& clock = viewer::SimulationClock::instance(); clock.reset();
    const viewer::CollisionWorld clear(viewer::Model{}), partial(partialWall());
    auto hidden = spawn(ZombieState::hiding);
    hidden.makeNoise({10, 0, 0}, 100);
    require(state(hidden) == ZombieState::hiding && hidden.instances[0].heardNoiseRevision == 1,
            "A hiding actor must hear noises without entering investigation");
    for (int step = 0; step < 130; ++step) {
        clock.advance(.1);
        hidden.chase(.1f, {-10, 0, -10}, clear);
        require(state(hidden) == ZombieState::hiding && glm::vec3(hidden.instances[0].restRoot[3]) == glm::vec3(0),
                "Noise turning must retain the hiding position and survive ordinary idle expiration");
    }
    require(hidden.instances[0].restRoot[2].x > .999f && !hidden.instances[0].walking,
            "An ambusher must finish turning toward a heard noise while standing still");
    hidden.chase(.1f, {10, 0, 0}, clear);
    require(state(hidden) == ZombieState::chasing && hidden.instances[0].walking && hidden.instances[0].chaseRevision == 1,
            "A full player sighting must release the ambush and start ordinary pursuit");
    hidden.setInteriorSleeping(0, true); hidden.setInteriorSleeping(0, false);
    require(state(hidden) == ZombieState::idle, "A discovered ambusher must remain ordinary after streaming sleep");

    clock.reset();
    auto ambusher = spawn(ZombieState::hiding);
    auto ordinary = spawn(ZombieState::idle);
    ambusher.chase(.1f, {0, 0, 10}, partial);
    ordinary.chase(.1f, {0, 0, 10}, partial);
    require(ambusher.behaviors.find(0)->sight == viewer::ZombieSight::partial && state(ambusher) == ZombieState::hiding &&
            glm::vec3(ambusher.instances[0].restRoot[3]) == glm::vec3(0),
            "A partial sighting must never make an undiscovered ambusher investigate");
    require(state(ordinary) == ZombieState::investigating && ordinary.instances[0].walking,
            "An ordinary room-center spawn must retain normal partial-sighting investigation");
    ambusher.chase(.1f, {0, 0, 10}, clear);
    require(state(ambusher) == ZombieState::chasing, "Clear sight must trigger an ambusher normally");

    auto listening = spawn(ZombieState::idle);
    listening.makeNoise({10, 0, 0}, 100);
    require(state(listening) == ZombieState::looking,
            "A new ordinary spawn must hear noise before its first perception update");
    auto untriggered = spawn(ZombieState::hiding);
    untriggered.setInteriorSleeping(0, true); untriggered.setInteriorSleeping(0, false);
    require(state(untriggered) == ZombieState::hiding, "Streaming must retain an undiscovered hiding policy");
    untriggered.setPerceptionEnabled(false); untriggered.setPerceptionEnabled(true);
    untriggered.chase(.01f, {0, 0, -10}, clear);
    require(state(untriggered) == ZombieState::hiding, "AI toggling must not turn an undiscovered ambusher ordinary");
}

void midnightActivation() {
    auto& clock = viewer::SimulationClock::instance(); clock.reset();
    const viewer::CollisionWorld clear(viewer::Model{});
    const viewer::DayNightCycle day{23, 1200};
    auto actor = spawn(ZombieState::midnight, day.nextMidnightAtOrAfter(0));
    actor.makeNoise({10, 0, 0}, 100);
    clock.advance(49);
    actor.chase(.1f, {0, 0, .5f}, clear);
    require(state(actor) == ZombieState::midnight && actor.instances[0].heardNoiseRevision == 0 &&
            actor.behaviorStats().sightRays == 0 && actor.behaviorStats().attackChecks == 0 &&
            actor.attackHits().empty() && glm::vec3(actor.instances[0].restRoot[3]) == glm::vec3(0),
            "A midnight actor must ignore noise, sight and nearby attack opportunities before its deadline");
    clock.advance(2); // Cross midnight without sampling exactly 00:00.
    actor.chase(.1f, {0, 0, 10}, clear);
    require(state(actor) == ZombieState::chasing && actor.instances[0].walking,
            "Crossing midnight between steps must activate ordinary pursuit immediately");
    actor.setInteriorSleeping(0, true); actor.setInteriorSleeping(0, false);
    require(state(actor) == ZombieState::idle, "Midnight activation must be permanent after streaming sleep");
    actor.makeNoise({10, 0, 0}, 100);
    require((state(actor) == ZombieState::looking || state(actor) == ZombieState::investigating) &&
            actor.instances[0].heardNoiseRevision == 1, "An awakened midnight actor must allow normal noise turning or investigation");

    clock.reset();
    auto sleeper = spawn(ZombieState::midnight, 50);
    sleeper.setInteriorSleeping(0, true);
    clock.advance(20); sleeper.setInteriorSleeping(0, false);
    require(state(sleeper) == ZombieState::midnight, "Reentry before midnight must retain the original wake deadline");
    sleeper.setInteriorSleeping(0, true);
    clock.advance(2500); sleeper.setInteriorSleeping(0, false);
    require(state(sleeper) == ZombieState::idle, "Sleeping through midnight or whole days must not miss activation");

    clock.reset();
    auto toggled = spawn(ZombieState::midnight, 50);
    toggled.setPerceptionEnabled(false); clock.advance(49); toggled.setPerceptionEnabled(true);
    toggled.chase(.01f, {0, 0, 10}, clear);
    require(state(toggled) == ZombieState::midnight, "AI toggling must not bypass a pending midnight activation");
    clock.advance(2); toggled.chase(.01f, {0, 0, 10}, clear);
    require(state(toggled) == ZombieState::chasing, "A recreated midnight entry must retain its deadline");

    clock.reset();
    auto frozen = spawn(ZombieState::midnight, viewer::DayNightCycle{12, 0}.nextMidnightAtOrAfter(0));
    clock.advance(1000000); frozen.chase(.01f, {0, 0, .5f}, clear);
    require(state(frozen) == ZombieState::midnight, "A clock frozen at noon must not activate upstairs actors");
    auto alreadyMidnight = spawn(ZombieState::midnight, viewer::DayNightCycle{0, 0}.nextMidnightAtOrAfter(clock.seconds()));
    require(state(alreadyMidnight) == ZombieState::idle, "An actor spawned at midnight must already be awake");
}

void damageWakesMidnight() {
    auto& clock = viewer::SimulationClock::instance(); clock.reset();
    const viewer::CollisionWorld clear(viewer::Model{});
    auto actor = spawn(ZombieState::midnight, std::numeric_limits<double>::infinity());
    require(actor.shoot({3, .9f, 0}, {-1, 0, 0}) && state(actor) == ZombieState::looking &&
            actor.behaviors.find(0)->lookDirection == glm::vec2(1, 0) && !actor.instances[0].ragdoll,
            "A surviving hit must wake a midnight actor into ordinary looking toward the attacker");
    for (int step = 0; step < 11; ++step) {
        clock.advance(.1); actor.chase(.1f, {-10, 0, -10}, clear);
    }
    require(state(actor) == ZombieState::idle && actor.instances[0].restRoot[2].x > .999f,
            "A damaged midnight actor must return to ordinary idle after turning, not dormancy");
    actor.setInteriorSleeping(0, true); actor.setInteriorSleeping(0, false);
    actor.setPerceptionEnabled(false); actor.setPerceptionEnabled(true);
    actor.chase(.1f, {10, 0, 0}, clear);
    require(state(actor) == ZombieState::chasing,
            "Damage awakening must survive streaming and AI toggles and allow normal player pursuit");

    auto disabled = spawn(ZombieState::midnight, std::numeric_limits<double>::infinity());
    disabled.setPerceptionEnabled(false);
    require(disabled.shoot({3, .9f, 0}, {-1, 0, 0}) && disabled.behaviors.entries().empty(),
            "Damage must awaken the pending policy without executing disabled AI");
    disabled.setPerceptionEnabled(true); disabled.chase(.1f, {0, 0, 10}, clear);
    require(state(disabled) == ZombieState::chasing, "Damage while AI is disabled must still cancel midnight dormancy permanently");
}
}

int main() {
    try {
        hidingAndOrdinary(); midnightActivation(); damageWakesMidnight();
        std::cout << "Interior behavior and midnight activation tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Interior behavior test failed: " << error.what() << '\n';
        return 1;
    }
}
