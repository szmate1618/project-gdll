#pragma once
#include "protocol.hpp"
#include "camera.hpp"
#include "collision_world.hpp"
#include <optional>

namespace multiplayer {
constexpr float signalReach = 500;
// First opaque hit in runtime X-east/Y-up/Z-south meters; sky misses have no target.
std::optional<glm::vec3> signalRayTarget(const viewer::CollisionWorld& collision,
                                       const viewer::Camera& camera);
using viewer::signalWheelSelection;
struct SignalPlacement { glm::vec3 position; bool atEdge = false; };
// Reposition outside/behind-camera targets just inside the frustum. The margin
// includes the HUD label's half size so the complete marker remains visible.
SignalPlacement signalPlacement(const viewer::Camera& camera, glm::vec3 position,
                                 int width, int height, float marginPixels = 28);
} // namespace multiplayer
