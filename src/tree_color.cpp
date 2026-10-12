#include "tree_color.hpp"

#include <array>
#include <cmath>

namespace viewer {
namespace {
const std::array<double, 256>& linearChannels() {
    static const auto channels = [] {
        std::array<double, 256> result{};
        for (std::size_t i = 0; i < result.size(); ++i) {
            const double value = static_cast<double>(i) / 255;
            result[i] = value <= .04045 ? value / 12.92 : std::pow((value + .055) / 1.055, 2.4);
        }
        return result;
    }();
    return channels;
}
}

std::optional<glm::vec3> visibleTextureColor(const Model& model, const Material& material) {
    const int texture = material.baseColorTexture;
    if (texture < 0 || static_cast<std::size_t>(texture) >= model.textures.size()) return std::nullopt;
    const int imageIndex = model.textures[texture].image;
    if (imageIndex < 0 || static_cast<std::size_t>(imageIndex) >= model.images.size()) return std::nullopt;
    const auto& image = model.images[imageIndex];
    if (image.width <= 0 || image.height <= 0 ||
        image.rgba.size() / 4 != static_cast<std::size_t>(image.width) * image.height ||
        image.rgba.size() % 4 != 0) return std::nullopt;
    const auto& channels = linearChannels();
    glm::dvec3 sum(0);
    std::size_t count = 0;
    for (std::size_t pixel = 0; pixel < image.rgba.size(); pixel += 4) {
        const float alpha = image.rgba[pixel + 3] / 255.0f * material.baseColor.a;
        if (material.alphaMode == "MASK" && alpha < material.alphaCutoff) continue;
        for (int channel = 0; channel < 3; ++channel) sum[channel] += channels[image.rgba[pixel + channel]];
        ++count;
    }
    if (!count) return std::nullopt;
    return glm::vec3(sum / static_cast<double>(count)) * glm::vec3(material.baseColor);
}

std::vector<Material> matchTreeTextureColors(const Model& model, glm::vec3 target) {
    auto materials = model.materials;
    for (auto& material : materials) {
        const auto mean = visibleTextureColor(model, material);
        if (!mean || !material.unlit || material.alphaMode != "MASK") continue;
        for (int channel = 0; channel < 3; ++channel) {
            // Multiplication cannot create a channel absent from the source.
            if ((*mean)[channel] > 1e-8f)
                material.baseColor[channel] *= target[channel] / (*mean)[channel];
        }
    }
    return materials;
}
} // namespace viewer
