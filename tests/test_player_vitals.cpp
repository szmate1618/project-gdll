#include "player_vitals.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}
int main() {
    try {
        viewer::PlayerVitals player;
        require(player.health() == 1.f && player.weariness() == 0 && !player.bleeding(), "Players begin at full normalized health");
        require(player.applyDamage(1e-9f) == 0 && player.healthState().injuries().empty(), "Unrepresentable damage must not create wounds");
        require(std::abs(player.applyDamage(.125f) - .125f) < 1e-6f && player.health() == .875f, "Damage retains fractions of health");
        const auto& injury = player.healthState().injuries().at(0);
        require(injury.type() == viewer::InjuryType::Cut && injury.damage() == .125f && injury.modifiers().bleeding &&
                injury.bodyPart() != viewer::InjuryBodyPart::None, "Every actual hit must create a located bleeding wound carrying its damage");
        require(player.healthState().bandage(injury.id()) && !player.bleeding(), "Treating the only wound stops bleeding");
        for (float invalid : {0.f, -1.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
            require(player.applyDamage(invalid) == 0 && player.health() == .875f, "Invalid damage must preserve health");
        const auto other = player;
        require(player.applyDamage(std::numeric_limits<float>::max()) == .875f && player.health() == 0 &&
                player.healthState().injuries().size() == 2, "Overkill must attach only the remaining health as damage");
        require(player.applyDamage(.1f) == 0 && player.healthState().injuries().size() == 2 && !player.bleeding(), "Depleted players cannot receive more wounds or emit scent");
        require(other.health() == .875f && other.weariness() == 0, "Players have independent health ownership");
        std::set<viewer::InjuryBodyPart> locations;
        viewer::PlayerVitals many;
        for (int hit = 0; hit < 100; ++hit) {
            many.applyDamage(.001f);
            const auto& wound = many.healthState().injuries().back();
            require(wound.type() == viewer::InjuryType::Cut && wound.bodyPart() < viewer::InjuryBodyPart::None,
                    "Attacks must never create systemic injury types");
            locations.insert(wound.bodyPart());
        }
        require(locations.size() > 1, "Attack locations must vary across hits");
        std::cout << "Player vitals injury tests passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
