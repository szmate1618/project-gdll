#pragma once

#include "model.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <limits>

namespace interior_fixture {
inline void quad(viewer::Model& model, glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d,
                 const std::string& name, const glm::mat4& node = glm::mat4(1), bool low = false) {
    viewer::Primitive primitive;
    primitive.material = 0;
    const auto normal = glm::normalize(glm::cross(b - a, c - a));
    for (const auto p : {a, b, c, d}) {
        viewer::Vertex v; v.position = p; v.normal = normal;
        primitive.vertices.push_back(v);
    }
    primitive.indices = {0, 1, 2, 0, 2, 3};
    const bool detail = name.rfind("building_", 0) == 0 && name.find("_roof") == std::string::npos;
    model.draws.push_back({model.primitives.size(), node, name, detail ? 120.0f : 0, detail && low, detail ? "building_test" : ""});
    model.primitives.push_back(std::move(primitive));
}
inline void bounds(viewer::Model& model) {
    model.boundsMin = glm::vec3(std::numeric_limits<float>::max());
    model.boundsMax = -model.boundsMin;
    for (const auto& draw : model.draws) for (const auto& v : model.primitives[draw.primitive].vertices) {
        const auto p = glm::vec3(draw.transform * glm::vec4(v.position, 1));
        model.boundsMin = glm::min(model.boundsMin, p); model.boundsMax = glm::max(model.boundsMax, p);
    }
}
inline viewer::Model town(bool courtyard = false, const glm::mat4& node = glm::mat4(1)) {
    viewer::Model model;
    model.materials.push_back({});
    quad(model, {-30, 0, -30}, {-30, 0, 30}, {30, 0, 30}, {30, 0, -30}, "terrain", node);
    for (const auto rect : std::vector<glm::vec4>(courtyard ?
         std::vector<glm::vec4>{{-7, -6, -1, 6}, {1, -6, 7, 6}, {-1, -6, 1, -1}, {-1, 1, 1, 6}} :
         std::vector<glm::vec4>{{-7, -6, 7, 6}}))
        quad(model, {rect.x, 3.5f, rect.y}, {rect.x, 3.5f, rect.w}, {rect.z, 3.5f, rect.w}, {rect.z, 3.5f, rect.y}, "building_test_roof", node, true);
    for (const auto rect : std::vector<glm::vec4>(courtyard ?
         std::vector<glm::vec4>{{-7, -6, 7, 6}, {-1, -1, 1, 1}} : std::vector<glm::vec4>{{-7, -6, 7, 6}})) {
        const auto a = glm::vec3(rect.x, -.08f, rect.y), b = glm::vec3(rect.z, -.08f, rect.y);
        const auto c = glm::vec3(rect.z, -.08f, rect.w), d = glm::vec3(rect.x, -.08f, rect.w);
        for (const auto& edge : std::vector<std::pair<glm::vec3, glm::vec3>>{{b, a}, {c, b}, {d, c}, {a, d}}) {
            quad(model, edge.first, edge.second, edge.second + glm::vec3(0, 3.58f, 0), edge.first + glm::vec3(0, 3.58f, 0), "building_test_walls", node, true);
            if (edge.first.z != 6 || edge.second.z != 6)
                quad(model, edge.first, edge.second, edge.second + glm::vec3(0, 3.58f, 0), edge.first + glm::vec3(0, 3.58f, 0), "building_test_detailed_walls", node);
        }
    }
    // Matching detailed masonry enables real doorway clipping in CollisionWorld.
    quad(model, {-7, -.08f, 6}, {-.5f, -.08f, 6}, {-.5f, 3.5f, 6}, {-7, 3.5f, 6}, "building_test_detailed_walls", node);
    quad(model, {.5f, -.08f, 6}, {3.5f, -.08f, 6}, {3.5f, 3.5f, 6}, {.5f, 3.5f, 6}, "building_test_detailed_walls", node);
    quad(model, {4.5f, -.08f, 6}, {7, -.08f, 6}, {7, 3.5f, 6}, {4.5f, 3.5f, 6}, "building_test_detailed_walls", node);
    for (const float center : {0.0f, 4.0f})
        quad(model, {center - .5f, 2.1f, 6}, {center + .5f, 2.1f, 6}, {center + .5f, 3.5f, 6}, {center - .5f, 3.5f, 6}, "building_test_detailed_walls", node);
    quad(model, {-.5f, -.08f, 6}, {.5f, -.08f, 6}, {.5f, 2.1f, 6}, {-.5f, 2.1f, 6}, "building_test_doors", node);
    quad(model, {3.5f, -.08f, 6}, {4.5f, -.08f, 6}, {4.5f, 2.1f, 6}, {3.5f, 2.1f, 6}, "building_test_doors", node);
    bounds(model);
    return model;
}
} // namespace interior_fixture
