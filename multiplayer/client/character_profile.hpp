#pragma once
#include "character_canvas.hpp"
#include <filesystem>
#include <string>

namespace multiplayer {
std::filesystem::path characterProfilePath();
// Versioned local appearance, including the independent paint layers. A missing
// file returns false; invalid saves throw without changing the destination.
bool loadCharacterProfile(const std::filesystem::path& path, CharacterCanvas& canvas, std::string& faceId);
// Validate a temporary file before atomically replacing the previous save.
void saveCharacterProfile(const std::filesystem::path& path, const CharacterCanvas& canvas, const std::string& faceId);
} // namespace multiplayer
