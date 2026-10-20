#include "sun_lighting.hpp"
#include "visibility.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void checkCoverage(glm::vec3 eye, glm::vec3 sun,int resolution) {
    const auto view = viewer::sunShadowView(eye, sun,resolution);
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
        for(const auto resolution:{1024,2048}) {
            checkCoverage({0, 1.7f, 0}, {-0.4f, 0.85f, 0.3f},resolution);
            checkCoverage({1234, 75, -2345}, {-0.4f, 0.85f, 0.3f},resolution);
            checkCoverage({0, 1.7f, 0}, {0, 1, 0},resolution);
            checkCoverage({1234, 75, -2345}, {1, 0.07f, 0.02f},resolution);
            checkCoverage({1234, 75, -2345}, {-1, 0.07f, 0.02f},resolution);
            // This displacement advances exactly one texel in either grid.
            // Checking normalized image movement detects a stale resolution.
            const auto a=viewer::sunShadowView({0,0,0},{0,1,0},resolution).projectionView()*glm::vec4(0,0,0,1);
            const auto b=viewer::sunShadowView({.16f,0,0},{0,1,0},resolution).projectionView()*glm::vec4(0,0,0,1);
            require(std::abs(glm::length(glm::vec2(b-a))*resolution*.5f-1)<.0001f,
                    "Shadow snapping does not follow selected pixel resolution");
        }
        const auto initial = viewer::sunShadowView({0, 0, 0}, {-0.4f, 0.85f, 0.3f}).projectionView();
        const auto moved = viewer::sunShadowView({0.001f, 0, 0}, {-0.4f, 0.85f, 0.3f}).projectionView();
        const auto point = glm::vec4(17, 0, -23, 1);
        require(glm::length(glm::vec2(initial * point) - glm::vec2(moved * point)) < 1e-7f,
                "Sub-texel walking motion must not move shadow texture coordinates");
        bool rejected = false;
        try { viewer::sunShadowView({0, 0, 0}, {0, 0, 0}); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "Zero sun direction must be rejected");
        rejected=false;try{viewer::sunShadowView({0,0,0},{0,1,0},0);}catch(const std::invalid_argument&){rejected=true;}
        require(rejected,"Invalid shadow resolution accepted");
        std::cout << "Sun lighting tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
