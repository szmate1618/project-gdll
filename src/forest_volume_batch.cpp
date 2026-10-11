#include "forest_volume_batch.hpp"

#include <glm/gtc/matrix_inverse.hpp>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>

namespace viewer {

BatchedForestVolumes batchForestVolumes(const ForestLayer& forests) {
    BatchedForestVolumes result;
    result.model.images = forests.volumes.images;
    result.model.textures = forests.volumes.textures;
    result.model.materials = forests.volumes.materials;
    std::map<int, std::size_t> materials;
    std::vector<bool> assigned(forests.volumes.draws.size(), false);
    for (std::size_t patchIndex = 0; patchIndex < forests.patches.size(); ++patchIndex) {
        const auto& patch = forests.patches[patchIndex];
        if (patch.firstDraw > assigned.size() || patch.drawCount > assigned.size() - patch.firstDraw)
            throw std::runtime_error("Forest draw range exceeds the volume layer");
        for (std::size_t i = patch.firstDraw; i < patch.firstDraw + patch.drawCount; ++i) {
            if (assigned[i]) throw std::runtime_error("Forest draw ranges overlap");
            assigned[i] = true;
            const auto& draw = forests.volumes.draws[i];
            const auto& source = forests.volumes.primitives.at(draw.primitive);
            if (source.indices.empty()) continue;
            if (source.indices.size() % 3) throw std::runtime_error("Forest volume is not triangle geometry");
            const auto found = materials.emplace(source.material, result.model.primitives.size());
            if (found.second) {
                Primitive primitive;
                primitive.material = source.material;
                result.model.primitives.push_back(std::move(primitive));
                result.ranges.emplace_back();
            }
            const auto batch = found.first->second;
            auto& target = result.model.primitives[batch];
            if (source.vertices.size() > std::numeric_limits<std::uint32_t>::max() - target.vertices.size())
                throw std::runtime_error("Forest batch has too many vertices");
            const auto base = static_cast<std::uint32_t>(target.vertices.size());
            const auto determinant = glm::determinant(glm::mat3(draw.transform));
            if (!std::isfinite(determinant) || determinant == 0)
                throw std::runtime_error("Forest volume has a singular draw transform");
            const auto normalMatrix = glm::inverseTranspose(glm::mat3(draw.transform));
            for (auto vertex : source.vertices) {
                vertex.position = glm::vec3(draw.transform * glm::vec4(vertex.position, 1));
                vertex.normal = normalMatrix * vertex.normal;
                target.vertices.push_back(vertex);
            }
            const auto first = target.indices.size();
            for (std::size_t triangle = 0; triangle < source.indices.size(); triangle += 3) {
                for (std::size_t corner = 0; corner < 3; ++corner) {
                    const auto offset = determinant < 0 && corner ? 3 - corner : corner;
                    const auto index = source.indices[triangle + offset];
                    if (index >= source.vertices.size()) throw std::runtime_error("Invalid forest volume vertex index");
                    target.indices.push_back(base + index);
                }
            }
            auto& ranges = result.ranges[batch];
            if (!ranges.empty() && ranges.back().patch == patchIndex)
                ranges.back().indexCount += source.indices.size();
            else ranges.push_back({patchIndex, first, source.indices.size()});
        }
    }
    return result;
}

} // namespace viewer
