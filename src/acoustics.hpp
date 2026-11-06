#pragma once

#include <glm/vec3.hpp>
#include <filesystem>
#include <string>
#include <vector>
#include <optional>

namespace viewer {
struct NoiseEvent;
enum class AcousticMaterial { metal, wood, glass, rock, soft };
inline constexpr unsigned acousticMaterialCount = 5;

struct AcousticConfig {
    AcousticMaterial material = AcousticMaterial::wood;
    float hollowness = .25f; // 0 solid, 1 strongly resonant.
    float size = 1; // Relative acoustic size: larger lowers pitch and resonance.
    float gain = .65f;
    float minSpeed = .65f; // Normal approach speed in m/s, excluding resting contacts.
    float noiseDb = 45; // Gameplay source level at 1 m for a nominal 1 J impact.
};

struct ImpactSound {
    glm::vec3 position{0}; // World X east/Y up/Z south, metres.
    AcousticMaterial material = AcousticMaterial::wood;
    bool hardSurface = false;
    float sharpness = 0, hollowness = 0, size = 1, gain = 1;
};

bool validImpactSound(const ImpactSound& sound);
AcousticConfig loadAcousticConfig(const std::filesystem::path& path);
std::vector<std::filesystem::path> acousticConfigFiles(const std::filesystem::path& storage);
std::string acousticConfigId(const std::filesystem::path& path);
ImpactSound makeImpactSound(const AcousticConfig& config, float massKg, float approachSpeed,
                           float scale, bool hardSurface, glm::vec3 position);
float impactNoiseDb(const AcousticConfig& config, float massKg, float approachSpeed);
// Reuse material impact playback for player-only bangs and breaking restraints.
// This conversion never emits gameplay noise or changes its audience.
std::optional<ImpactSound> noiseImpactSound(const NoiseEvent& noise);
} // namespace viewer
