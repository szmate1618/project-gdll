#pragma once

#include "player_health.hpp"

#include <algorithm>
#include <cmath>

namespace viewer {

// Health and long-term weariness. Movement owns stamina and the separate strain
// stat in PlayerExertion so prediction can restore them without restoring health.
class PlayerVitals {
public:
    static constexpr float maximumHealth = PlayerHealth::maximumHealth;

    float health() const { return health_.health(); }
    PlayerHealth& healthState() { return health_; }
    const PlayerHealth& healthState() const { return health_; }
    float weariness() const { return weariness_; }
    bool bleeding() const { return bleeding_ && health() > 0; }
    void setBleeding(bool bleeding) { bleeding_ = bleeding; }

    // Returns health actually lost, including fractional damage. Invalid damage
    // has no effect, and damage to an already depleted player returns zero.
    float applyDamage(float amount) {
        if (!std::isfinite(amount) || amount <= 0) return 0;
        const float previous = health();
        health_.setHealth(previous - std::min(previous, amount));
        if (health() < previous) bleeding_ = true;
        return previous - health();
    }

private:
    PlayerHealth health_;
    float weariness_ = 0.f;
    bool bleeding_ = false;
};

} // namespace viewer
