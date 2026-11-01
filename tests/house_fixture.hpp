#pragma once
#include "interior_fixture.hpp"
#include <algorithm>
#include <cmath>

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
// Synthetic near-orthogonal shell. Grid roof tiles give concave fixtures an
// independent cap without using the runtime clipping/triangulation algorithm.
inline viewer::Model outlined(const std::vector<glm::vec2>& outline, const glm::mat4& transform = glm::mat4(1), int levels = 2) {
    viewer::Model model; model.materials.resize(1);
    model.materials[0].doubleSided = true; model.materials[0].baseColor = {.45f, .48f, .52f, 1};
    const float eaves = 3.f * levels + .5f;
    const auto quad = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, const char* suffix, bool low = false) {
        interior_fixture::quad(model, a, b, c, d, std::string("building_house_") + suffix, transform, low);
    };
    interior_fixture::quad(model, {-30, 0, -30}, {-30, 0, 30}, {30, 0, 30}, {30, 0, -30}, "terrain", transform);
    for (std::size_t i = 0; i < outline.size(); ++i) {
        const auto a = outline[i], b = outline[(i + 1) % outline.size()];
        const float length = glm::distance(a, b); const auto direction = (b - a) / length;
        const auto point = [&](float along, float y) { const auto p = a + direction * along; return glm::vec3(p.x, y, p.y); };
        const auto wall = [&](float low, float high, float bottom, float top, const char* suffix, bool lod = false) {
            quad(point(low, bottom), point(high, bottom), point(high, top), point(low, top), suffix, lod);
        };
        wall(0, length, -.08f, eaves, "walls", true);
        const bool door = std::abs(a.y - b.y) < .001f && std::min(a.x, b.x) < -.6f && std::max(a.x, b.x) > .6f;
        if (!door) wall(0, length, -.08f, eaves, "detailed_walls");
        else {
            const float center = -a.x / direction.x;
            wall(0, center - .5f, -.08f, eaves, "detailed_walls");
            wall(center + .5f, length, -.08f, eaves, "detailed_walls");
            wall(center - .5f, center + .5f, 2.1f, eaves, "detailed_walls");
            wall(center - .5f, center + .5f, 0, 2.1f, "doors");
        }
    }
    if (outline.size() == 4) {
        const auto p = [&](std::size_t i) { return glm::vec3(outline[i].x, eaves, outline[i].y); };
        quad(p(0), p(1), p(2), p(3), "roof");
    } else {
        std::vector<float> xs, zs;
        for (const auto p : outline) { xs.push_back(p.x); zs.push_back(p.y); }
        std::sort(xs.begin(), xs.end()); xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
        std::sort(zs.begin(), zs.end()); zs.erase(std::unique(zs.begin(), zs.end()), zs.end());
        for (std::size_t x = 1; x < xs.size(); ++x) for (std::size_t z = 1; z < zs.size(); ++z) {
            const glm::vec2 center((xs[x - 1] + xs[x]) * .5f, (zs[z - 1] + zs[z]) * .5f);
            bool inside = false;
            for (std::size_t i = 0; i < outline.size(); ++i) {
                const auto a = outline[i], b = outline[(i + 1) % outline.size()];
                if ((a.y > center.y) != (b.y > center.y) && center.x < a.x + (b.x - a.x) * (center.y - a.y) / (b.y - a.y)) inside = !inside;
            }
            if (inside) quad({xs[x - 1], eaves, zs[z - 1]}, {xs[x - 1], eaves, zs[z]},
                             {xs[x], eaves, zs[z]}, {xs[x], eaves, zs[z - 1]}, "roof");
        }
    }
    for (int row = 0; row < levels; ++row)
        quad({-5, .9f + 3 * row, -.6f}, {-5, .9f + 3 * row, .6f},
             {-5, 2.1f + 3 * row, .6f}, {-5, 2.1f + 3 * row, -.6f}, "windows");
    interior_fixture::bounds(model);
    return model;
}
} // namespace house_fixture
