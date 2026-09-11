#pragma once

#include "camera.hpp"
#include "collision_world.hpp"
#include "fps_controller.hpp"

#include <optional>

namespace viewer {

enum class CameraMode { FreeFly, Walking };

// Mode transitions and camera/controller synchronization live outside GLFW
// callbacks. Rendering only sees a Camera, never player physics or GIS data.
class CameraRig {
public:
    CameraRig(const CollisionWorld& world, glm::vec3 minimum, glm::vec3 maximum, float aspect);
    bool enterWalking(std::optional<glm::vec3> preferredEye = std::nullopt);
    void enterFreeFly();
    void frame(float aspect);
    void update(float dt, float forward, float right, float up, bool run, bool jump);
    Camera& camera() { return camera_; }
    const Camera& camera() const { return camera_; }
    const FPSController& player() const { return player_; }
    CameraMode mode() const { return mode_; }
    bool walking() const { return mode_ == CameraMode::Walking; }
    bool hasPlayer() const { return hasPlayer_; }
    const char* modeName() const { return walking() ? "FPS walking" : "Free-fly"; }
    float speed(bool run = false) const;

private:
    const CollisionWorld& world_;
    glm::vec3 minimum_, maximum_;
    Camera camera_;
    FPSController player_;
    CameraMode mode_ = CameraMode::FreeFly;
    bool hasPlayer_ = false;
};

} // namespace viewer
