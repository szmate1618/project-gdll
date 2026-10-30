#pragma once
#include "character_canvas.hpp"
#include <filesystem>
#include <string>

namespace multiplayer {
// Defaults to <executable directory>/userdata/character.json. A nonempty
// GD_CHARACTER_PROFILE overrides this path and the library's parent directory.
std::filesystem::path characterProfilePath();
// Versioned local appearance, including the independent paint layers. A missing
// file returns false; invalid saves throw without changing the destination.
bool loadCharacterProfile(const std::filesystem::path& path, CharacterCanvas& canvas, std::string& faceId, std::string* name = nullptr);
// Validate a temporary file before atomically replacing the previous save.
void saveCharacterProfile(const std::filesystem::path& path, const CharacterCanvas& canvas, const std::string& faceId, const std::string& name = {});
// Same-directory atomic replacement, including an existing Windows destination.
void replaceCharacterFile(const std::filesystem::path& temporary, const std::filesystem::path& destination);
} // namespace multiplayer
