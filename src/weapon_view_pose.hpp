#pragma once

#include "model.hpp"

namespace viewer {

// Standing dimensions in meters, even when the camera is crouched or prone.
// A stance moves the eyes; it must not shrink the player's arms or shoulders.
struct WeaponViewBody {
    float height = 1.80f;
    float eyeHeight = 1.70f;
};

// Rigid model-to-eye transform: camera-local X right, Y up, forward -Z.
// Handguns anchor at the palm; stocked weapons anchor at the shoulder pocket.
glm::mat4 weaponViewPose(const Model& model, bool handgun, WeaponViewBody body);

} // namespace viewer
