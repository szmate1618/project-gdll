#pragma once

#include "collision_world.hpp"
#include "zombie_navmesh.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace viewer::steering_benchmark {
struct Request {
    glm::vec3 start{};
    navmesh_path::Point goal{};
    std::vector<navmesh_path::Point> path;
};
struct Scenario {
    std::string name;
    std::string purpose;
    Model terrain;
    std::vector<TrunkCollider> trunks;
    navmesh_path::Mesh mesh;
    std::vector<Request> requests;
    double pathfindingMilliseconds = 0;
};
// All scene geometry is in runtime X east/Y up/Z south meters. These fixtures
// isolate trunk steering: agents have independent positions and do not collide.
std::vector<Scenario> makeScenarios(std::size_t population, std::uint32_t seed);
}
