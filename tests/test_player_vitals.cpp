#include "player_vitals.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
} // namespace

int main() {
    static_assert(std::is_same_v<decltype(std::declval<viewer::PlayerVitals>().health()), float>);
    static_assert(std::is_same_v<decltype(std::declval<viewer::PlayerVitals>().stamina()), float>);
    static_assert(std::is_same_v<decltype(std::declval<viewer::PlayerVitals>().weariness()), float>);
    try {
        viewer::PlayerVitals player;
        require(player.health() == 100.f && player.stamina() == 100.f && player.weariness() == 0.f,
                "Players must begin with 100 floating-point health and the initial stored vitals");
        require(player.applyDamage(.000001f) == 0.f && player.health() == 100.f,
                "Damage smaller than float precision must not report a health change");
        require(player.applyDamage(2.25f) == 2.25f && player.health() == 97.75f,
                "Damage must retain fractional health");
        for (float invalid : {0.f, -1.f, std::numeric_limits<float>::infinity(),
                              -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
            require(player.applyDamage(invalid) == 0.f && player.health() == 97.75f,
                    "Invalid or nonpositive damage must preserve health");
        require(player.stamina() == 100.f && player.weariness() == 0.f,
                "Health damage must not alter stamina or weariness");
        const auto otherPlayer = player;
        require(player.applyDamage(std::numeric_limits<float>::max()) == 97.75f && player.health() == 0.f,
                "Overkill must report only remaining health and clamp to zero");
        require(player.applyDamage(10.f) == 0.f && player.health() == 0.f,
                "Repeated hits at zero health must not produce negative health or new damage feedback");
        require(otherPlayer.health() == 97.75f, "Each player's vitals must have independent ownership");
        std::cout << "Player vitals tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Player vitals test failed: " << error.what() << '\n';
        return 1;
    }
}
