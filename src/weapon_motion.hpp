#pragma once

#include "camera.hpp"
#include <array>
#include <cstddef>
#include <limits>

namespace viewer {

struct FPSConfig;

// Breathing amplitude relative to stationary crouch. Speed is horizontal m/s;
// posture is the continuous movement pose: 0 standing, 1 crouched, 2 prone.
float sniperSwayScale(float speed, float posture, const FPSConfig& movement);

struct WeaponMotionPose {
    glm::vec3 offset{0}; // Eye-local meters: X right, Y up, +Z rearward.
    glm::vec3 angles{0}; // Model-local pitch, yaw, roll in degrees, about the receiver.
    float pumpTravel = 0; // Model-local meters toward +Z; only the shotgun pump moves.
};

// Client-local presentation, sampled from absolute seconds rather than integrated
// per frame. Slots match weaponIds; grenades have no firearm motion.
class WeaponMotion {
public:
    void fire(std::size_t slot, double seconds); // Successful shots only, once per shell.
    WeaponMotionPose pose(std::size_t slot, double seconds, bool lowered = false) const;
    // Returns a copy: movement/mouse orientation never accumulate visual offsets.
    // Scoped render and shot queries must use this same camera; hip aim is unchanged.
    Camera aimCamera(const Camera& input, std::size_t slot, double seconds, bool combat,
                     float swayScale = 1) const;
private:
    struct Shot {
        double seconds = -std::numeric_limits<double>::infinity();
        float strength = 0;
    };
    std::array<Shot, 4> shots_{};
};

glm::mat4 animatedWeaponPose(const glm::mat4& rest, const WeaponMotionPose& motion);

} // namespace viewer
