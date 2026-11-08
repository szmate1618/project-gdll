#include "sword_geometry.hpp"
#include "sword_melee.hpp"
#include <array>

namespace viewer {
namespace {
// Extrude a counterclockwise XY polygon into a closed solid with flat normals.
template<std::size_t N>
void prism(Model& model, const std::array<glm::vec2, N>& outline, float halfDepth, glm::vec3 color) {
    Primitive mesh; mesh.material = 0;
    const auto triangle = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c) {
        const auto normal = glm::normalize(glm::cross(b - a, c - a));
        for (const auto point : {a, b, c}) {
            mesh.indices.push_back(static_cast<std::uint32_t>(mesh.vertices.size()));
            Vertex v; v.position = point; v.normal = normal; v.color = glm::vec4(color, 1);
            mesh.vertices.push_back(v);
        }
    };
    for (std::size_t i = 1; i + 1 < N; ++i) {
        triangle({outline[0], halfDepth}, {outline[i], halfDepth}, {outline[i + 1], halfDepth});
        triangle({outline[0], -halfDepth}, {outline[i + 1], -halfDepth}, {outline[i], -halfDepth});
    }
    for (std::size_t i = 0; i < N; ++i) {
        const auto a = outline[i], b = outline[(i + 1) % N];
        triangle({a, -halfDepth}, {b, -halfDepth}, {b, halfDepth});
        triangle({a, -halfDepth}, {b, halfDepth}, {a, halfDepth});
    }
    DrawInstance draw; draw.primitive = model.primitives.size();
    model.draws.push_back(draw); model.primitives.push_back(std::move(mesh));
}
}
Model makeSwordModel() {
    Model model; model.materials.emplace_back();
    // Narrow six-sided outline: tapered heel, parallel edges, pointed tip.
    prism(model, std::array<glm::vec2, 6>{{{0, swordBladeBase}, {.026f, .10f},
        {.026f, .94f}, {0, swordBladeTip}, {-.026f, .94f}, {-.026f, .10f}}}, .006f, {.78f, .82f, .87f});
    prism(model, std::array<glm::vec2, 4>{{{-.13f, -.015f}, {.13f, -.015f},
        {.13f, .015f}, {-.13f, .015f}}}, .018f, {.32f, .29f, .19f});
    prism(model, std::array<glm::vec2, 4>{{{-.019f, -.19f}, {.019f, -.19f},
        {.019f, -.015f}, {-.019f, -.015f}}}, .018f, {.16f, .085f, .04f});
    prism(model, std::array<glm::vec2, 4>{{{-.03f, -.23f}, {.03f, -.23f},
        {.03f, -.19f}, {-.03f, -.19f}}}, .024f, {.32f, .29f, .19f});
    model.boundsMin = {-.13f, -.23f, -.024f}; model.boundsMax = {.13f, swordBladeTip, .024f};
    return model;
}
} // namespace viewer
