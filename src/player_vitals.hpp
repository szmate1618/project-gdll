#pragma once

#include <algorithm>
#include <cmath>

namespace viewer {

// Simulation-owned player values. Stamina and weariness are stored for future
// mechanics; actual health loss also starts scent-producing bleeding.
class PlayerVitals {
public:
    static constexpr float maximumHealth = 100.f;

    float health() const { return health_; }
    float stamina() const { return stamina_; }
    float weariness() const { return weariness_; }
    bool bleeding() const { return bleeding_ && health_ > 0; }
    void setBleeding(bool bleeding) { bleeding_ = bleeding; }

    // Returns health actually lost, including fractional damage. Invalid damage
    // has no effect, and damage to an already depleted player returns zero.
    float applyDamage(float amount) {
        if (!std::isfinite(amount) || amount <= 0) return 0;
        const float previous = health_;
        health_ -= std::min(health_, amount);
        if (health_ < previous) bleeding_ = true;
        return previous - health_;
    }

private:
    float health_ = maximumHealth;
    float stamina_ = 100.f;
    float weariness_ = 0.f;
    bool bleeding_ = false;
};

} // namespace viewer
