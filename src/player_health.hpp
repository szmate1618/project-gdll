#pragma once

#include "player_injury.hpp"

#include <cmath>
#include <stdexcept>
#include <vector>

namespace viewer {

// Player-owned health and medical records. Movement derives its penalties
// separately; PlayerVitals retains combat damage and reserved weariness.
class PlayerHealth {
public:
    static constexpr float maximumHealth = 100.f;

    float health() const { return health_; }
    // Normalized percentage (1 = 100%). Movement subtracts tolerance / 5 from pain.
    float painTolerance() const { return painTolerance_; }
    const std::vector<PlayerInjury>& injuries() const { return injuries_; }

    void setHealth(float health) {
        if (!std::isfinite(health) || health < 0 || health > maximumHealth)
            throw std::invalid_argument("Player health must be a finite value between 0 and 100");
        health_ = health;
    }
    void setPainTolerance(float tolerance) {
        if (!std::isfinite(tolerance) || tolerance < 0 || tolerance > 1)
            throw std::invalid_argument("Pain tolerance must be a finite value between 0 and 1");
        painTolerance_ = tolerance;
    }
    void addInjury(const PlayerInjury& injury) { injuries_.push_back(injury); }

private:
    float health_ = maximumHealth;
    float painTolerance_ = 0;
    std::vector<PlayerInjury> injuries_;
};

} // namespace viewer
