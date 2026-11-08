#include "player_health.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void near(float actual, float expected, const char* message) {
    require(std::abs(actual - expected) < 1e-6f, message);
}
template<class Action> void rejected(Action action) {
    bool invalid = false;
    try { action(); } catch (const std::invalid_argument&) { invalid = true; }
    require(invalid, "Invalid medical data must be rejected");
}
void storageAndValidation() {
    using namespace viewer;
    PlayerHealth player;
    require(player.health() == 1 && player.injuries().empty(), "Players must start at 100% with no injuries");
    player.setPainTolerance(.35f);
    const auto cut = player.addInjury({InjuryBodyPart::LeftHand, InjuryType::Cut, .1f, .25f, .1f, {true}});
    const auto sprain = player.addInjury({InjuryBodyPart::RightAnkle, InjuryType::Sprain, .2f, .6f, .75f, {false, true}});
    player.addInjury({InjuryBodyPart::None, InjuryType::MinorBruises, .025f});
    near(player.health(), .675f, "Health must subtract every injury's damage, including systemic injuries");
    require(cut != sprain && player.injuries()[0].pain() == .25f && player.injuries()[1].modifiers().swelling,
            "Injuries retain distinct IDs and modifiers");
    auto copy = player;
    require(copy.bandage(cut) && !copy.bleeding() && player.bleeding(), "Players must own independent medical records");
    near(copy.health(), player.health(), "Bandages stop bleeding without healing damage");
    require(!copy.bandage(cut) && !copy.bandage(9999) && !copy.bandage(sprain), "Stale and nonbleeding wounds reject treatment");
    player.addInjury({InjuryBodyPart::Head, InjuryType::Burn, 0});
    require(player.injuries().size() == 3, "Zero damage injuries must disappear immediately");
    for (float invalid : {-1.f, 1.01f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
        rejected([&] { player.setPainTolerance(invalid); });
        rejected([&] { PlayerInjury i(InjuryBodyPart::Head, InjuryType::Burn, invalid); });
        rejected([&] { PlayerInjury i(InjuryBodyPart::Head, InjuryType::Burn, .1f, invalid); });
        rejected([&] { PlayerInjury i(InjuryBodyPart::Head, InjuryType::Burn, .1f, 0, invalid); });
    }
    rejected([] { PlayerInjury i(InjuryBodyPart::None, InjuryType::Cut, .1f); });
    rejected([] { PlayerInjury i(InjuryBodyPart::Head, InjuryType::BloodLoss, .1f); });
    rejected([] { PlayerInjury i(InjuryBodyPart::None, InjuryType::MinorBruises, .1f, 0, 0, {true}); });
    rejected([] { PlayerInjury i(InjuryBodyPart::Count, InjuryType::Cut, .1f); });
    rejected([] { PlayerInjury i(InjuryBodyPart::Head, InjuryType::Count, .1f); });
    rejected([&] { player.addInjury(player.injuries()[0]); });
}
void regenerationAndBleeding() {
    using namespace viewer;
    DayNightCycle cycle; cycle.dayLengthSeconds = 1200;
    PlayerHealth awake;
    const auto first = awake.addInjury({InjuryBodyPart::LeftFoot, InjuryType::Cut, .2f});
    awake.addInjury({InjuryBodyPart::None, InjuryType::MinorBruises, .1f});
    auto asleep = awake;
    awake.advance(600, cycle, false);
    near(awake.injuries()[0].damage(), .15f, "Half a day heals a quarter of each original injury");
    near(awake.injuries()[1].damage(), .075f, "Systemic damage shares base regeneration");
    awake.advance(600, cycle, false);
    near(awake.health(), .85f, "One awake day must heal half the original damage");
    asleep.advance(1200, cycle, true);
    require(asleep.health() == 1 && asleep.injuries().empty(), "One day sleeping must fully heal original injuries");
    awake.advance(1200, cycle, false);
    require(awake.health() == 1 && awake.injuries().empty() && !awake.bandage(first), "Healed records and their wound IDs must disappear");
    const auto later = awake.addInjury({InjuryBodyPart::Head, InjuryType::Cut, .1f});
    require(later != first, "Healing must never recycle wound IDs");
    PlayerHealth bleeding;
    const auto wound = bleeding.addInjury({InjuryBodyPart::Head, InjuryType::Cut, .2f, 0, 0, {true}});
    bleeding.advance(50, cycle, false);
    require(bleeding.injuries().size() == 2 && bleeding.injuries()[1].type() == InjuryType::BloodLoss &&
            bleeding.injuries()[1].bodyPart() == InjuryBodyPart::None, "An untreated wound must produce systemic blood loss");
    near(bleeding.injuries()[1].damage(), .009895833f,
         "One game hour adds one percentage point of blood loss, with base regeneration during that hour");
    PlayerHealth fine;
    fine.addInjury({InjuryBodyPart::Head, InjuryType::Cut, .2f, 0, 0, {true}});
    for (int tick = 0; tick < 3000; ++tick) fine.advance(50.0 / 3000, cycle, false);
    near(fine.health(), bleeding.health(), "Blood loss and regeneration must be independent of step size");
    require(bleeding.bandage(wound), "A bleeding wound must accept a bandage");
    const float before = bleeding.injuries()[1].damage();
    bleeding.advance(50, cycle, false);
    require(!bleeding.bleeding() && bleeding.injuries()[1].damage() < before, "Bandaging must stop new blood loss while existing damage heals");
    PlayerHealth frozen;
    frozen.addInjury({InjuryBodyPart::Head, InjuryType::Cut, .2f, 0, 0, {true}});
    cycle.dayLengthSeconds = 0;
    frozen.advance(86400, cycle, true);
    near(frozen.health(), .8f, "A frozen game calendar must freeze healing and blood loss");
    PlayerHealth dead;
    dead.addInjury({InjuryBodyPart::Head, InjuryType::Cut, 1.f, 0, 0, {true}});
    cycle.dayLengthSeconds = 1200;
    dead.advance(2400, cycle, true);
    require(dead.health() == 0 && !dead.bleeding(), "Regeneration must not revive dead players");
}
}
int main() {
    try { storageAndValidation(); regenerationAndBleeding(); std::cout << "Player health and regeneration tests passed\n"; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
