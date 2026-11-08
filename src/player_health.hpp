#pragma once

#include "player_injury.hpp"
#include "day_night_clock.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace viewer {

// Player-owned injury records are the single source of health. All damage and
// health values use 1 = 100%. Medical presentation copies retain stable IDs.
class PlayerHealth {
public:
    static constexpr float maximumHealth = 1.f;
    static constexpr double baseHealthRegeneration = .5; // Original injury damage per game day.
    static constexpr double bloodLossPerGameHour = .01;

    float health() const {
        double damage = 0;
        for (const auto& injury : injuries_) damage += injury.damage_;
        return static_cast<float>(std::clamp(1.0 - damage, 0.0, 1.0));
    }
    float painTolerance() const { return painTolerance_; }
    const std::vector<PlayerInjury>& injuries() const { return injuries_; }
    bool bleeding() const {
        return health() > 0 && std::any_of(injuries_.begin(), injuries_.end(),
            [](const auto& injury) { return injury.modifiers_.bleeding; });
    }
    void setPainTolerance(float tolerance) {
        if (!std::isfinite(tolerance) || tolerance < 0 || tolerance > 1)
            throw std::invalid_argument("Pain tolerance must be a finite value between 0 and 1");
        painTolerance_ = tolerance;
    }
    std::uint64_t addInjury(PlayerInjury injury) {
        if (injury.damage_ == 0) return 0;
        if (injury.id_ == 0) injury.id_ = nextId_;
        if (injury.id_ == std::numeric_limits<std::uint64_t>::max() ||
            std::any_of(injuries_.begin(), injuries_.end(), [&](const auto& i) { return i.id_ == injury.id_; }))
            throw std::invalid_argument("Invalid or duplicate injury ID");
        nextId_ = std::max(nextId_, injury.id_ + 1);
        injuries_.push_back(injury);
        return injury.id_;
    }
    bool bandage(std::uint64_t id) {
        for (auto& injury : injuries_) if (injury.id_ == id && injury.modifiers_.bleeding && health() > 0) {
            injury.modifiers_.bleeding = false;
            return true;
        }
        return false;
    }
    // Simulation time already includes shared sleep acceleration. Apply the
    // additional 2x healing bonus only to sleepers, never to their guards.
    void advance(double seconds, const DayNightCycle& cycle, bool sleeping) {
        if (!std::isfinite(seconds) || seconds <= 0 || !std::isfinite(cycle.dayLengthSeconds) || cycle.dayLengthSeconds <= 0 || health() <= 0) return;
        double days = seconds / cycle.dayLengthSeconds;
        const double regeneration = baseHealthRegeneration * (sleeping ? 2 : 1);
        while (days > 1e-12 && health() > 0) {
            double step = days, bleedingRate = 0;
            PlayerInjury* blood = nullptr;
            for (auto& injury : injuries_) {
                if (injury.type_ == InjuryType::BloodLoss) blood = &injury;
                else if (injury.modifiers_.bleeding) {
                    bleedingRate += 24 * bloodLossPerGameHour;
                    // Integrate only while this wound still exists.
                    step = std::min(step, injury.damage_ / (injury.originalDamage_ * regeneration));
                }
            }
            const double originalBlood = blood ? blood->originalDamage_ : 0;
            // Newly lost blood shares the same regeneration rule. Integrate its
            // growing original damage so results do not depend on tick size.
            const double growth = originalBlood < 1 ? bleedingRate : 0;
            if (growth > 0) step = std::min(step, (1 - originalBlood) / growth);
            for (auto& injury : injuries_) if (&injury != blood)
                injury.damage_ = std::max(0.0, injury.damage_ - step * injury.originalDamage_ * regeneration);
            const double bloodDamage = std::max(0.0, (blood ? blood->damage_ : 0) + bleedingRate * step -
                regeneration * (originalBlood * step + growth * step * step / 2));
            const double bloodOriginal = std::min(1.0, originalBlood + growth * step);
            if (blood) {
                blood->damage_ = std::min(1.0, bloodDamage);
                blood->originalDamage_ = bloodOriginal;
            } else if (bloodDamage > 1e-10) {
                const auto id = addInjury({InjuryBodyPart::None, InjuryType::BloodLoss,
                    static_cast<float>(std::min(1.0, bloodDamage)), 0, 0, {}, 0, static_cast<float>(bloodOriginal)});
                (void)id;
                injuries_.back().damage_ = std::min(1.0, bloodDamage);
                injuries_.back().originalDamage_ = bloodOriginal;
            }
            injuries_.erase(std::remove_if(injuries_.begin(), injuries_.end(),
                [](const auto& injury) { return injury.damage_ <= 1e-10; }), injuries_.end());
            days -= step;
        }
    }

private:
    float painTolerance_ = 0;
    std::uint64_t nextId_ = 1;
    std::vector<PlayerInjury> injuries_;
};

} // namespace viewer
