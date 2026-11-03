#pragma once

#include "model.hpp"

namespace viewer::test {
inline void quad(viewer::Model& model, const std::string& name, glm::vec3 a, glm::vec3 b,
          glm::vec3 c, glm::vec3 d, bool detail = false, bool low = false) {
    viewer::Primitive primitive;
    for (const auto point : {a, b, c, d}) {
        viewer::Vertex vertex;
        vertex.position = point;
        vertex.normal = glm::normalize(glm::cross(b - a, c - a));
        primitive.vertices.push_back(vertex);
    }
    primitive.indices = {0, 1, 2, 0, 2, 3};
    model.draws.push_back({model.primitives.size(), glm::mat4(1), name, detail ? 120.f : 0, low,
                           detail ? "building_test" : ""});
    model.primitives.push_back(std::move(primitive));
}
inline viewer::Model doorway(bool outward = true, float width = 1.f) {
    viewer::Model model;
    quad(model, "terrain", {-30, 0, -30}, {-30, 0, 30}, {30, 0, 30}, {30, 0, -30});
    const auto panel = [&](float left, float bottom, float right, float top, const std::string& suffix, bool low = false) {
        quad(model, "building_test_" + suffix, {left, bottom, 0}, {right, bottom, 0},
             {right, top, 0}, {left, top, 0}, true, low);
    };
    const float half = width * .5f;
    panel(-10, 0, 10, 3, "walls", true);
    panel(-10, 0, -half, 3, "detailed_walls");
    panel(half, 0, 10, 3, "detailed_walls");
    panel(-half, 2.1f, half, 3, "detailed_walls");
    if (outward) panel(-half, 0, half, 2.1f, "doors");
    else quad(model, "building_test_doors", {half, 0, 0}, {-half, 0, 0},
              {-half, 2.1f, 0}, {half, 2.1f, 0}, true);
    return model;
}
} // namespace viewer::test
