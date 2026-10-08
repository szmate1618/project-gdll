#pragma once

#include <glm/glm.hpp>

namespace viewer {

// Uniform distance haze in the runtime meter frame. Color is linear RGB;
// density is inverse meters. A clear foreground preserves FPS visibility.
struct Atmosphere {
    glm::vec3 color{0.42f, 0.50f, 0.60f};
    float startDistance = 30.0f;
    float density = 0.0008f;

    // Match the background to the distant haze after the shader's sRGB encode.
    glm::vec3 backgroundSrgb() const {
        const auto linear = glm::max(color, glm::vec3(0));
        return glm::mix(12.92f * linear,
                        1.055f * glm::pow(linear, glm::vec3(1.0f / 2.4f)) - 0.055f,
                        glm::greaterThanEqual(linear, glm::vec3(0.0031308f)));
    }
};

} // namespace viewer
