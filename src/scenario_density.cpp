#include "scenario_density.hpp"

// The implementation is shared with the existing model image loader.
#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <memory>
#include <stdexcept>

namespace viewer {

ScenarioDensity::ScenarioDensity(const std::filesystem::path& path, const ZombieHordeConfig::Rectangle& bounds)
    : bounds_(bounds), coordinates_(bounds) {
    const auto invalid = [&](const char* detail) {
        throw std::runtime_error("Scenario density image '" + path.u8string() + "': " + detail);
    };
    // Require PNG content, rather than accepting another format renamed .png.
    std::ifstream file(path, std::ios::binary);
    std::array<unsigned char, 8> signature{};
    file.read(reinterpret_cast<char*>(signature.data()), signature.size());
    if (!file || signature != std::array<unsigned char, 8>{137,80,78,71,13,10,26,10})
        invalid("expected a readable grayscale PNG");
    int channels = 0;
    if (!stbi_info(path.u8string().c_str(), &width_, &height_, &channels))
        invalid("cannot read PNG dimensions");
    // Bound startup memory before decoding and building the cumulative weights.
    constexpr std::size_t maximumPixels = 16 * 1024 * 1024;
    if (width_ <= 0 || height_ <= 0 || std::size_t(width_) > maximumPixels / std::size_t(height_))
        invalid("image exceeds 16 million pixels");
    std::unique_ptr<stbi_us, decltype(&stbi_image_free)> pixels(
        stbi_load_16(path.u8string().c_str(), &width_, &height_, &channels, 0), stbi_image_free);
    if (!pixels) invalid("cannot decode grayscale PNG");
    const auto count = std::size_t(width_) * std::size_t(height_);
    if (channels < 1 || channels > 4 || count > maximumPixels) invalid("invalid PNG dimensions/channels");
    weights_.reserve(count);
    cumulative_.reserve(count);
    double sum = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const auto* pixel = pixels.get() + i * channels;
        if (channels >= 3 && (pixel[0] != pixel[1] || pixel[0] != pixel[2]))
            invalid("expected grayscale pixels (RGB channels must be equal)");
        if ((channels == 2 || channels == 4) && pixel[channels - 1] != 65535)
            invalid("alpha must be fully opaque");
        const auto weight = pixel[0];
        weights_.push_back(weight);
        sum += weight;
        cumulative_.push_back(sum);
    }
    if (!(sum > 0)) invalid("image has no positive density");
}

glm::vec2 ScenarioDensity::sample(double selection, double pixelX, double pixelZ) const {
    for (const auto value : {selection, pixelX, pixelZ})
        if (!std::isfinite(value) || value < 0 || value >= 1)
            throw std::invalid_argument("Scenario density samples require inputs in [0,1)");
    const auto found = std::upper_bound(cumulative_.begin(), cumulative_.end(), selection * cumulative_.back());
    const auto index = std::size_t(found - cumulative_.begin());
    return coordinates_.sample((double(index % width_) + pixelX) / width_,
                               (double(index / width_) + pixelZ) / height_);
}

bool ScenarioDensity::allows(glm::vec2 position) const {
    const double x = (double(position.x) - bounds_[0]) / (bounds_[2] - bounds_[0]);
    const double z = (double(position.y) - bounds_[1]) / (bounds_[3] - bounds_[1]);
    if (!std::isfinite(x) || !std::isfinite(z) || x < 0 || x > 1 || z < 0 || z > 1) return false;
    const auto column = std::min(std::size_t(x * width_), std::size_t(width_ - 1));
    const auto row = std::min(std::size_t(z * height_), std::size_t(height_ - 1));
    return weights_[row * width_ + column] != 0;
}

} // namespace viewer
