#include "roof_batch.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <tuple>

#include <glm/gtc/matrix_inverse.hpp>

namespace viewer {
namespace {
bool staticRoof(const DrawInstance& draw, const Primitive& mesh, const Model& model) {
    const auto& name = draw.name;
    return name.rfind("building_", 0) == 0 && name.size() >= 5 &&
        name.compare(name.size() - 5, 5, "_roof") == 0 &&
        draw.horizontalMotionRadius == 0 && draw.detailDistance == 0 &&
        draw.detailGroup.empty() && !draw.lowDetail && !draw.fence &&
        (mesh.material < 0 || model.materials.at(static_cast<std::size_t>(mesh.material)).alphaMode == "OPAQUE");
}
} // namespace

RoofBatches buildRoofBatches(const Model& model) {
    RoofBatches result;
    result.batchForDraw.assign(model.draws.size(), noRoofBatch);
    // floor retains separate cells on either side of the geographic origin.
    using Key = std::tuple<int, double, double>;
    std::map<Key, std::size_t> groups;
    constexpr double cellSize = 128.0;
    for (std::size_t drawIndex = 0; drawIndex < model.draws.size(); ++drawIndex) {
        const auto& draw = model.draws[drawIndex];
        const auto& source = model.primitives.at(draw.primitive);
        if (!staticRoof(draw, source, model) || source.vertices.empty() || source.indices.empty()) continue;
        const float determinant = glm::determinant(glm::mat3(draw.transform));
        // Degenerate transforms cannot be baked with a usable normal matrix.
        if (!std::isfinite(determinant) || determinant == 0) continue;
        const auto normalMatrix = glm::inverseTranspose(glm::mat3(draw.transform));
        Primitive baked;
        baked.material = source.material;
        baked.vertices.reserve(source.vertices.size());
        glm::vec3 minimum(std::numeric_limits<float>::max()), maximum(std::numeric_limits<float>::lowest());
        for (auto vertex : source.vertices) {
            vertex.position = glm::vec3(draw.transform * glm::vec4(vertex.position, 1));
            // Preserve normal magnitudes: the shader normalizes after interpolation.
            vertex.normal = normalMatrix * vertex.normal;
            for (int axis = 0; axis < 3; ++axis)
                if (!std::isfinite(vertex.position[axis]) || !std::isfinite(vertex.normal[axis]))
                    throw std::invalid_argument("Non-finite baked roof vertex");
            minimum = glm::min(minimum, vertex.position);
            maximum = glm::max(maximum, vertex.position);
            baked.vertices.push_back(vertex);
        }
        const auto center = glm::dvec3(minimum) * .5 + glm::dvec3(maximum) * .5;
        const Key key{source.material, std::floor(center.x / cellSize), std::floor(center.z / cellSize)};
        const auto inserted = groups.emplace(key, result.meshes.size());
        if (inserted.second) {
            result.meshes.emplace_back();
            result.meshes.back().material = source.material;
        }
        auto& batch = result.meshes[inserted.first->second];
        if (batch.vertices.size() + baked.vertices.size() > std::numeric_limits<std::uint32_t>::max() ||
            batch.indices.size() + source.indices.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            throw std::length_error("Roof batch exceeds OpenGL index limits");
        if (source.indices.size() % 3 != 0) throw std::invalid_argument("Roof indices must describe triangles");
        const auto base = static_cast<std::uint32_t>(batch.vertices.size());
        batch.vertices.insert(batch.vertices.end(), baked.vertices.begin(), baked.vertices.end());
        for (std::size_t triangle = 0; triangle < source.indices.size(); triangle += 3) {
            auto a = source.indices[triangle], b = source.indices[triangle + 1], c = source.indices[triangle + 2];
            if (a >= source.vertices.size() || b >= source.vertices.size() || c >= source.vertices.size())
                throw std::invalid_argument("Roof index outside vertex buffer");
            // Original reflected draws use GL_CW; merged meshes all use GL_CCW.
            if (determinant < 0) std::swap(b, c);
            batch.indices.insert(batch.indices.end(), {base + a, base + b, base + c});
        }
        result.batchForDraw[drawIndex] = inserted.first->second;
    }
    return result;
}
} // namespace viewer
