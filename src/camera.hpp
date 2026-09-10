#pragma once

#include <glm/glm.hpp>

namespace viewer {

// A Y-up free-fly camera. Imported scene coordinates are interpreted as meters.
class Camera {
public:
    void frame(glm::vec3 minimum, glm::vec3 maximum, float aspect);
    [[nodiscard]] glm::mat4 view() const;
    [[nodiscard]] glm::mat4 projection(float aspect) const;

    // Inputs are signed axes; simultaneous movement is normalized. Up is world Y.
    void move(float forward, float right, float up, float dt, bool fast);
    // Mouse offsets are in pixels. Positive dy looks up.
    void look(float dx, float dy);
    void adjustSpeed(float multiplier);

    [[nodiscard]] glm::vec3 position() const { return position_; }
    [[nodiscard]] float speed() const { return speed_; }

private:
    [[nodiscard]] glm::vec3 forward() const;

    glm::vec3 position_{0.0F, 3.0F, 6.0F};
    float yaw_ = -90.0F;
    float pitch_ = -20.0F;
    float speed_ = 10.0F;
    float near_ = 0.1F;
    float far_ = 10000.0F;
    float verticalFov_ = 60.0F;
};

}  // namespace viewer
