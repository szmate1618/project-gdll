#pragma once

#include "model.hpp"
#include <functional>
#include <optional>

namespace viewer {

// Local east/north metres. Rings include their closing vertex; exterior CCW,
// holes CW. Runtime geometry uses X=east, Y=height, Z=-north.
struct ForestPolygon {
    std::vector<glm::dvec2> exterior;
    std::vector<std::vector<glm::dvec2>> holes;
};
using ForestGround = std::function<std::optional<float>(glm::dvec2)>;

void validateForestPolygon(const ForestPolygon& polygon);
bool forestContains(const ForestPolygon& polygon, glm::dvec2 point);
double forestArea(const ForestPolygon& polygon);
// Polygon-shaped box shell: terrain-following walls and canopy top. Even/odd
// slab decomposition handles concavity and holes without a triangle fan.
std::pair<Primitive, Primitive> forestVolume(const ForestPolygon& polygon,
    const ForestGround& ground, float height, float tileMeters);

} // namespace viewer
