#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <utility>
#include <vector>

namespace viewer::house_polygon {
using Polygon = std::vector<glm::vec2>; // One simple boundary, no holes; horizontal metres.
float area(const Polygon& polygon);
bool simple(const Polygon& polygon);
bool contains(const Polygon& polygon, glm::vec2 point, float margin = .001f);
// A disconnected intersection is rejected rather than bridged across exterior space.
Polygon clip(const Polygon& polygon, glm::vec2 low, glm::vec2 high);
bool covers(const Polygon& polygon, glm::vec2 low, glm::vec2 high);
std::vector<std::uint32_t> triangles(const Polygon& polygon);
std::vector<std::pair<float, float>> spans(const Polygon& polygon, int axis, float position, float low, float high);
} // namespace viewer::house_polygon
