#pragma once

#include <array>
#include <memory>
#include <glm/glm.hpp>
#include <box3d/id.h>

namespace viewer {
class PhysicsWorld;
enum class PlayerBodyPart : std::size_t {
    hip, lowerTorso, upperTorso, leftUpperArm, leftLowerArm, leftFist,
    rightUpperArm, rightLowerArm, rightFist, count
};
constexpr std::size_t playerBodyPartCount = static_cast<std::size_t>(PlayerBodyPart::count);

// Rigid boxes in world meters, attached to a walking/camera-carried anchor.
// Standing eye height determines anatomy; crouching never scales the body.
// Arms lock in guard without springs until their own punch is thrown.
class PlayerBody {
public:
    PlayerBody(std::shared_ptr<PhysicsWorld> world, float standingEyeHeight);
    ~PlayerBody();
    PlayerBody(const PlayerBody&) = delete;
    PlayerBody& operator=(const PlayerBody&) = delete;
    void follow(glm::vec3 eye, glm::vec3 forward, bool enabled);
    void beforeStep(float seconds);
    // Read contacts after melee damage has consumed them, then start returning
    // any striking arm that hit something or reached its extension limit.
    void afterStep();
    void punch(glm::vec3 direction, const glm::vec3* target = nullptr);
    bool punching(PlayerBodyPart part) const;
    // Includes the non-damaging 0.1 s pause before every motor retraction.
    bool returning(PlayerBodyPart part) const;
    // Preserve a solved contact when melee handoff removes its zombie mesh
    // before afterStep() reads the remaining Box3D contacts.
    void recordArmContact(PlayerBodyPart part);
    bool enabled() const;
    b3BodyId body(PlayerBodyPart part) const;
    glm::mat4 transform(PlayerBodyPart part) const; // Includes full box size.
    glm::vec3 fistPosition(bool right) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace viewer
