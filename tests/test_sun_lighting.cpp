#include "sun_lighting.hpp"
#include "visibility.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void checkCoverage(glm::vec3 eye, glm::vec3 sun) {
    const auto view = viewer::sunShadowView(eye, sun);
    const auto matrix = view.projectionView();
    // Sample the whole receiver sphere, rather than repeating the fit formula.
    for (int latitude = -90; latitude <= 90; latitude += 15)
        for (int longitude = 0; longitude < 360; longitude += 15) {
            const float pitch = glm::radians(static_cast<float>(latitude));
            const float yaw = glm::radians(static_cast<float>(longitude));
            const auto point = eye + viewer::SunLighting::shadowDistance *
                glm::vec3(std::cos(pitch) * std::cos(yaw), std::sin(pitch), std::cos(pitch) * std::sin(yaw));
            const auto clip = matrix * glm::vec4(point, 1);
            require(glm::all(glm::lessThanEqual(glm::abs(glm::vec3(clip) / clip.w), glm::vec3(1.0001f))),
                    "Shadow fit clipped a nearby receiver");
        }
    const viewer::Frustum frustum(matrix);
    const auto upstream = eye + glm::normalize(sun) * 200.0f;
    require(frustum.intersects(upstream - glm::vec3(1), upstream + glm::vec3(1)),
            "Offscreen upstream casters must remain in the light volume");
    const auto distant = eye + glm::normalize(sun) * 500.0f;
    require(!frustum.intersects(distant - glm::vec3(1), distant + glm::vec3(1)),
            "Distant casters must be culled");
}
}

int main() {
    try {
        checkCoverage({0, 1.7f, 0}, {-0.4f, 0.85f, 0.3f});
        checkCoverage({1234, 75, -2345}, {-0.4f, 0.85f, 0.3f});
        checkCoverage({0, 1.7f, 0}, {0, 1, 0});
        const auto initial = viewer::sunShadowView({0, 0, 0}, {-0.4f, 0.85f, 0.3f}).projectionView();
        const auto moved = viewer::sunShadowView({0.001f, 0, 0}, {-0.4f, 0.85f, 0.3f}).projectionView();
        const auto point = glm::vec4(17, 0, -23, 1);
        require(glm::length(glm::vec2(initial * point) - glm::vec2(moved * point)) < 1e-7f,
                "Sub-texel walking motion must not move shadow texture coordinates");
        bool rejected = false;
        try { viewer::sunShadowView({0, 0, 0}, {0, 0, 0}); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "Zero sun direction must be rejected");
        std::cout << "Sun lighting tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
