#pragma once
#include "character_appearance_data.hpp"
#include <filesystem>
#include <string>

namespace multiplayer {
CharacterAppearance defaultCharacterAppearance();
// Same CPU compositor and face assets as the editor; no display/GL required.
CharacterAppearance loadSavedCharacterAppearance(const std::filesystem::path& root, const std::filesystem::path& profile,
    std::string* name = nullptr);
} // namespace multiplayer
