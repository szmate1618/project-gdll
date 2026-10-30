#include "firearm_hits.hpp"
#include "weapons.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace multiplayer {

FirearmShot reportFirearmHits(std::uint8_t slot, const viewer::WeaponConfig& weapon,
        glm::vec3 origin, glm::vec3 direction,
        const std::function<std::optional<viewer::ZombieShotTrace>(glm::vec3, glm::vec3)>& trace,
        std::vector<FirearmImpact>* impacts) {
    if (slot >= viewer::punchSlot || weapon.grenade || !trace)
        throw std::invalid_argument("Firearm hit reporting requires an equipped firearm and scene query");
    const auto directions = viewer::weaponShotDirections(weapon, direction);
    if (directions.size() > maxFirearmHits)
        throw std::invalid_argument("Firearm pellet count exceeds report capacity");
    FirearmShot result;
    result.slot = slot;
    std::vector<FirearmImpact> presentation;
    for (std::size_t pellet = 0; pellet < directions.size(); ++pellet) {
        if (const auto hit = trace(origin, directions[pellet])) {
            result.hits.push_back({static_cast<std::uint8_t>(pellet), hit->claim});
            if (!impacts) continue;
            if (!std::isfinite(hit->distance) || hit->distance < 0)
                throw std::invalid_argument("Firearm impact distance must be finite and nonnegative");
            auto size = hit->claim.joint >= 0 ? viewer::BloodMistSize::medium : viewer::BloodMistSize::small;
            if (hit->claim.woundRegion == viewer::ZombieWoundRegion::torso ||
                (hit->claim.woundRegion == viewer::ZombieWoundRegion::head && weapon.caliber == viewer::ShotCaliber::high))
                size = viewer::BloodMistSize::large;
            // Contact claims use source bone units. The displayed ray distance
            // instead gives the world position, offset enough to clear the skin.
            const auto point = origin + directions[pellet] * (hit->distance - std::min(.025f, hit->distance));
            presentation.push_back({point, size});
        }
    }
    if (impacts) *impacts = std::move(presentation);
    return result;
}

} // namespace multiplayer
