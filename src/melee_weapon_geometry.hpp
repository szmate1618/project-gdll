#pragma once
#include "model.hpp"
#include <array>

namespace viewer {
// Extrude a counterclockwise XY polygon into a closed solid with flat normals.
template<std::size_t N>
void meleeWeaponPrism(Model& model, const std::array<glm::vec2, N>& outline, float halfDepth, glm::vec3 color) {
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
} // namespace viewer
