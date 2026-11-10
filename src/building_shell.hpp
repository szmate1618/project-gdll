#pragma once

#include "model.hpp"
#include <array>

namespace viewer {
struct HouseSource;
namespace building_shell {
using Face = std::array<glm::vec3, 3>;
std::vector<Face> faces(const Model& model, const std::vector<std::size_t>& draws);
std::vector<float> windowHeads(const std::vector<Face>& windows);
struct Shell {
    glm::vec2 origin{0}, u{1, 0}, v{0, 1}, low{0}, high{0};
    std::vector<glm::vec2> outline; // Empty is the established rectangular fast path.
    float floor = 0, eaves = 0, roofTop = 0;
    bool closed = false;
    glm::vec2 local(glm::vec2 p) const { const auto d = p - origin; return {glm::dot(d, u), glm::dot(d, v)}; }
    glm::vec3 world(glm::vec2 p, float y) const { const auto q = origin + u * p.x + v * p.y; return {q.x, y, q.y}; }
};
// Wall/roof geometry only. Floor uses the highest sampled wall base plus the
// generator's 8 cm foundation skin, or the explicit main-floor datum on basement
// shells; doors, windows and neighbours are not read.
// Callers may lower the default 45 m2 minimum for a smaller layout category;
// closure, axis tolerance, polygon complexity and envelope fill remain checked.
std::optional<Shell> inspect(const Model& model, const HouseSource& source, float minimumArea = 45);
} // namespace building_shell
} // namespace viewer
