#include "day_night.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template<class Function>
void rejects(Function function) {
    bool rejected = false;
    try { function(); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "Invalid clock inputs must be rejected");
}

void checkClock() {
    viewer::DayNightCycle clock{23.0, 1200.0};
    require(clock.hourAt(0) == 23.0, "Clock must start at the requested hour");
    require(clock.hourAt(100) == 1.0, "Clock must wrap through midnight");
    require(clock.hourAt(600) == 11.0, "Half a cycle must advance twelve hours");
    require(clock.hourAt(1200) == 23.0 && clock.hourAt(1200000100) == 1.0,
            "Whole days must wrap without losing the start hour");
    clock.dayLengthSeconds = 0;
    require(clock.hourAt(1000000) == 23.0, "Zero duration must freeze the requested hour");
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    for (const auto hour : {-1.0, 24.0, nan}) {
        rejects([&] { viewer::DayNightCycle{hour, 1200}.hourAt(0); });
        rejects([&] { viewer::dayNightLighting(hour); });
    }
    for (const auto duration : {-1.0, 0.5, nan})
        rejects([&] { viewer::DayNightCycle{12, duration}.hourAt(0); });
    rejects([&] { clock.hourAt(-1); });
    rejects([&] { clock.hourAt(nan); });
}

void checkLighting() {
    const viewer::SunLighting day;
    const viewer::NightLighting night;
    const viewer::Atmosphere atmosphere;
    const auto noon = viewer::dayNightLighting(12);
    const auto midnight = viewer::dayNightLighting(0);
    require(glm::length(noon.sun.color - day.color) < 1e-6f && noon.nightBlend == 0,
            "Noon must retain full daylight and turn off the flashlight");
    require(glm::length(noon.fogColor - atmosphere.color) < 1e-6f &&
            glm::length(noon.unlitColor - glm::vec3(1)) < 1e-6f,
            "Noon sky and unlit tree colors must retain daylight settings");
    require(glm::length(midnight.sun.color) == 0 && midnight.nightBlend == 1 &&
            glm::length(midnight.sun.skyAmbient - night.skyAmbient) < 1e-6f &&
            glm::length(midnight.sun.groundAmbient - night.groundAmbient) < 1e-6f &&
            glm::length(midnight.unlitColor - night.skyAmbient) < 1e-6f &&
            glm::length(midnight.fogColor - night.fogColor) < 1e-6f,
            "Midnight must match fixed night without direct sunlight");
    const auto sunrise = viewer::dayNightLighting(6);
    const auto sunset = viewer::dayNightLighting(18);
    require(sunrise.sun.direction.x > 0.99f && sunset.sun.direction.x < -0.99f &&
            noon.sun.direction.y > 0.9f && noon.sun.direction.z > 0,
            "Sun must rise in the east, pass overhead to the south, and set in the west");
    require(glm::length(sunrise.sun.color) == 0 && glm::length(sunset.sun.color) == 0,
            "The horizon must retain twilight without direct light or its shadow pass");
    require(sunset.nightBlend > 0 && sunset.nightBlend < 1 &&
            sunset.unlitColor.r > midnight.unlitColor.r && sunset.unlitColor.r < noon.unlitColor.r,
            "Twilight must blend trees and the flashlight rather than switch them");
    const auto low = viewer::dayNightLighting(6.25);
    const auto fading = viewer::dayNightLighting(6.5);
    const auto high = viewer::dayNightLighting(7);
    require(glm::length(low.sun.color) == 0 && glm::length(fading.sun.color) > 0 &&
            glm::length(fading.sun.color) < glm::length(high.sun.color),
            "Very long shadows must fade out before the sun reaches the horizon");
    require(fading.sun.color.r / day.color.r > fading.sun.color.b / day.color.b,
            "Low sunlight must have a warmer color than daylight");
}

void checkContinuity() {
    auto previous = viewer::dayNightLighting(0);
    // Sample the full day, including both sides of each fade threshold and
    // midnight. Adjacent frames must not switch lights, trees, sky, or fog.
    for (int second = 1; second <= 86400; ++second) {
        const auto current = viewer::dayNightLighting(second == 86400 ? 0.0 : second / 3600.0);
        require(std::abs(glm::length(current.sun.direction) - 1.0f) < 1e-5f,
                "Sun direction must remain finite and normalized all day");
        require(glm::length(current.sun.direction - previous.sun.direction) < 0.001f &&
                glm::length(current.sun.color - previous.sun.color) < 0.002f &&
                glm::length(current.fogColor - previous.fogColor) < 0.002f &&
                glm::length(current.unlitColor - previous.unlitColor) < 0.002f &&
                std::abs(current.nightBlend - previous.nightBlend) < 0.002f,
                "Lighting must be continuous across the full day");
        previous = current;
    }
}
}

int main() {
    try {
        checkClock();
        checkLighting();
        checkContinuity();
        std::cout << "Day/night tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
