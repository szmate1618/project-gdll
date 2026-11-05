#pragma once

#include "player_health.hpp"

#include <algorithm>
#include <cmath>

namespace viewer {

// Injury-only multipliers. Movement selects the lower multiplier of these and
// stamina. Kept in movement checkpoints so replay does not need medical records.
struct PlayerInjuryMovement {
    float speed = 1;
    float forwardAcceleration = 1;
    float leftAcceleration = 1;
    float rightAcceleration = 1;

    bool valid() const {
        const auto within = [](float value, float minimum) {
            return std::isfinite(value) && value >= minimum && value <= 1;
        };
        return within(speed, .75f) && within(forwardAcceleration, .5f) &&
            within(leftAcceleration, .5f) && within(rightAcceleration, .5f);
    }
};

inline PlayerInjuryMovement playerInjuryMovement(const PlayerHealth& health) {
    struct Leg {
        float pain = 0, weakness = 0;
        float penalty(float tolerance) const { return std::max(weakness, std::max(0.f, pain - tolerance / 5)); }
        void include(const PlayerInjury& injury) {
            pain = std::max(pain, injury.pain());
            weakness = std::max(weakness, injury.weakness());
        }
    } left, right;
    for (const auto& injury : health.injuries()) {
        switch (injury.bodyPart()) {
        case InjuryBodyPart::LeftFoot:
        case InjuryBodyPart::LeftLowerLeg:
        case InjuryBodyPart::LeftUpperLeg:
        case InjuryBodyPart::LeftAnkle:
        case InjuryBodyPart::LeftKnee:
        case InjuryBodyPart::LeftHip:
            left.include(injury);
            break;
        case InjuryBodyPart::RightFoot:
        case InjuryBodyPart::RightLowerLeg:
        case InjuryBodyPart::RightUpperLeg:
        case InjuryBodyPart::RightAnkle:
        case InjuryBodyPart::RightKnee:
        case InjuryBodyPart::RightHip:
            right.include(injury);
            break;
        default: break;
        }
    }
    const float leftPenalty = left.penalty(health.painTolerance());
    const float rightPenalty = right.penalty(health.painTolerance());
    const float both = std::max(leftPenalty, rightPenalty);
    return {1 - both / 4, 1 - both / 2, 1 - rightPenalty / 2, 1 - leftPenalty / 2};
}

} // namespace viewer
