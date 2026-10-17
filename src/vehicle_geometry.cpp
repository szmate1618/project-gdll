#include "vehicle_layer.hpp"
#include <glm/gtc/constants.hpp>
#include <cmath>

namespace viewer {
Model VehicleLayer::geometry() {
    auto model = BoxLayer::unitCube();
    model.materials[0].baseColor = {.16f, .32f, .48f, 1};
    Material rubber; rubber.baseColor = {.035f, .038f, .04f, 1};
    model.materials.push_back(rubber);
    Primitive tire; tire.material = 1;
    const auto triangle = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c) {
        const auto normal = glm::normalize(glm::cross(b - a, c - a));
        const auto first = static_cast<std::uint32_t>(tire.vertices.size());
        for (const auto p : {a, b, c}) {
            Vertex v; v.position = p; v.normal = normal; tire.vertices.push_back(v);
        }
        tire.indices.insert(tire.indices.end(), {first, first + 1, first + 2});
    };
    for (int i = 0; i < 12; ++i) {
        const float a = glm::two_pi<float>() * i / 12, b = glm::two_pi<float>() * (i + 1) / 12;
        const glm::vec3 loA(wheelRadius * std::cos(a), -wheelWidth / 2, wheelRadius * std::sin(a));
        const glm::vec3 loB(wheelRadius * std::cos(b), -wheelWidth / 2, wheelRadius * std::sin(b));
        const auto hiA = loA + glm::vec3(0, wheelWidth, 0), hiB = loB + glm::vec3(0, wheelWidth, 0);
        triangle(loA, hiA, hiB); triangle(loA, hiB, loB);
        triangle({0, -wheelWidth / 2, 0}, loA, loB);
        triangle({0, wheelWidth / 2, 0}, hiB, hiA);
    }
    model.primitives.push_back(std::move(tire));
    return model;
}
} // namespace viewer
