#pragma once
#include "zombie_layer.hpp"
#include <filesystem>

namespace multiplayer {
// Validate the actual GLB JSON, not just the extension. Data URIs and the GLB's
// embedded BIN chunk are self-contained; filesystem/network URIs are not.
void validateSessionGlb(const std::filesystem::path& path);
void validateSessionZombies(const viewer::ZombieLayer& zombies);
} // namespace multiplayer
