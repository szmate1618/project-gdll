#pragma once

#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <stdexcept>

namespace viewer {

// Health locations are separate from the coarser rendered/physical PlayerBodyPart.
enum class InjuryBodyPart {
    LeftHand, RightHand,
    LeftForearm, RightForearm,
    LeftUpperArm, RightUpperArm,
    LeftWrist, RightWrist,
    LeftElbow, RightElbow,
    LeftShoulder, RightShoulder,
    LeftFoot, RightFoot,
    LeftLowerLeg, RightLowerLeg,
    LeftUpperLeg, RightUpperLeg,
    LeftAnkle, RightAnkle,
    LeftKnee, RightKnee,
    LeftHip, RightHip,
    Pelvis, LowerTorso, UpperTorso, Head, Neck,
    None, // Systemic injuries have no anatomical location.
    Count
};

enum class InjuryType {
    Cut,
    Abrasion,     // Scraped skin.
    Puncture,
    Bruise,
    BluntTrauma,
    Sprain,       // Joint/ligament injury, including a rolled ankle.
    Strain,       // Muscle/tendon injury.
    Dislocation,
    Fracture,
    Crush,
    Burn,
    BloodLoss,
    MinorBruises,
    Count
};

// Independent conditions: a cut can bleed, swell and become infected together.
struct InjuryModifiers {
    bool bleeding = false;
    bool swelling = false;
    bool infected = false;
};

class PlayerInjury {
public:
    // Damage is a fraction of full health. Original damage fixes the base
    // regeneration rate: half the original damage per day, twice while asleep.
    // IDs stay stable when healed records are removed or snapshots are delayed.
    PlayerInjury(InjuryBodyPart bodyPart, InjuryType type, float damage,
                 float pain = 0, float weakness = 0, InjuryModifiers modifiers = {},
                 std::uint64_t id = 0, float originalDamage = -1)
        : bodyPart_(bodyPart), type_(type), damage_(damage),
          originalDamage_(originalDamage == -1 ? damage : originalDamage),
          pain_(pain), weakness_(weakness), modifiers_(modifiers), id_(id) {
        const bool systemic = type == InjuryType::BloodLoss || type == InjuryType::MinorBruises;
        if (bodyPart < InjuryBodyPart::LeftHand || bodyPart >= InjuryBodyPart::Count ||
            type < InjuryType::Cut || type >= InjuryType::Count ||
            systemic != (bodyPart == InjuryBodyPart::None))
            throw std::invalid_argument("Injury must have a valid location for its type");
        for (const double score : {double(damage), double(pain), double(weakness), originalDamage_})
            if (!std::isfinite(score) || score < 0 || score > 1)
                throw std::invalid_argument("Injury scores must be finite values between 0 and 1");
        if (originalDamage_ < damage || (systemic && modifiers.bleeding))
            throw std::invalid_argument("Invalid original damage or systemic bleeding");
    }

    InjuryBodyPart bodyPart() const { return bodyPart_; }
    InjuryType type() const { return type_; }
    float damage() const { return static_cast<float>(damage_); }
    float originalDamage() const { return static_cast<float>(originalDamage_); }
    float pain() const { return pain_; }
    float weakness() const { return weakness_; }
    std::uint64_t id() const { return id_; }
    const InjuryModifiers& modifiers() const { return modifiers_; }

private:
    friend class PlayerHealth;
    InjuryBodyPart bodyPart_;
    InjuryType type_;
    double damage_; // Avoid losing small fixed-step regeneration increments.
    double originalDamage_;
    float pain_, weakness_;
    InjuryModifiers modifiers_;
    std::uint64_t id_;
};

} // namespace viewer
