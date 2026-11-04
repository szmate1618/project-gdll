#pragma once

#include "player_noise.hpp"
#include "collision_box.hpp"

#include <glm/glm.hpp>
#include <optional>

namespace viewer {

class CollisionWorld;
struct Contact;

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
    float crouchHeight = 1.10F, proneHeight = .45F;
    float crouchSpeed = 1.5F, proneSpeed = .75F;
    // Prone head/torso box, independent of the upright capsule radius.
    float proneEyeHeight = .35F, proneWidth = .60F, proneLength = 1.0F;
    // Adjacent stance durations: standing/crouch, then crouch/prone, seconds.
    float crouchTransitionSeconds = .25F, proneTransitionSeconds = .50F;
};

enum class PlayerStance { Standing, Crouched, Prone };

struct FPSInput {
    float forward = 0.0F;
    float right = 0.0F;
    float yawDegrees = -90.0F;
    bool run = false;
    bool jump = false;
    bool crouch = false; // Held; airborne this tucks legs without moving eyes.
    bool toggleProne = false; // One press event, not the held key state.
};

// Complete movement state for network reconciliation; positions denote feet.
struct FPSState {
    glm::vec3 feet{0}, groundNormal{0, 1, 0};
    float verticalVelocity = 0;
    double accumulator = 0;
    PlayerStance stance = PlayerStance::Standing, takeoffStance = PlayerStance::Standing;
    bool grounded = false, jumpHeld = false, jumpQueued = false;
    bool proneRequested = false, proneQueued = false;
    float bodyYawDegrees = -90; // Collision orientation can lag view yaw near obstacles.
    float posture = 0; // Continuous pose: 0 standing, 1 crouched, 2 prone.
};

class FPSController {
public:
    explicit FPSController(FPSConfig config = {});
    bool spawn(glm::vec3 preferredEye, const CollisionWorld& world);
    // Preserve the requested eye position and start airborne if the capsule
    // fits and a scene surface exists below. Failure leaves this player intact.
    bool startFalling(glm::vec3 eye, const CollisionWorld& world);
    void teleport(glm::vec3 feet);
    void update(float dt, const FPSInput& input, const CollisionWorld& world);
    FPSState movementState() const;
    void restoreMovement(const FPSState& state);

    [[nodiscard]] glm::vec3 feet() const { return feet_; }
    [[nodiscard]] glm::vec3 eyePosition() const { return feet_ + glm::vec3(0, eyeHeight(), 0); }
    [[nodiscard]] float bodyHeight() const;
    [[nodiscard]] float eyeHeight() const;
    // Incoming capsule-shaped damage queries retain a valid low approximation.
    [[nodiscard]] float bodyRadius() const;
    [[nodiscard]] std::optional<CollisionBox> collisionBox() const;
    [[nodiscard]] PlayerStance stance() const { return stance_; }
    [[nodiscard]] float movementSpeed(bool run = false) const;
    [[nodiscard]] bool grounded() const { return grounded_; }
    [[nodiscard]] float verticalVelocity() const { return verticalVelocity_; }
    [[nodiscard]] glm::vec3 groundNormal() const { return groundNormal_; }
    [[nodiscard]] const FPSConfig& config() const { return config_; }
    // Noise events produced during the latest update; never plays audio.
    [[nodiscard]] const std::vector<NoiseEvent>& noises() const { return noises_; }

private:
    void step(float dt, const FPSInput& input, const CollisionWorld& world, bool jump);
    bool resolve(const CollisionWorld& world, bool horizontal, glm::vec3 previous);
    bool snapToGround(const CollisionWorld& world, float distance);
    bool tryStep(glm::vec3 previous, glm::vec3 requested, const CollisionWorld& world);
    void turnBody(float yawDegrees, const CollisionWorld& world);
    [[nodiscard]] CollisionBox proneBox(glm::vec3 feet, float yawDegrees, float posture) const;
    [[nodiscard]] std::vector<Contact> contactsAt(glm::vec3 feet, float posture,
        const CollisionWorld& world, float skin = 0) const;
    [[nodiscard]] std::optional<std::pair<float, glm::vec3>> proneSupport(
        glm::vec3 feet, float posture, const CollisionWorld& world, float minY, float maxY) const;
    [[nodiscard]] bool walkable(glm::vec3 normal) const;
    [[nodiscard]] float stanceHeight(PlayerStance stance) const;
    [[nodiscard]] float postureHeight(float posture) const;
    [[nodiscard]] float postureEyeHeight(float posture, float yawDegrees) const;
    [[nodiscard]] glm::vec2 groundSlope() const;
    bool changePosture(float posture, const CollisionWorld& world);
    void updateStance(float dt, bool crouch, const CollisionWorld& world);

    FPSConfig config_;
    glm::vec3 feet_{0.0F};
    glm::vec3 groundNormal_{0.0F, 1.0F, 0.0F};
    float verticalVelocity_ = 0.0F;
    double accumulator_ = 0.0;
    bool grounded_ = false;
    bool jumpHeld_ = false;
    bool jumpQueued_ = false;
    PlayerStance stance_ = PlayerStance::Standing;
    PlayerStance takeoffStance_ = PlayerStance::Standing; // A leg tuck preserves airborne movement speed.
    bool proneRequested_ = false, proneQueued_ = false;
    float bodyYawDegrees_ = -90;
    float posture_ = 0;
    PlayerNoiseEmitter noiseEmitter_;
    std::vector<NoiseEvent> noises_;
};

}  // namespace viewer
