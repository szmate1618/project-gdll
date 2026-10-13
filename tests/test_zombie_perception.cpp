#include "zombie_perception.hpp"
#include "zombie_layer.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void schedules() {
    require(viewer::zombiePerceptionPeriod(100) == 0.1 && viewer::zombiePerceptionPeriod(100.01) == 0.2 &&
            viewer::zombiePerceptionPeriod(900) == 0.2 && viewer::zombiePerceptionPeriod(900.01) == 1,
            "Distance bands include exactly 10 and 30 meters");
    viewer::ZombiePerceptionClock clock;
    require(clock.due(0, 100, 0), "New behavior observes immediately");
    require(!clock.due(0.099, 100, 0) && clock.due(0.1, 100, 0) && !clock.due(0.1, 100, 0),
            "Near perception runs at 10 Hz without repeated same-time observations");
    require(!clock.due(0.199, 100, 0) && clock.due(0.2, 100, 0), "Near cadence remains stable");
    viewer::ZombiePerceptionClock middle;
    require(middle.due(0, 400, 0) && !middle.due(0.199, 400, 0) && middle.due(0.2, 400, 0), "Middle band is 5 Hz");
    viewer::ZombiePerceptionClock far;
    require(far.due(0, 1600, 0) && !far.due(0.999, 1600, 0) && far.due(1, 1600, 0), "Far band is 1 Hz");
    viewer::ZombiePerceptionClock entering;
    require(entering.due(0, 1600, 0) && !entering.due(0.05, 25, 0) && entering.due(0.1, 25, 0),
            "Moving closer shortens a previously distant deadline");
    require(entering.due(5.43, 25, 0) && !entering.due(5.43, 25, 0), "A long frame runs one observation without catch-up bursts");

    std::vector<viewer::ZombiePerceptionClock> crowd(1000);
    for (std::size_t id = 0; id < crowd.size(); ++id) crowd[id].due(0, 1600, id);
    std::size_t total = 0, maximum = 0;
    for (int step = 1; step <= 100; ++step) {
        std::size_t due = 0;
        for (std::size_t id = 0; id < crowd.size(); ++id) due += crowd[id].due(step * 0.01, 1600, id);
        maximum = std::max(maximum, due);
        total += due;
    }
    require(total == 1000 && maximum < 50, "Stable-ID staggering distributes one observation per zombie across the second");
    viewer::ZombiePerceptionClock first, second;
    first.due(0, 400, 57); second.due(0, 400, 57);
    require(first.nextSeconds == second.nextSeconds, "Schedules are deterministic and independent of pool slot order");
}

void sightClassification() {
    const viewer::CollisionWorld world(viewer::Model{});
    viewer::ZombieInstance zombie;
    std::size_t rays = 0;
    require(viewer::sampleZombieSight(zombie, {{0, 0, -10}}, world, rays) == viewer::ZombieSight::none && rays == 0,
            "Outside-FOV observations cast no rays");
    require(viewer::sampleZombieSight(zombie, {{0, 0, 10}}, world, rays) == viewer::ZombieSight::full && rays == 3,
            "Full visibility requires all three clear rays");
    rays = 0;
    require(viewer::sampleZombieSight(zombie, {{0, 0, 10}}, world, rays, true) == viewer::ZombieSight::partial && rays == 1,
            "Every-step chasing can stop at the first clear ray");
    viewer::Model model;
    viewer::Primitive wall;
    for (const auto point : {glm::vec3(-20, 0, 5), glm::vec3(20, 0, 5), glm::vec3(20, 1.5f, 5), glm::vec3(-20, 1.5f, 5)}) {
        viewer::Vertex vertex;
        vertex.position = point;
        wall.vertices.push_back(vertex);
    }
    wall.indices = {0, 1, 2, 0, 2, 3};
    model.primitives.push_back(wall);
    model.draws.push_back({0, glm::mat4(1), "wall"});
    const viewer::CollisionWorld partial(model);
    rays = 0;
    require(viewer::sampleZombieSight(zombie, {{0, 0, 10}}, partial, rays) == viewer::ZombieSight::partial && rays == 2,
            "One clear and one blocked ray classify partial sight without the third query");
    model.primitives[0].vertices[2].position.y = 2;
    model.primitives[0].vertices[3].position.y = 2;
    const viewer::CollisionWorld occluded(model);
    rays = 0;
    require(viewer::sampleZombieSight(zombie, {{0, 0, 10}}, occluded, rays) == viewer::ZombieSight::none && rays == 3,
            "Fully occluded sight checks all three points before rejecting visibility");
}
}

int main() {
    try { schedules(); sightClassification(); std::cout << "Zombie perception tests passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
