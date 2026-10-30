#include "character_appearance_data.hpp"
#include <algorithm>
#include <stdexcept>

namespace multiplayer {
void validateCharacterAppearanceChunk(const CharacterAppearanceChunk& chunk) {
    if (!chunk.totalBytes) {
        if (!chunk.player || chunk.offset || !chunk.bytes.empty()) throw std::runtime_error("Invalid character appearance removal");
        return;
    }
    if (chunk.totalBytes > maxCharacterAppearanceBytes || chunk.offset >= chunk.totalBytes ||
        chunk.offset % characterAppearanceChunkBytes ||
        chunk.bytes.size() != std::min(characterAppearanceChunkBytes, std::size_t(chunk.totalBytes - chunk.offset)))
        throw std::runtime_error("Invalid character appearance chunk");
}
std::vector<std::uint8_t> encodeCharacterAppearance(const CharacterAppearance& appearance) {
    const auto& pixels = appearance.rgb;
    if (pixels.size() != characterTextureBytes) throw std::runtime_error("Invalid character texture dimensions");
    std::vector<std::uint8_t> runs{1};
    for (std::size_t pixel = 0; pixel < pixels.size() / 3;) {
        const auto start = pixel++;
        while (pixel - start < 65535 && pixel < pixels.size() / 3 &&
               std::equal(pixels.begin() + start * 3, pixels.begin() + start * 3 + 3, pixels.begin() + pixel * 3)) ++pixel;
        const auto count = pixel - start;
        runs.push_back(std::uint8_t(count)); runs.push_back(std::uint8_t(count >> 8));
        runs.insert(runs.end(), pixels.begin() + start * 3, pixels.begin() + start * 3 + 3);
        if (runs.size() >= maxCharacterAppearanceBytes) {
            std::vector<std::uint8_t> raw{0}; raw.insert(raw.end(), pixels.begin(), pixels.end()); return raw;
        }
    }
    return runs;
}
CharacterAppearance decodeCharacterAppearance(const std::vector<std::uint8_t>& bytes) {
    if (bytes.empty() || bytes.size() > maxCharacterAppearanceBytes) throw std::runtime_error("Invalid character appearance size");
    CharacterAppearance result;
    if (bytes[0] == 0) {
        if (bytes.size() != maxCharacterAppearanceBytes) throw std::runtime_error("Invalid raw character texture size");
        result.rgb.assign(bytes.begin() + 1, bytes.end());
    } else if (bytes[0] == 1) {
        if ((bytes.size() - 1) % 5) throw std::runtime_error("Truncated character texture run");
        result.rgb.reserve(characterTextureBytes);
        for (std::size_t i = 1; i < bytes.size(); i += 5) {
            const auto count = unsigned(bytes[i]) | (unsigned(bytes[i + 1]) << 8);
            if (!count || count > (characterTextureBytes - result.rgb.size()) / 3)
                throw std::runtime_error("Invalid character texture run length");
            for (unsigned pixel = 0; pixel < count; ++pixel)
                result.rgb.insert(result.rgb.end(), bytes.begin() + i + 2, bytes.begin() + i + 5);
        }
        if (result.rgb.size() != characterTextureBytes) throw std::runtime_error("Incomplete character texture");
    } else throw std::runtime_error("Unknown character texture encoding");
    return result;
}
} // namespace multiplayer
