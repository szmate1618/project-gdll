#include "client/firearm_hits.hpp"
#include "weapons.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

void worldImpacts() {
    viewer::WeaponConfig weapon;
    const glm::vec3 origin(10, 1.7f, -5);
    std::vector<multiplayer::FirearmImpact> impacts;
    const auto report = multiplayer::reportFirearmHits(0, weapon, origin, {0, 0, -12},
        [&](glm::vec3 start, glm::vec3 direction) -> std::optional<viewer::ZombieShotTrace> {
            require(start == origin && direction == glm::vec3(0, 0, -1), "Impact query did not normalize the aim ray");
            viewer::ZombieShotTrace hit;
            hit.distance = 5;
            hit.claim.bone = 7; hit.claim.point = {200, 400, 900};
            return hit;
        }, &impacts);
    require(report.hits.size() == 1 && report.hits[0].hit.point == glm::vec3(200, 400, 900),
        "Presentation impact changed the bone-local hit report");
    require(impacts.size() == 1 && glm::distance(impacts[0].position, {10, 1.7f, -9.975f}) < .00001f &&
            impacts[0].size == viewer::BloodMistSize::small,
        "Impact used bone-local coordinates or lost its world-space surface offset");
    multiplayer::reportFirearmHits(0, weapon, origin, {0, 0, -1},
        [](glm::vec3, glm::vec3) -> std::optional<viewer::ZombieShotTrace> {
            viewer::ZombieShotTrace hit; hit.distance = .01f; return hit;
        }, &impacts);
    require(impacts.size() == 1 && impacts[0].position == origin, "Near impact offset crossed behind the shot origin");
    multiplayer::reportFirearmHits(0, weapon, origin, {0, 0, -1},
        [](glm::vec3, glm::vec3) -> std::optional<viewer::ZombieShotTrace> { return {}; }, &impacts);
    require(impacts.empty(), "A miss retained the preceding shot's presentation impacts");
}

void pelletImpacts() {
    viewer::WeaponConfig weapon; weapon.pellets = 8; weapon.spreadDegrees = 8;
    const glm::vec3 origin(20, 2, -30);
    std::vector<glm::vec3> rays;
    std::vector<multiplayer::FirearmImpact> impacts;
    const auto trace = [&](glm::vec3 start, glm::vec3 direction) -> std::optional<viewer::ZombieShotTrace> {
        require(start == origin && std::abs(glm::length(direction) - 1) < .00001f,
            "Shotgun impact lost the normalized authored pellet ray");
        rays.push_back(direction);
        viewer::ZombieShotTrace hit; hit.distance = 3; return hit;
    };
    const auto report = multiplayer::reportFirearmHits(2, weapon, origin, {0, 0, -4}, trace, &impacts);
    require(report.hits.size() == 8 && impacts.size() == 8 && rays.size() == 8,
        "A shotgun pellet produced missing or duplicate presentation impacts");
    for (std::size_t i = 0; i < impacts.size(); ++i) {
        const auto offset = impacts[i].position - origin;
        require(std::abs(glm::length(offset) - 2.975f) < .00001f &&
                glm::dot(glm::normalize(offset), rays[i]) > .99999f &&
                report.hits[i].pellet == static_cast<std::uint8_t>(i),
            "Shotgun impact left its own pellet ray or distance");
    }
    std::size_t pellet = 0;
    const auto partial = multiplayer::reportFirearmHits(2, weapon, origin, {0, 0, -4},
        [&](glm::vec3, glm::vec3) -> std::optional<viewer::ZombieShotTrace> {
            if (pellet++ % 2) return {};
            viewer::ZombieShotTrace hit; hit.distance = 3; return hit;
        }, &impacts);
    require(partial.hits.size() == 4 && impacts.size() == 4 && partial.hits.back().pellet == 6,
        "Missed pellets created presentation impacts or stale preceding impacts survived");
}

void impactSizes() {
    const auto size = [](viewer::ZombieHitClaim claim, viewer::ShotCaliber caliber) {
        viewer::WeaponConfig weapon; weapon.caliber = caliber;
        std::vector<multiplayer::FirearmImpact> impacts;
        multiplayer::reportFirearmHits(0, weapon, {}, {0, 0, -1},
            [&](glm::vec3, glm::vec3) -> std::optional<viewer::ZombieShotTrace> {
                return viewer::ZombieShotTrace{claim, 2};
            }, &impacts);
        require(impacts.size() == 1, "Size classification changed the impact count");
        return impacts[0].size;
    };
    viewer::ZombieHitClaim claim;
    require(size(claim, viewer::ShotCaliber::low) == viewer::BloodMistSize::small, "Ordinary hit predicted a large mist");
    claim.joint = 2;
    require(size(claim, viewer::ShotCaliber::low) == viewer::BloodMistSize::medium, "Joint hit lost medium mist");
    claim.joint = -1; claim.woundRegion = viewer::ZombieWoundRegion::torso;
    require(size(claim, viewer::ShotCaliber::low) == viewer::BloodMistSize::large, "Torso opening lost large mist");
    claim.headshot = true; claim.woundRegion = viewer::ZombieWoundRegion::head;
    require(size(claim, viewer::ShotCaliber::low) == viewer::BloodMistSize::small, "Low-caliber head hit predicted an opening");
    require(size(claim, viewer::ShotCaliber::high) == viewer::BloodMistSize::large, "High-caliber head opening lost large mist");
    claim.woundRegion.reset();
    require(size(claim, viewer::ShotCaliber::high) == viewer::BloodMistSize::small,
        "Headshot without a wound region invented an opening");
}

void atomicOutput() {
    viewer::WeaponConfig weapon;
    const multiplayer::FirearmImpact previous{{1, 2, 3}, viewer::BloodMistSize::medium};
    std::vector<multiplayer::FirearmImpact> impacts{previous};
    const auto preserved = [&] {
        return impacts.size() == 1 && impacts[0].position == previous.position && impacts[0].size == previous.size;
    };
    const auto miss = [](glm::vec3, glm::vec3) -> std::optional<viewer::ZombieShotTrace> { return {}; };
    bool rejected = false;
    try {
        multiplayer::reportFirearmHits(static_cast<std::uint8_t>(viewer::punchSlot), weapon,
            {}, {0, 0, -1}, miss, &impacts);
    } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected && preserved(), "Invalid firearm input partially replaced presentation output");
    weapon.pellets = 3;
    std::size_t queried = 0;
    rejected = false;
    try {
        multiplayer::reportFirearmHits(0, weapon, {}, {0, 0, -1},
            [&](glm::vec3, glm::vec3) -> std::optional<viewer::ZombieShotTrace> {
                if (++queried == 2) throw std::runtime_error("Query failed");
                viewer::ZombieShotTrace hit; hit.distance = 2; return hit;
            }, &impacts);
    } catch (const std::runtime_error&) { rejected = true; }
    require(rejected && preserved(), "A failed later pellet published a partial impact list");
    rejected = false;
    try {
        multiplayer::reportFirearmHits(0, weapon, {}, {0, 0, -1},
            [](glm::vec3, glm::vec3) -> std::optional<viewer::ZombieShotTrace> {
                viewer::ZombieShotTrace hit; hit.distance = std::numeric_limits<float>::quiet_NaN(); return hit;
            }, &impacts);
    } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected && preserved(), "Nonfinite impact distance replaced presentation output");
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
        worldImpacts(); pelletImpacts(); impactSizes(); atomicOutput();
        std::cout << "Client firearm reports and presentation impacts preserve world/local frames, pellets and misses\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
