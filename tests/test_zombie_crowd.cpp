#include "simulation_clock.hpp"
#include "zombie_chase.hpp"
#include "zombie_layer.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <numeric>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool near(glm::vec3 a, glm::vec3 b) { return glm::length(a - b) < 1e-4f; }
float distance(glm::vec3 a, glm::vec3 b) { return glm::length(glm::vec2(a.x - b.x, a.z - b.z)); }

void sweepSlideAndMembership() {
    viewer::ZombieCrowd crowd;
    crowd.reset(2);
    crowd.update(0, {-2, 0, 0});
    crowd.update(1, {0, 0, 0});
    const auto stopped = crowd.constrain(0, {2, 0, 0});
    require(stopped.x < -0.6f && stopped.x > -0.61f && stopped.z == 0,
            "A large step must stop before an idle body, even if the endpoint is clear");
    crowd.update(0, {-1, 0, -0.45f});
    const auto slid = crowd.constrain(0, {1, 0, -0.45f});
    require(slid.x > 0 && slid.z < -0.6f && distance(slid, {}) > 0.6f,
            "An oblique contact must slide past the torso");
    require(distance(slid, {-1, 0, -0.45f}) <= 2.0001f, "Sliding must not add movement speed");
    crowd.update(0, {-2, 0, 0});
    crowd.update(1, {0, 2, 0});
    require(near(crowd.constrain(0, {2, 0, 0}), {2, 0, 0}), "Separate floors must not block each other");
    crowd.update(1, {0, 0, 0});
    crowd.erase(1);
    require(near(crowd.constrain(0, {2, 0, 0}), {2, 0, 0}), "Removed bodies must leave no stale occupancy");
    crowd.update(1, {10, 0, 0});
    crowd.update(1, {-1, 0, 0});
    require(crowd.constrain(0, {2, 0, 0}).x < -1.6f, "Crossing negative grid cells must repair membership");
    crowd.reset(2);
    crowd.update(0, {0, 0, 0});
    crowd.update(1, {0.2f, 0, 0});
    require(near(crowd.constrain(0, {-1, 0, 0}), {-1, 0, 0}), "Existing overlap must allow outward escape");
}

void opposingMoversAndDenseBudget() {
    viewer::ZombieCrowd crowd;
    crowd.reset(2);
    crowd.update(0, {-1, 0, 0});
    crowd.update(1, {1, 0, 0});
    const auto first = crowd.constrain(0, {1, 0, 0});
    crowd.update(0, first);
    const auto second = crowd.constrain(1, {-1, 0, 0});
    require(distance(first, second) >= 0.6f && first.x < second.x,
            "Fast opposing movers must neither overlap nor exchange sides");

    crowd.reset(10001);
    crowd.update(0, {-2, 0, 0});
    for (std::size_t id = 1; id <= 10000; ++id) crowd.update(id, {});
    require(near(crowd.constrain(0, {2, 0, 0}), {-2, 0, 0}),
            "Overloaded cells must stop conservatively rather than ignore neighbors");
    require(crowd.stats().neighborChecks == viewer::ZombieCrowd::neighborBudget && crowd.stats().budgetStops == 1,
            "Even 10,000 coincident obstacles must use bounded neighbor work");
}

viewer::ZombieLayer layerAt(std::initializer_list<glm::vec3> positions) {
    viewer::ZombieLayer layer;
    layer.assets.emplace_back();
    for (const auto feet : positions) {
        viewer::ZombieInstance instance;
        instance.restRoot = glm::translate(glm::mat4(1), feet);
        instance.transform = instance.restTransform = instance.restRoot;
        layer.instances.push_back(instance);
    }
    layer.rebuildSpatialIndex();
    return layer;
}

void behaviorIntegration() {
    viewer::SimulationClock::instance().reset();
    const viewer::CollisionWorld world(viewer::Model{});
    // The blocker is outside activation range and has no behavior object.
    auto layer = layerAt({{0, 0, 0}, {0, 0, 1}});
    auto& behavior = layer.behaviors.create(0, 0);
    behavior.state = viewer::ZombieState::chasing;
    behavior.target = {0, 0, 10};
    for (int step = 0; step < 20; ++step) layer.chase(0.1f, {0, 0, 300}, world);
    const auto feet = glm::vec3(layer.instances[0].restRoot[3]);
    require(feet.z > 0.39f && feet.z < 0.4f && !layer.instances[0].walking &&
            layer.instances[0].walkSeconds < 0.14 && !layer.behaviors.contains(1),
            "Inactive bodies block pursuit and blocked movement stops the animation clock");
    require(layer.behaviors.find(0)->state == viewer::ZombieState::chasing &&
            layer.behaviors.find(0)->target == glm::vec3(0, 0, 10),
            "Being blocked must retain pursuit and the last target");
    // A shot removes its body from the crowd index immediately.
    require(layer.shoot({-2, 0.9f, 1}, {1, 0, 0}), "The inactive blocker must be shootable");
    require(layer.instances[1].ragdoll, "Shoot the blocker rather than its pursuer");
    layer.chase(0.1f, {0, 0, 300}, world);
    require(layer.instances[0].restRoot[3].z > 0.69f && layer.instances[0].walking,
            "Pursuit must resume immediately when its blocker becomes a ragdoll");

    // Pool insertion/removal history must not affect movement priority.
    auto a = layerAt({{-1, 0, 0}, {1, 0, 0}});
    auto b = layerAt({{-1, 0, 0}, {1, 0, 0}});
    for (const auto id : {0u, 1u}) a.behaviors.create(id, 0);
    for (const auto id : {1u, 0u}) b.behaviors.create(id, 0);
    for (auto* layerPtr : {&a, &b}) {
        for (const auto id : {0u, 1u}) {
            auto* entry = layerPtr->behaviors.find(id);
            entry->state = viewer::ZombieState::chasing;
            entry->target = id == 0 ? glm::vec3(1, 0, 0) : glm::vec3(-1, 0, 0);
        }
        layerPtr->chase(1, {0, 0, 300}, world);
    }
    for (std::size_t id = 0; id < 2; ++id)
        require(a.instances[id].transform == b.instances[id].transform,
                "Stable IDs must determine collision priority regardless of pool order");
}

