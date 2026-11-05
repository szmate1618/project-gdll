#include "player_health.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template<class Action>
void rejected(Action action) {
    bool invalid = false;
    try { action(); } catch (const std::invalid_argument&) { invalid = true; }
    require(invalid, "Invalid health data must be rejected");
}
} // namespace

int main() {
    try {
        using namespace viewer;
        static_assert(static_cast<int>(InjuryBodyPart::Count) == 29);
        PlayerHealth player;
        require(player.health() == 100 && player.painTolerance() == 0 && player.injuries().empty(),
                "A new health model must start at full health with no injuries or pain tolerance");
        player.setHealth(72.5f);
        player.setPainTolerance(.35f);
        player.addInjury({InjuryBodyPart::LeftHand, InjuryType::Cut, .25f, .1f, {true, false, false}});
        player.addInjury({InjuryBodyPart::RightAnkle, InjuryType::Sprain, .6f, .75f, {false, true, false}});
        player.addInjury({InjuryBodyPart::LeftHand, InjuryType::Bruise, .1f, .05f, {false, true, false}});
        const auto& cut = player.injuries().at(0);
        require(cut.bodyPart() == InjuryBodyPart::LeftHand && cut.type() == InjuryType::Cut &&
                    cut.pain() == .25f && cut.weakness() == .1f && cut.modifiers().bleeding,
                "A bleeding injury must preserve its body part, type and individual scores");
        require(player.injuries().at(1).bodyPart() == InjuryBodyPart::RightAnkle &&
                    player.injuries().at(1).modifiers().swelling && !player.injuries().at(1).modifiers().bleeding,
                "An injury modifier must apply only to its own body part's injury");
        require(player.injuries().size() == 3 && player.health() == 72.5f && player.painTolerance() == .35f,
                "Multiple injuries on one body part must coexist without triggering gameplay calculations");
        auto copy = player;
        copy.setHealth(50);
        copy.addInjury({InjuryBodyPart::UpperTorso, InjuryType::Puncture, 1, 0, {true, true, true}});
        require(player.health() == 72.5f && player.injuries().size() == 3 &&
                    copy.injuries().at(3).modifiers().infected,
                "Each health model must own its injuries independently and retain combined modifiers");

        for (float invalid : {-1.f, std::numeric_limits<float>::infinity(),
                              -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
            rejected([&] { player.setHealth(invalid); });
            rejected([&] { player.setPainTolerance(invalid); });
            rejected([&] { PlayerInjury injury(InjuryBodyPart::Head, InjuryType::BluntTrauma, invalid, 0); });
            rejected([&] { PlayerInjury injury(InjuryBodyPart::Head, InjuryType::BluntTrauma, 0, invalid); });
        }
        rejected([&] { player.setHealth(100.01f); });
        rejected([&] { player.setPainTolerance(1.01f); });
        rejected([] { PlayerInjury injury(InjuryBodyPart::Neck, InjuryType::Strain, 1.01f, 0); });
        rejected([] { PlayerInjury injury(InjuryBodyPart::Neck, InjuryType::Strain, 0, 1.01f); });
        for (const auto part : {static_cast<InjuryBodyPart>(-1), InjuryBodyPart::Count})
            rejected([&] { PlayerInjury injury(part, InjuryType::Cut); });
        for (const auto type : {static_cast<InjuryType>(-1), InjuryType::Count})
            rejected([&] { PlayerInjury injury(InjuryBodyPart::Pelvis, type); });
        require(player.health() == 72.5f && player.painTolerance() == .35f && player.injuries().size() == 3,
                "Rejected data must leave the existing health model intact");
        for (const float endpoint : {0.f, 1.f}) {
            player.setHealth(endpoint * PlayerHealth::maximumHealth);
            player.setPainTolerance(endpoint);
            player.addInjury({InjuryBodyPart::LowerTorso, InjuryType::Burn, endpoint, endpoint});
            require(player.health() == endpoint * 100 && player.painTolerance() == endpoint &&
                        player.injuries().back().pain() == endpoint && player.injuries().back().weakness() == endpoint,
                    "Health and normalized scores must accept their inclusive endpoints");
        }
        std::cout << "Player health storage tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Player health test failed: " << error.what() << '\n';
        return 1;
    }
}
