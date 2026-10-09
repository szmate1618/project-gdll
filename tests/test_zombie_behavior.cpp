#include "simulation_clock.hpp"
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
    layer.chase(5, {0, 0, 10}, clear);
    require(state(layer) == ZombieState::attacking && !layer.instances[0].walking,
            "Attack range stops locomotion and plays idle even with a large step");
    require(std::abs(layer.instances[0].restRoot[3].z - 8.5f) < 1e-4f, "Stop 1.5 m from the player");
    layer.chase(0.1f, {0, 0, 20}, clear);
    require(state(layer) == ZombieState::chasing && layer.instances[0].walking, "Resume pursuit when player leaves attack range");
}

void targetMemoryAndEveryIdleTimeout() {
    auto& clock = viewer::SimulationClock::instance();
    clock.reset();
    const viewer::CollisionWorld clear(viewer::Model{});
    auto layer = crowd();
    layer.chase(0.1f, {0, 0, 10}, clear);
    clock.advance(20);
    layer.chase(4, {0, 0, -200}, clear);
    require(state(layer) == ZombieState::idle && layer.instances[0].restRoot[3].z == 10,
            "Lost sight follows last seen feet and returns to idle on arrival");
    require(layer.behaviors.find(0)->idleSinceSeconds == 20 && !layer.instances[0].walking,
            "Return to idle starts a fresh timer and uses idle animation");
    clock.advance(9);
    layer.chase(0.1f, {0, 0, -200}, clear);
    require(layer.behaviors.contains(0), "Object age does not expire a newly idle state");
    layer.chase(0.1f, {0, 0, 20}, clear);
    require(state(layer) == ZombieState::chasing, "Reacquisition cancels the idle countdown");
    clock.advance(2);
    layer.chase(4, {0, 0, -200}, clear);
    require(state(layer) == ZombieState::idle && layer.behaviors.find(0)->idleSinceSeconds == 31,
            "Every subsequent return to idle resets the countdown");
    clock.advance(9.9);
    layer.chase(0.1f, {0, 0, -200}, clear);
    require(layer.behaviors.contains(0), "Second idle timer survives before its deadline");
    clock.advance(0.1);
    layer.chase(0.1f, {0, 0, -200}, clear);
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
}

int main() {
    try {
        clockAndPool();
        fovAndActivation();
        perceptionAndStates();
        targetMemoryAndEveryIdleTimeout();
        removalAndInvalidInputs();
        std::cout << "Zombie behavior tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Zombie behavior test failed: " << error.what() << '\n';
        return 1;
    }
}
