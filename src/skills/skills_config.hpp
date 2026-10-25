#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace viewer {
struct MeleeSkill {
    std::string id, name;
};
// Presentation catalog only. Definitions may be empty; no combat behavior is loaded.
// Only skills listed in root/skills.toml are returned, ordered by ID.
std::vector<MeleeSkill> loadMeleeSkills(const std::filesystem::path& resourceRoot);
} // namespace viewer
