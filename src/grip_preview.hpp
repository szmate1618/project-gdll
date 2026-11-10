#pragma once
#include <glm/vec3.hpp>
#include <glm/gtc/quaternion.hpp>

namespace viewer {
// Palm-heel contact frame in world metres: +Z enters the object, +Y fingers.
struct GripPreviewPose { glm::vec3 position{0}; glm::quat rotation{1,0,0,0}; };
}
