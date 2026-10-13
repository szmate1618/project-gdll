#pragma once

#include "atmosphere.hpp"
#include "night_lighting.hpp"
#include "sun_lighting.hpp"

namespace viewer {

// Game hours in [0, 24); a full day takes dayLengthSeconds of simulation time.
// Zero freezes the clock. This clock is independent of animation capture time.
struct DayNightCycle {
    double startHour = 12.0;
    double dayLengthSeconds = 1200.0;
    double hourAt(double elapsedSeconds) const;
};

// Linear RGB colors in the runtime frame (X east, Y up, Z south).
struct DayNightLighting {
    SunLighting sun;
    glm::vec3 fogColor{Atmosphere{}.color};
    glm::vec3 unlitColor{1.0f};
    float nightBlend = 0.0f; // Also fades the camera flashlight and rain lighting.
};

// Stylized east-to-west sun path: sunrise at 06:00, sunset at 18:00.
// Direct light fades out between 12 and 4 degrees above the horizon, bounding
// the visible long-shadow problem without expanding the local shadow volume.
DayNightLighting dayNightLighting(double hour, const SunLighting& day = {},
                                 const NightLighting& night = {}, const Atmosphere& atmosphere = {});

} // namespace viewer
