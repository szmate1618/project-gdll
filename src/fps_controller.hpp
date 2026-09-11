#pragma once

#include <glm/glm.hpp>

namespace viewer {

class CollisionWorld;

// Meters and seconds, in the same Y-up space as the loaded scene and Camera.
struct FPSConfig {
    float eyeHeight = 1.70F;
    float height = 1.80F;
    float radius = 0.30F;
    float walkSpeed = 3.0F;
    float runSpeed = 6.0F;
    float gravity = 9.81F;
    float jumpVelocity = 3.8F;
    float maxSlopeDegrees = 45.0F;
    float stepHeight = 0.30F;
    float groundSnap = 0.30F;
    float skin = 0.002F;
    float fixedDt = 1.0F / 120.0F;
    float terminalVelocity = 50.0F;
};

struct FPSInput {
    float forward = 0.0F;
    float right = 0.0F;
    float yawDegrees = -90.0F;
    bool run = false;
    bool jump = false;
};

class FPSController {
public:
    explicit FPSController(FPSConfig config = {});
    bool spawn(glm::vec3 preferredEye, const CollisionWorld& world);
    void teleport(glm::vec3 feet);
    void update(float dt, const FPSInput& input, const CollisionWorld& world);

    [[nodiscard]] glm::vec3 feet() const { return feet_; }
    [[nodiscard]] glm::vec3 eyePosition() const { return feet_ + glm::vec3(0, config_.eyeHeight, 0); }
    [[nodiscard]] bool grounded() const { return grounded_; }
    [[nodiscard]] float verticalVelocity() const { return verticalVelocity_; }
    [[nodiscard]] glm::vec3 groundNormal() const { return groundNormal_; }
    [[nodiscard]] const FPSConfig& config() const { return config_; }

private:
    void step(float dt, const FPSInput& input, const CollisionWorld& world, bool jump);
    bool resolve(const CollisionWorld& world, bool horizontal, glm::vec3 previous);
    bool snapToGround(const CollisionWorld& world, float distance);
    [[nodiscard]] bool walkable(glm::vec3 normal) const;

    FPSConfig config_;
    glm::vec3 feet_{0.0F};
    glm::vec3 groundNormal_{0.0F, 1.0F, 0.0F};
    float verticalVelocity_ = 0.0F;
    double accumulator_ = 0.0;
    bool grounded_ = false;
    bool jumpHeld_ = false;
    bool jumpQueued_ = false;
};

}  // namespace viewer
