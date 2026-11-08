#pragma once

#include "player_health.hpp"
#include <algorithm>
#include <cmath>
#include <random>

namespace viewer {

class PlayerVitals {
public:
    static constexpr float maximumHealth = PlayerHealth::maximumHealth;
    float health() const { return health_.health(); }
    PlayerHealth& healthState() { return health_; }
    const PlayerHealth& healthState() const { return health_; }
    float weariness() const { return weariness_; }
    std::uint64_t damageEvents() const { return damageEvents_; }
    bool bleeding() const { return health_.bleeding(); }
    void advance(double seconds, const DayNightCycle& cycle, bool sleeping) { health_.advance(seconds, cycle, sleeping); }

    // Every successful attack creates its own bleeding cut at a random body
    // location. Systemic injuries are created explicitly, never by this path.
    float applyDamage(float amount) {
        if (!std::isfinite(amount) || amount <= 0) return 0;
        const float previous = health();
        amount = std::min(previous, amount);
        if (previous - amount == previous) return 0;
        const auto part = static_cast<InjuryBodyPart>(std::uniform_int_distribution<int>(
            0, static_cast<int>(InjuryBodyPart::None) - 1)(random_));
        health_.addInjury({part, InjuryType::Cut, amount, 0, 0, {true, false, false}});
        ++damageEvents_;
        return previous - health();
    }

private:
    PlayerHealth health_;
    std::uint64_t damageEvents_ = 0; // Direct hits only; healing and blood loss never increment this.
    float weariness_ = 0.f;
    std::mt19937 random_{std::random_device{}()};
};

} // namespace viewer
