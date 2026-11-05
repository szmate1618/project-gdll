#pragma once

#include <cmath>
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
    // Scores are normalized percentages: 0 = none, 1 = 100%.
    // No automatic pain/weakness values or effects are assigned by injury type.
    PlayerInjury(InjuryBodyPart bodyPart, InjuryType type, float pain = 0, float weakness = 0,
                 InjuryModifiers modifiers = {})
        : bodyPart_(bodyPart), type_(type), pain_(pain), weakness_(weakness), modifiers_(modifiers) {
        if (bodyPart < InjuryBodyPart::LeftHand || bodyPart >= InjuryBodyPart::Count ||
            type < InjuryType::Cut || type >= InjuryType::Count)
            throw std::invalid_argument("Injury must have a valid body part and type");
        if (!std::isfinite(pain) || pain < 0 || pain > 1 ||
            !std::isfinite(weakness) || weakness < 0 || weakness > 1)
            throw std::invalid_argument("Injury pain and weakness must be finite values between 0 and 1");
    }

    InjuryBodyPart bodyPart() const { return bodyPart_; }
    InjuryType type() const { return type_; }
    float pain() const { return pain_; }
    float weakness() const { return weakness_; }
    const InjuryModifiers& modifiers() const { return modifiers_; }

private:
    InjuryBodyPart bodyPart_;
    InjuryType type_;
    float pain_, weakness_;
    InjuryModifiers modifiers_;
};

} // namespace viewer
