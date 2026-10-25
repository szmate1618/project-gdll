#include "minimap.hpp"
#include "model.hpp"
#include <cmath>
#include <limits>

namespace viewer {
std::optional<MinimapSource> findMinimapSource(const Model& model) {
    const DrawInstance* terrain = nullptr;
    for (const auto& draw : model.draws) {
        if (draw.name != "terrain") continue;
        if (terrain) return {};
        terrain = &draw;
    }
    if (!terrain || terrain->primitive >= model.primitives.size()) return {};
    const auto& mesh = model.primitives[terrain->primitive];
    if (mesh.vertices.size() < 3 || mesh.material < 0 || std::size_t(mesh.material) >= model.materials.size()) return {};
    const int texture = model.materials[mesh.material].baseColorTexture;
    if (texture < 0 || std::size_t(texture) >= model.textures.size()) return {};
    const int imageIndex = model.textures[texture].image;
    if (imageIndex < 0 || std::size_t(imageIndex) >= model.images.size()) return {};
    const auto& image = model.images[imageIndex];
    if (image.width < 2 || image.height < 2 || image.rgba.empty()) return {};

    const auto horizontal = [&](std::size_t index) {
        const auto p = glm::dmat4(terrain->transform) * glm::dvec4(mesh.vertices[index].position, 1);
        return glm::dvec2(p.x, p.z);
    };
    const auto a = horizontal(0);
    std::size_t bIndex = 0, cIndex = 0;
    double longest = 0, largestArea = 0;
    for (std::size_t i = 1; i < mesh.vertices.size(); ++i) {
        const auto offset = horizontal(i) - a;
        const double distance = glm::dot(offset, offset);
        if (distance > longest) { longest = distance; bIndex = i; }
    }
    const auto ab = horizontal(bIndex) - a;
    for (std::size_t i = 1; i < mesh.vertices.size(); ++i) {
        const auto ac = horizontal(i) - a;
        const double area = std::abs(ab.x * ac.y - ab.y * ac.x);
        if (area > largestArea) { largestArea = area; cIndex = i; }
    }
    if (largestArea < 1e-8) return {};
    const auto b = horizontal(bIndex), c = horizontal(cIndex);
    const glm::dmat3 world{glm::dvec3(a, 1), glm::dvec3(b, 1), glm::dvec3(c, 1)};
    const glm::dmat3 uv{glm::dvec3(mesh.vertices[0].uv, 1), glm::dvec3(mesh.vertices[bIndex].uv, 1),
                         glm::dvec3(mesh.vertices[cIndex].uv, 1)};
    MinimapSource result{texture, uv * glm::inverse(world)};
    glm::vec2 minimum(std::numeric_limits<float>::max()), maximum(std::numeric_limits<float>::lowest());
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        const auto expected = mesh.vertices[i].uv;
        const auto actual = glm::dvec2(result.uvFromWorld * glm::dvec3(horizontal(i), 1));
        if (!std::isfinite(actual.x) || !std::isfinite(actual.y) ||
            glm::length(actual - glm::dvec2(expected)) > 1e-4 ||
            expected.x < -1e-5 || expected.y < -1e-5 || expected.x > 1.00001 || expected.y > 1.00001) return {};
        minimum = glm::min(minimum, expected); maximum = glm::max(maximum, expected);
    }
    if (glm::length(minimum) > 1e-4 || glm::length(maximum - glm::vec2(1)) > 1e-4) return {};
    return result;
}
} // namespace viewer
