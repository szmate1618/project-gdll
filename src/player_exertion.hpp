#pragma once

#include "day_night_clock.hpp"

#include <algorithm>
#include <cmath>

namespace viewer {

// Rates use normalized stamina/strain per simulation second unless noted. Speeds and
// accelerations remain FPSConfig's responsibility.
struct PlayerExertionConfig {
    float runningDrain = .04f;
    float strainRate = 1.f / 300;
    float strainRecoveryPerDay = .75f; // Normalized units per in-game day.
    float sleepingStrainRecoveryMultiplier = 2;
    double dayLengthSeconds = DayNightCycle{}.dayLengthSeconds; // Zero freezes strain recovery.
    float walkingRecovery = 1.f / 120;
    float crouchedRecoveryMultiplier = 2;
    float seatedRecoveryMultiplier = 2;
    float proneRecoveryMultiplier = 4;
    float loadDecaySeconds = 2;
    float loadDrainMultiplier = .5f;
    float directionLoad = .275f; // Full-speed reversal adds 2 * .275.
    float jumpLoad = .40f;
    float landingLoad = .15f;
};

// Part of the movement checkpoint: replay must restore recent load as well as
// stamina. Strain is unbounded and independent of long-term weariness.
struct PlayerExertion {
    float stamina = 1;
    float strain = 0;
    float load = 0;

    float maximumStamina() const { return 1 - .20f * std::clamp(strain, 0.f, 1.f); }
    float regenerationMultiplier() const { return 1 - .50f * std::clamp(strain, 0.f, 1.f); }
    bool valid() const {
        return std::isfinite(stamina) && stamina >= 0 &&
            std::isfinite(strain) && strain >= 0 && std::isfinite(load) && load >= 0 &&
            stamina <= maximumStamina();
    }
    float exhaustion() const { return std::clamp((.25f - stamina) / .25f, 0.f, 1.f); }
    float speedMultiplier() const { return 1 - .16f * exhaustion(); }
    float accelerationMultiplier() const { return 1 - .575f * exhaustion(); }

    void decay(float dt, const PlayerExertionConfig& config) {
        load *= std::exp(-dt / config.loadDecaySeconds);
    }
    void advance(float dt, bool running, float recoveryMultiplier, const PlayerExertionConfig& config,
                 float strainRecoveryMultiplier = 1) {
        stamina = std::min(stamina, maximumStamina());
        if (!running) {
            // Positive regeneration leaves zero stamina immediately. With no
            // regeneration, zero stamina must continue to block strain recovery.
            if (stamina > 0 || config.walkingRecovery * recoveryMultiplier * regenerationMultiplier() > 0)
                recoverStrain(dt, strainRecoveryMultiplier, config);
            stamina = std::min(maximumStamina(),
                stamina + config.walkingRecovery * recoveryMultiplier * regenerationMultiplier() * dt);
            return;
        }
        const float effort = 1 + config.loadDrainMultiplier * load;
        // Split exactly at depletion; only the remainder of this step produces
        // strain, including when a step crosses zero stamina.
        const float drain = config.runningDrain * effort;
        const float fueledSeconds = drain > 0 ? std::min(dt, stamina / drain) : (stamina > 0 ? dt : 0);
        recoverStrain(fueledSeconds, strainRecoveryMultiplier, config);
        stamina = std::max(0.f, stamina - drain * fueledSeconds);
        strain += config.strainRate * effort * (dt - fueledSeconds);
    }

private:
    void recoverStrain(float seconds, float multiplier, const PlayerExertionConfig& config) {
        if (config.dayLengthSeconds > 0)
            strain = std::max(0.f, strain - static_cast<float>(
                seconds / config.dayLengthSeconds * config.strainRecoveryPerDay * multiplier));
    }
};

} // namespace viewer
