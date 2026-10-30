#pragma once
#include "model.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace multiplayer {
struct CharacterFace { std::string id, name; std::filesystem::path image; };
std::vector<CharacterFace> loadCharacterFaces(const std::filesystem::path& root);
viewer::ImageData loadCharacterFaceImage(const std::filesystem::path& path);
} // namespace multiplayer
