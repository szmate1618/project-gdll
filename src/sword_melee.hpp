#pragma once
#include "weapon_motion.hpp"
#include <limits>

namespace viewer {
// Eye-local drag direction: X right, Y up. Negative elapsed means idle.
struct SwordPose {
    glm::vec2 direction{1, 0};
    float elapsed = -1;
};
constexpr float swordWindupSeconds = .09f;
constexpr float swordCutSeconds = .26f;
constexpr float swordDurationSeconds = .55f;
constexpr float swordBladeBase = 0.f, swordBladeTip = 1.04f;

// Hold and drag 12 fresh mouse pixels after recovery for each cut. Releasing
// does not interrupt a cut; cancellation requires a real release to rearm.
class SwordMelee {
public:
    void trigger(bool held);
    void drag(glm::vec2 pixels);
    bool update(double seconds); // True once when a drag starts a cut.
    void cancel();
    SwordPose pose(double seconds) const;
    double startedAt() const { return startedAt_; }
private:
    bool held_ = false, armed_ = false;
    glm::vec2 drag_{0}, direction_{1, 0};
    double startedAt_ = -std::numeric_limits<double>::infinity();
};
glm::mat4 swordRestPose();
WeaponMotionPose swordMotion(const SwordPose& pose);
glm::mat4 swordTransform(const SwordPose& pose);
} // namespace viewer
