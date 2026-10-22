#include "zombie_steering.hpp"
#include "DetourAlloc.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <glm/geometric.hpp>

namespace {
std::size_t allocations = 0, detourAllocations = 0;
bool countAllocations = false;
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void* detourAllocate(std::size_t size, dtAllocHint) {
    if (countAllocations) ++detourAllocations;
    return std::malloc(size ? size : 1);
}
void detourFree(void* memory) { std::free(memory); }

// Independent double-precision closest-point check, used as a swept physical
// disk guard in the isolated-tree fixture. Steering remains a velocity choice.
double trunkDistance(glm::vec2 a, glm::vec2 b, glm::vec2 center) {
    const double dx = double(b.x) - a.x, dz = double(b.y) - a.y;
    const double lengthSquared = dx * dx + dz * dz;
    const double t = lengthSquared > 0 ? std::clamp(
        ((double(center.x) - a.x) * dx + (double(center.y) - a.y) * dz) / lengthSquared,
        0., 1.) : 0.;
    return std::hypot(double(a.x) + t * dx - center.x,
                      double(a.y) + t * dz - center.y);
}

template<class Exception, class Function>
void rejects(Function function, const char* message) {
    bool rejected = false;
    try { function(); }
    catch (const Exception&) { rejected = true; }
    require(rejected, message);
}
}

[[gnu::noinline]] void* operator new(std::size_t size) {
    if (countAllocations) ++allocations;
    if (void* memory = std::malloc(size ? size : 1)) return memory;
    throw std::bad_alloc();
}
[[gnu::noinline]] void* operator new[](std::size_t size) { return ::operator new(size); }
[[gnu::noinline]] void operator delete(void* memory) noexcept { std::free(memory); }
[[gnu::noinline]] void operator delete[](void* memory) noexcept { std::free(memory); }
[[gnu::noinline]] void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
[[gnu::noinline]] void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

int main() try {
    using namespace viewer::steering;
    dtAllocSetCustom(detourAllocate, detourFree);
    const Config config;
    Controller controller(config, 8);
    const Obstacle tree{{0, 0}, .4f};
    require(controller.capacity() == 8 && controller.workspaceBytes() > sizeof(Obstacle) * 8,
            "fixed Detour capacity/workspace was not retained");
    require(controller.steer({-2, 0}, {0, 0}, {2, .3f}, nullptr, 0) == glm::vec2(2, .3f),
            "empty neighborhood changed desired velocity");
    require(controller.steer({-2, 0}, {2, 0}, {0, 0}, &tree, 1) == glm::vec2(0),
            "zero desired velocity acquired movement");

    // An unobstructed direct follower would run through this trunk. The actual
    // adaptive sampler must introduce a useful lateral pass and reach the goal
    // with the same external swept-disk rejection used for every update here.
    constexpr float dt = .05f, speed = 2.f;
    const glm::vec2 goal(3, 0);
    glm::vec2 position(-3, 0), previousVelocity(0);
    Stats stats;
    float maximumLateral = 0;
    int rejectedMoves = 0;
    const double combinedRadius = double(tree.radius) + config.radius + config.skin;
    countAllocations = true;
    for (int step = 0; step < 400 && glm::length(goal - position) > .15f; ++step) {
        const float remaining = glm::length(goal - position);
        const auto desired = (goal - position) / remaining * std::min(speed, remaining / dt);
        const auto velocity = controller.steer(position, previousVelocity, desired, &tree, 1, &stats);
        require(std::isfinite(velocity.x) && std::isfinite(velocity.y) &&
                glm::length(velocity) <= glm::length(desired) + 1e-6f,
                "Detour returned non-finite or excessive velocity");
        const auto proposed = position + velocity * dt;
        const auto accepted = trunkDistance(position, proposed, tree.center) > combinedRadius ?
                              proposed : position;
        if (accepted == position) ++rejectedMoves;
        previousVelocity = (accepted - position) / dt;
        position = accepted;
        maximumLateral = std::max(maximumLateral, std::abs(position.y));
        require(trunkDistance(position, position, tree.center) > combinedRadius,
                "external guard accepted an overlap");
    }
    countAllocations = false;
    require(glm::length(goal - position) <= .15f && maximumLateral > .5f,
            "adaptive avoidance failed the isolated head-on trunk pass");
    require(rejectedMoves == 0 && stats.velocitySamples > 0 && stats.obstacleTests > 0,
            "head-on fixture relied on collision rejection or bypassed Detour");
    require(allocations == 0 && detourAllocations == 0,
            "steering allocated after fixed workspace construction");

    const float nan = std::numeric_limits<float>::quiet_NaN();
    rejects<std::length_error>([&] { controller.steer({}, {}, {1, 0}, &tree, 9); },
                               "capacity overflow silently omitted trunks");
    rejects<std::invalid_argument>([&] { controller.steer({}, {}, {1, 0}, nullptr, 1); },
                                   "nonempty null obstacle input accepted");
    rejects<std::invalid_argument>([&] { controller.steer({nan, 0}, {}, {1, 0}, &tree, 1); },
                                   "non-finite position accepted");
    rejects<std::invalid_argument>([&] { controller.steer({}, {nan, 0}, {1, 0}, &tree, 1); },
                                   "non-finite previous accepted velocity accepted");
    const Obstacle invalid{{0, 0}, -1};
    rejects<std::invalid_argument>([&] { controller.steer({}, {}, {1, 0}, &invalid, 1); },
                                   "invalid trunk radius accepted");
    rejects<std::invalid_argument>([&] { Controller invalidController(Config{}, 0); },
                                   "zero obstacle capacity accepted");
    auto invalidConfig = config;
    invalidConfig.horizon = 0;
    rejects<std::invalid_argument>([&] { Controller invalidController(invalidConfig); },
                                   "invalid prediction horizon accepted");
    std::cout << "Detour head-on trunk avoidance, speed bounds, validation and fixed workspace passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
