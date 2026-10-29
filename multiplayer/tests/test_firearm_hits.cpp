#include "client/firearm_hits.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
}

int main() {
    try {
        viewer::WeaponConfig weapon;
        weapon.pellets = 3; weapon.spreadDegrees = 6;
        const glm::vec3 origin(10, 1.7f, -5), forward(0, 0, -1);
        std::size_t rays = 0;
        const auto report = multiplayer::reportFirearmHits(2, weapon, origin, forward,
            [&](glm::vec3 start, glm::vec3 direction) -> std::optional<viewer::ZombieShotTrace> {
                require(start == origin && std::abs(glm::length(direction) - 1) < .0001f,
                    "Pellet query lost the displayed aim origin or normalized direction");
                const auto pellet = rays++;
                if (pellet == 1) return {};
                viewer::ZombieShotTrace hit;
                hit.claim.zombie = 73; hit.claim.bone = 2; hit.claim.point = {1, 2, 3};
                hit.claim.headshot = pellet == 2;
                return hit;
            });
        require(rays == 3 && report.slot == 2 && report.hits.size() == 2 &&
                report.hits[0].pellet == 0 && report.hits[1].pellet == 2 &&
                report.hits[1].hit.zombie == 73 && report.hits[1].hit.headshot &&
                report.hits[0].hit.point == glm::vec3(1, 2, 3),
            "Shot report lost pellet identities, local contact data or classifications");
        const auto miss = multiplayer::reportFirearmHits(2, weapon, origin, forward,
            [](glm::vec3, glm::vec3) -> std::optional<viewer::ZombieShotTrace> { return {}; });
        require(miss.hits.empty() && miss.slot == 2, "A complete miss did not retain its firing weapon");
        std::cout << "Client firearm reports preserve pellet identities, contacts and misses\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
