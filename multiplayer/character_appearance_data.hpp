#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace multiplayer {
constexpr int characterTextureResolution = 256, characterTextureFaces = 6;
constexpr std::size_t characterTextureBytes = characterTextureFaces * characterTextureResolution * characterTextureResolution * 3;
constexpr std::size_t maxCharacterAppearanceBytes = characterTextureBytes + 1;
constexpr std::size_t characterAppearanceChunkBytes = 8192;
// Opaque RGB, row-major, top-left image origin. Sides match CharacterCanvas:
// front +Z, back -Z, right +X, left -X, top +Y, bottom -Y.
struct CharacterAppearance { std::vector<std::uint8_t> rgb; };
struct CharacterAppearanceChunk {
    std::uint32_t player = 0; // Uploads use zero; only the server assigns an ID.
    std::uint32_t totalBytes = 0, offset = 0;
    std::vector<std::uint8_t> bytes;
    // Server-only removal: nonzero player, zero total/offset, empty bytes.
};
void validateCharacterAppearanceChunk(const CharacterAppearanceChunk& chunk);
std::vector<std::uint8_t> encodeCharacterAppearance(const CharacterAppearance& appearance);
CharacterAppearance decodeCharacterAppearance(const std::vector<std::uint8_t>& encoded);
} // namespace multiplayer
