#pragma once
#include <array>
#include <cstdint>
#include <glm/glm.hpp>

namespace viewer {
enum class PlayerBodyPart : std::size_t {
    hip, lowerTorso, upperTorso, leftUpperArm, leftLowerArm, leftFist,
    rightUpperArm, rightLowerArm, rightFist, count
};
constexpr std::size_t playerBodyPartCount = static_cast<std::size_t>(PlayerBodyPart::count);

// A render-only snapshot in world meters (X east, Y up, Z south). Each matrix
// transforms a unit cube, including its dimensions. Eye is the carrying camera
// position; clients may translate the whole pose to their predicted eye.
struct PlayerBodyPose {
    std::array<glm::mat4, playerBodyPartCount> parts{};
    glm::vec3 eye{0};
    std::uint8_t striking = 0, returning = 0; // Left bit 0, right bit 1.
    bool enabled = false;
};
bool validPlayerBodyPose(const PlayerBodyPose& pose);
PlayerBodyPose interpolatePlayerBodyPose(const PlayerBodyPose& a, const PlayerBodyPose& b, float t);
} // namespace viewer
