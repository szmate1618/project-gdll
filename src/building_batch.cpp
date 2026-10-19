#include "building_batch.hpp"

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
bool staticWall(const DrawInstance& draw, const Primitive& mesh, const Model& model) {
    const auto& name = draw.name;
    const auto ends = [&](const std::string& suffix) {
        return name.size() >= suffix.size() && name.compare(name.size()-suffix.size(), suffix.size(), suffix) == 0;
    };
    return name.rfind("building_", 0) == 0 && ends("_walls") && !ends("_detailed_walls") &&
        draw.horizontalMotionRadius == 0 && !draw.fence &&
        (draw.detailDistance == 0 || draw.lowDetail) &&
        (mesh.material < 0 || model.materials.at(static_cast<std::size_t>(mesh.material)).alphaMode == "OPAQUE");
}

// A visibility object requests wall batches; roofs have no detail alternatives.
BuildingBatches buildBatches(const Model& model, const ModelVisibility* visibility) {
    BuildingBatches result;
    result.batchForDraw.assign(model.draws.size(), noBuildingBatch);
    // floor retains separate cells on either side of the geographic origin.
    using Key = std::tuple<int, double, double>;
    std::map<Key, std::size_t> groups;
    constexpr double cellSize = 128.0;
    for (std::size_t drawIndex = 0; drawIndex < model.draws.size(); ++drawIndex) {
        const auto& draw = model.draws[drawIndex];
        const auto& source = model.primitives.at(draw.primitive);
        const bool eligible = visibility ? staticWall(draw, source, model) : staticRoof(draw, source, model);
        if (!eligible || source.vertices.empty() || source.indices.empty()) continue;
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
                    throw std::invalid_argument("Non-finite baked building vertex");
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
            result.details.emplace_back();
        }
        auto& batch = result.meshes[inserted.first->second];
        if (batch.vertices.size() + baked.vertices.size() > std::numeric_limits<std::uint32_t>::max() ||
            batch.indices.size() + source.indices.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            throw std::length_error("Building batch exceeds OpenGL index limits");
        if (source.indices.size() % 3 != 0) throw std::invalid_argument("Building indices must describe triangles");
        const auto base = static_cast<std::uint32_t>(batch.vertices.size());
        batch.vertices.insert(batch.vertices.end(), baked.vertices.begin(), baked.vertices.end());
        for (std::size_t triangle = 0; triangle < source.indices.size(); triangle += 3) {
            auto a = source.indices[triangle], b = source.indices[triangle + 1], c = source.indices[triangle + 2];
            if (a >= source.vertices.size() || b >= source.vertices.size() || c >= source.vertices.size())
                throw std::invalid_argument("Building index outside vertex buffer");
            // Original reflected draws use GL_CW; merged meshes all use GL_CCW.
            if (determinant < 0) std::swap(b, c);
            batch.indices.insert(batch.indices.end(), {base + a, base + b, base + c});
        }
        result.batchForDraw[drawIndex] = inserted.first->second;
        if (visibility) {
            auto& detail = result.details[inserted.first->second];
            const auto& bounds = visibility->detailBounds(drawIndex);
            detail.bounds.minimum = glm::min(detail.bounds.minimum, bounds.minimum);
            detail.bounds.maximum = glm::max(detail.bounds.maximum, bounds.maximum);
            detail.distance = std::max(detail.distance, draw.detailDistance);
        }
    }
    return result;
}
} // namespace

BuildingBatches buildRoofBatches(const Model& model) { return buildBatches(model, nullptr); }
BuildingBatches buildWallBatches(const Model& model, const ModelVisibility& visibility) {
    return buildBatches(model, &visibility);
}
} // namespace viewer
