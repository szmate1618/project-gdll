#include "navmesh_fixture.hpp"
#include "zombie_path_follow.hpp"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>

namespace {
std::atomic<std::size_t> allocations{0};
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool close(float a, float b) { return std::abs(a - b) < 1e-5f; }

void following() {
    using namespace viewer::steering;
    using viewer::navmesh_path::Point;
    const auto mesh = test_navmesh::convexSquares(3, 3, {{1, 1}});
    const auto open = test_navmesh::convexSquares(4, 4);
    const std::array<Point, 4> bend{{{.5, 1.5}, {1, 1}, {2, 1}, {2.5, 1.5}}};
    const std::array<Point, 3> rightAngle{{{0, 0}, {1, 0}, {1, 1}}};
    const std::array<Point, 3> duplicates{{{0, 0}, {0, 0}, {1, 0}}};
    const std::array<Point, 2> straight{{{0, 0}, {2, 0}}};
    const auto before = allocations.load();
    FollowState state;
    auto velocity = followPath(rightAngle.data(), rightAngle.size(), {0, 0}, 10, 1, state);
    require(close(velocity.x, 1) && close(velocity.y, 0) && !state.arrived && state.segment == 0,
            "Fast path following must stop its desired step at a corner");
    velocity = followPath(rightAngle.data(), rightAngle.size(), {1, 0}, 10, 1, state);
    require(close(velocity.x, 0) && close(velocity.y, 1) && state.segment == 1 && !state.arrived,
            "Reached corner must advance without overshooting the final endpoint");
    velocity = followPath(rightAngle.data(), rightAngle.size(), {1, 1}, 10, 1, state);
    require(velocity == glm::vec2(0) && state.arrived, "Only the actual endpoint position may signal arrival");
    velocity = followPath(rightAngle.data(), rightAngle.size(), {1, .7f}, 1, .1f, state);
    require(velocity.y > 0 && !state.arrived, "Displacement away from an arrived endpoint must resume following");

    state = {};
    const glm::vec2 besideCorner(.95f, 1.05f);
    require(!mesh.segmentClear({besideCorner.x, besideCorner.y}, bend[2]),
            "Corner fixture must forbid shortcut across its missing square");
    velocity = followPath(bend.data(), bend.size(), besideCorner, 10, 1, state, &mesh);
    const auto next = besideCorner + velocity;
    require(state.segment == 0 && close(next.x, 1) && close(next.y, 1) &&
            mesh.segmentClear({besideCorner.x, besideCorner.y}, {next.x, next.y}),
            "Proximity to a waypoint must not cut a blocked navmesh corner");
    velocity = followPath(bend.data(), bend.size(), {1, 1}, 10, 1, state, &mesh);
    require(state.segment == 1 && close(velocity.x, 1) && close(velocity.y, 0),
            "Valid navmesh corner must advance along its boundary");

    state = {};
    velocity = followPath(rightAngle.data(), rightAngle.size(), {1.1f, .05f}, 1, .1f, state, &open);
    require(state.segment == 1 && velocity.y > 0 && velocity.x < 0,
            "Passed segment with a clear shortcut must advance after local avoidance");
    state = {};
    velocity = followPath(duplicates.data(), duplicates.size(), {0, 0}, 1, .1f, state);
    require(state.segment == 1 && close(velocity.x, 1), "Repeated waypoints must not deadlock the follower");
    state = {};
    velocity = followPath(straight.data(), straight.size(), {1.95f, 0}, 1, .1f, state);
    require(state.arrived && velocity == glm::vec2(0), "Final arrival radius must use actual distance");
    state = {999, false};
    velocity = followPath(straight.data(), straight.size(), {0, 0}, 1, .1f, state);
    require(state.segment == 0 && close(velocity.x, 1), "Invalid segment cursor must stay within the borrowed path");
    state = {};
    velocity = followPath(nullptr, 0, {0, 0}, 1, .1f, state);
    require(!state.arrived && velocity == glm::vec2(0), "Missing path must stop without reporting arrival");
    velocity = followPath(straight.data(), 1, {.3f, 0}, 1, 1, state);
    require(!state.arrived && close(velocity.x, -.3f), "Single-point path must retain its exact endpoint");
    const float nan = std::numeric_limits<float>::quiet_NaN();
    require(followPath(straight.data(), straight.size(), {nan, 0}, 1, .1f, state) == glm::vec2(0) &&
            followPath(straight.data(), straight.size(), {0, 0}, 1, 0, state) == glm::vec2(0) &&
            followPath(straight.data(), straight.size(), {0, 0}, -1, .1f, state) == glm::vec2(0),
            "Invalid follower inputs must stop safely");
    require(allocations.load() == before, "Waypoint following must not allocate");
}
}

void* operator new(std::size_t size) {
    ++allocations;
    if (void* pointer = std::malloc(size ? size : 1)) return pointer;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }

int main() try {
    following();
    std::cout << "Allocation-free waypoint following tests passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
