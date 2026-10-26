#pragma once

#include <algorithm>
#include <cmath>

namespace viewer {

// Simulation-owned player values. Stamina and weariness are stored for future
// mechanics; applying damage changes health only.
class PlayerVitals {
public:
    static constexpr float maximumHealth = 100.f;

    float health() const { return health_; }
    float stamina() const { return stamina_; }
    float weariness() const { return weariness_; }

    // Returns health actually lost, including fractional damage. Invalid damage
    // has no effect, and damage to an already depleted player returns zero.
    float applyDamage(float amount) {
        if (!std::isfinite(amount) || amount <= 0) return 0;
        const float previous = health_;
        health_ -= std::min(health_, amount);
        return previous - health_;
    }

private:
    float health_ = maximumHealth;
    float stamina_ = 100.f;
    float weariness_ = 0.f;
};

} // namespace viewer
