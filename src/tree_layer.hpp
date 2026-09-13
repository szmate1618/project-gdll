#pragma once

#include "model.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace viewer {

struct TreeAsset {
    std::string id;
    std::filesystem::path path;
    Model model;
};

struct TreeInstance {
    std::string id;
    std::size_t asset = 0;
    glm::mat4 transform{1.0f};
    glm::vec3 boundsMin{0.0f}, boundsMax{0.0f};
    glm::vec3 dimensions{1.0f}; // Crown width, visible height, crown depth (meters).
};

struct TreeLayer {
    std::filesystem::path source;
    std::vector<TreeAsset> assets;
    std::vector<TreeInstance> instances;
};

// Validates the versioned instance manifest and map/asset SHA-256 fingerprints.
// Assets are loaded once; authoritative column-major instance matrices are kept.
// Throws a contextual std::runtime_error on incompatible or malformed data.
TreeLayer loadTreeLayer(const std::filesystem::path& manifest,
                       const std::filesystem::path& map);

} // namespace viewer