void slidingTerrainAndNormalization() {
    viewer::Model terrain;
    viewer::Primitive primitive;
    for (const auto p : {glm::vec3(-5, -1, -5), glm::vec3(-5, -1, 5),
                         glm::vec3(5, 1, 5), glm::vec3(5, 1, -5)}) {
        viewer::Vertex vertex;
        vertex.position = p;
        primitive.vertices.push_back(vertex);
    }
    primitive.indices = {0, 1, 2, 0, 2, 3};
    terrain.primitives.push_back(std::move(primitive));
    terrain.draws.push_back({0, glm::mat4(1), "terrain"});
    const viewer::CollisionWorld world(terrain);
    auto layer = layerAt({{-1, -.2f, -.45f}, {0, 0, 0}});
    auto& instance = layer.instances[0];
    const auto normalization = glm::scale(glm::translate(glm::mat4(1), {.1f, -.2f, .3f}), glm::vec3(1.4f));
    instance.chaseNormalization = normalization;
    viewer::ZombieCrowd crowd;
    crowd.reset(2);
    crowd.update(0, glm::vec3(instance.restRoot[3]));
    crowd.update(1, {});
    const bool arrived = viewer::moveZombieToward(instance, layer.assets[0], {1, .2f, -.45f},
                                                  2, 1, world, 0, &crowd, 0);
    const auto feet = glm::vec3(instance.restRoot[3]);
    require(!arrived && feet.z < -.6f && std::abs(feet.y - feet.x * .2f) < 1e-4f,
            "Sliding must resample the terrain at accepted feet and must not report false arrival");
    const auto retained = glm::inverse(instance.restRoot) * instance.transform;
    for (int column = 0; column < 4; ++column)
        require(glm::length(retained[column] - normalization[column]) < 1e-4f,
                "Collision displacement must preserve the character's asset normalization");
}

void packedCrowd() {
    constexpr std::size_t count = 10000;
    std::vector<glm::vec3> positions;
    for (int z = 0; z < 100; ++z)
        for (int x = 0; x < 100; ++x) positions.emplace_back((x - 50) * .65f, 0, (z - 50) * .65f);
    viewer::ZombieCrowd a, b;
    a.reset(count);
    b.reset(count);
    for (std::size_t id = 0; id < count; ++id) a.update(id, positions[id]);
    // Different bucket insertion history exercises contact tie-breaking too.
    for (std::size_t id = count; id-- > 0;) b.update(id, positions[id]);
    std::size_t checks = 0;
    const auto start = std::chrono::steady_clock::now();
    for (int step = 0; step < 120; ++step) {
        a.beginStep();
        b.beginStep();
        for (std::size_t id = 0; id < count; ++id) {
            auto desired = positions[id];
            const float length = glm::length(desired);
            if (length > .15f) desired -= desired / length * .15f;
            const auto accepted = a.constrain(id, desired);
            require(accepted == b.constrain(id, desired), "Crowd replay must produce identical positions");
            require(distance(positions[id], accepted) <= .1501f, "Crowd constraints cannot accelerate movement");
            positions[id] = accepted;
            a.update(id, accepted);
            b.update(id, accepted);
        }
        require(a.stats().neighborChecks <= count * viewer::ZombieCrowd::neighborBudget,
                "Packed crowd work must be bounded per mover");
        checks += a.stats().neighborChecks;
    }
    const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    // Independent sweep-and-prune assertion, not the solver's grid or sweep formula.
    std::vector<std::size_t> order(count);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](auto x, auto y) { return positions[x].x < positions[y].x; });
    for (std::size_t i = 0; i < count; ++i)
        for (std::size_t j = i + 1; j < count && positions[order[j]].x - positions[order[i]].x < .6f; ++j)
            require(distance(positions[order[i]], positions[order[j]]) >= .5999f,
                    "A converging 10,000-body crowd must preserve torso separation");
    std::cout << "10,000 packed zombies, 120 steps, two replays: " << seconds << " s; "
              << double(checks) / (120 * count) << " neighbor checks per mover\n";
}
}

int main() {
    try {
        sweepSlideAndMembership();
        opposingMoversAndDenseBudget();
        behaviorIntegration();
        slidingTerrainAndNormalization();
        packedCrowd();
        std::cout << "Zombie crowd tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Zombie crowd test failed: " << error.what() << '\n';
        return 1;
    }
}
