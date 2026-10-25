#pragma once
#include "model.hpp"
#include <array>
#include <utility>

// Four distinct quadrants: NW red, NE green, SW blue, SE yellow.
inline viewer::Model minimapFixture() {
    viewer::Model model;
    viewer::ImageData image{4, 4, {}};
    for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) {
        const std::array<std::array<std::uint8_t, 4>, 4> colors{{
            {{220, 30, 20, 255}}, {{20, 210, 30, 255}}, {{20, 30, 220, 255}}, {{220, 210, 20, 255}}}};
        const auto& color = colors[(y / 2) * 2 + x / 2];
        image.rgba.insert(image.rgba.end(), color.begin(), color.end());
    }
    model.images.push_back(std::move(image));
    model.textures.push_back({0});
    model.materials.emplace_back(); model.materials.back().baseColorTexture = 0;
    viewer::Primitive mesh;
    for (const auto& corner : {glm::vec2(0, 0), glm::vec2(1, 0), glm::vec2(1, 1), glm::vec2(0, 1)}) {
        viewer::Vertex vertex;
        vertex.position = {corner.x * 400 - 200, 0, corner.y * 300 - 150};
        vertex.uv = corner;
        mesh.vertices.push_back(vertex);
    }
    mesh.material = 0; mesh.indices = {0, 2, 1, 0, 3, 2};
    model.primitives.push_back(std::move(mesh));
    model.draws.push_back({0, glm::mat4(1), "terrain"});
    return model;
}

inline viewer::Model minimapRoadFixture() {
    auto model = minimapFixture();
    viewer::Primitive road;
    for (const auto p : {glm::vec3(80, 15, -350), glm::vec3(100, 15, -350),
                         glm::vec3(100, 15, 350), glm::vec3(80, 15, 350)}) {
        viewer::Vertex vertex; vertex.position = p; road.vertices.push_back(vertex);
    }
    road.material = 0; // Deliberately textured: the minimap must still draw a flat color.
    road.indices = {0, 1, 2, 0, 2, 3};
    model.primitives.push_back(std::move(road));
    model.draws.push_back({1, glm::mat4(1), "roads_asphalt"});
    return model;
}
