#include "model_merge.hpp"

#include <iterator>

namespace viewer {
void appendStaticModel(Model& destination, Model&& source) {
    if (source.draws.empty()) return;
    const auto imageOffset = static_cast<int>(destination.images.size());
    const auto textureOffset = static_cast<int>(destination.textures.size());
    const auto materialOffset = static_cast<int>(destination.materials.size());
    const auto primitiveOffset = destination.primitives.size();
    for (auto& texture : source.textures)
        if (texture.image >= 0) texture.image += imageOffset;
    for (auto& material : source.materials)
        if (material.baseColorTexture >= 0) material.baseColorTexture += textureOffset;
    for (auto& mesh : source.primitives)
        if (mesh.material >= 0) mesh.material += materialOffset;
    for (auto& draw : source.draws) draw.primitive += primitiveOffset;
    if (destination.draws.empty()) {
        destination.boundsMin = source.boundsMin;
        destination.boundsMax = source.boundsMax;
    } else {
        destination.boundsMin = glm::min(destination.boundsMin, source.boundsMin);
        destination.boundsMax = glm::max(destination.boundsMax, source.boundsMax);
    }
    const auto transfer = [](auto& to, auto& from) {
        to.insert(to.end(), std::make_move_iterator(from.begin()), std::make_move_iterator(from.end()));
    };
    transfer(destination.images, source.images);
    transfer(destination.textures, source.textures);
    transfer(destination.materials, source.materials);
    transfer(destination.primitives, source.primitives);
    transfer(destination.draws, source.draws);
    transfer(destination.warnings, source.warnings);
}
} // namespace viewer
