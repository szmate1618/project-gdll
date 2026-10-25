#include "minimap.hpp"
#include "minimap_fixture.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}
int main() {
    try {
        auto model = minimapFixture();
        // Translate, rotate and reflect the terrain. Map UVs must follow the
        // active draw transform once, not scene-wide bounds or config defaults.
        model.draws[0].transform = glm::translate(glm::mat4(1), {430, 12, -810}) *
            glm::rotate(glm::mat4(1), glm::radians(90.f), {0, 1, 0}) * glm::scale(glm::mat4(1), {-2, 1, 3});
        const auto source = viewer::findMinimapSource(model);
        require(source.has_value() && source->texture == 0, "Missing published terrain imagery");
        const auto uvAt = [&](double x, double z) { return glm::dvec2(source->uvFromWorld * glm::dvec3(x, z, 1)); };
        require(glm::length(uvAt(-20, -1210) - glm::dvec2(0, 0)) < 1e-5, "Transformed northwest corner is wrong");
        require(glm::length(uvAt(880, -410) - glm::dvec2(1, 1)) < 1e-5, "Transformed southeast corner is wrong");
        require(glm::length(uvAt(430, -810) - glm::dvec2(.5)) < 1e-5, "Scene origin/transform applied twice");
        model = minimapFixture(); model.draws[0].name = "building";
        require(!viewer::findMinimapSource(model), "Non-terrain image was selected");
        model = minimapFixture(); model.draws.push_back(model.draws[0]);
        require(!viewer::findMinimapSource(model), "Ambiguous active terrain was accepted");
        model = minimapFixture(); model.materials[0].baseColorTexture = -1;
        require(!viewer::findMinimapSource(model), "Untextured terrain was accepted");
        model = minimapFixture(); model.primitives[0].vertices[2].uv = {.8f, .8f};
        require(!viewer::findMinimapSource(model), "Non-affine terrain UVs were accepted");
        model = minimapFixture(); model.primitives[0].vertices[2].uv = {4, 4};
        require(!viewer::findMinimapSource(model), "Repeated texture was treated as satellite imagery");
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
