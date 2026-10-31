#pragma once
#include "interior_fixture.hpp"

namespace house_fixture {
inline viewer::Model house(const glm::mat4& transform = glm::mat4(1), bool upstairs = true, int storeys = 0) {
    viewer::Model model;
    model.materials.push_back({});
    model.materials[0].doubleSided = true;
    model.materials[0].baseColor = {.45f, .48f, .52f, 1};
    const int levels = storeys > 0 ? storeys : upstairs ? 2 : 1;
    const float eaves = levels * 3.f + .5f;
    const auto quad = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, const std::string& suffix, bool low = false) {
        interior_fixture::quad(model, a, b, c, d, "building_house_" + suffix, transform, low);
    };
    interior_fixture::quad(model, {-30, 0, -30}, {-30, 0, 30}, {30, 0, 30}, {30, 0, -30}, "terrain", transform);
    quad({-5, eaves, -6}, {-5, eaves, 6}, {5, eaves, 6}, {5, eaves, -6}, "roof");
    for (const float sign : {-1.f, 1.f}) {
        const float z = sign * 6;
        const auto point = [&](float x, float y) { return glm::vec3(x * sign, y, z); };
        quad(point(-5, -.08f), point(5, -.08f), point(5, eaves), point(-5, eaves), "walls", true);
        for (const auto span : {glm::vec2(-5, -.5f), glm::vec2(.5f, 5)})
            quad(point(span.x, -.08f), point(span.y, -.08f), point(span.y, eaves), point(span.x, eaves), "detailed_walls");
        quad(point(-.5f, 2.1f), point(.5f, 2.1f), point(.5f, eaves), point(-.5f, eaves), "detailed_walls");
        quad(point(-.5f, -.08f), point(.5f, -.08f), point(.5f, 2.1f), point(-.5f, 2.1f), "doors");
        for (int row = 0; row < levels; ++row)
            quad(point(2, .9f + row * 3), point(3.2f, .9f + row * 3), point(3.2f, 2.1f + row * 3), point(2, 2.1f + row * 3), "windows");
        const float x = sign * 5;
        quad({x, -.08f, 6}, {x, -.08f, -6}, {x, eaves, -6}, {x, eaves, 6}, "walls", true);
        quad({x, -.08f, 6}, {x, -.08f, -6}, {x, eaves, -6}, {x, eaves, 6}, "detailed_walls");
    }
    interior_fixture::bounds(model);
    return model;
}
} // namespace house_fixture
