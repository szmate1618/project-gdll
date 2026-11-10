#pragma once

#include "inventory/item.hpp"
#include <array>
#include <vector>
#include <glm/mat4x4.hpp>

namespace viewer {
// The grip is a model-local palm frame. Held geometry uses its inverse, so
// model positions and the saved palm contact agree in rendering and collision.
glm::mat4 craftedItemToGrip(const Item& item);
// Closed primitive point clouds in the same grip-local frame used for drawing.
std::vector<std::array<glm::vec3,8>> craftedMeleeParts(const Item& item);
}
