#pragma once

#include <cstddef>
#include <vector>

#include <glm/glm.hpp>

namespace viewer {

class CollisionWorld;
struct Model;

// Deterministic, nearest-first crowd placement in the scene's X-east/Y-up/Z-south
// meter frame. Matrices contain a yaw rotation and feet on the rendered ground.
// Keep at least 2 m between centers and a 2 m clearing around the requested
// center. Radius is a hard horizontal limit; an out-of-bounds center falls back
// to the scene center. Throws if the bounded search cannot place the full count.
std::vector<glm::mat4> placeZombies(const CollisionWorld& world, const Model& scene,
                                   glm::vec2 center, std::size_t count,
                                   float radius = 65.0f);

}  // namespace viewer
