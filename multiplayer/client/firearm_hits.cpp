#include "firearm_hits.hpp"
#include "weapons.hpp"

#include <stdexcept>

namespace multiplayer {

FirearmShot reportFirearmHits(std::uint8_t slot, const viewer::WeaponConfig& weapon,
        glm::vec3 origin, glm::vec3 direction,
        const std::function<std::optional<viewer::ZombieShotTrace>(glm::vec3, glm::vec3)>& trace) {
    if (slot >= viewer::punchSlot || weapon.grenade || !trace)
        throw std::invalid_argument("Firearm hit reporting requires an equipped firearm and scene query");
    const auto directions = viewer::weaponShotDirections(weapon, direction);
    if (directions.size() > maxFirearmHits)
        throw std::invalid_argument("Firearm pellet count exceeds report capacity");
    FirearmShot result;
    result.slot = slot;
    for (std::size_t pellet = 0; pellet < directions.size(); ++pellet)
        if (const auto hit = trace(origin, directions[pellet]))
            result.hits.push_back({static_cast<std::uint8_t>(pellet), hit->claim});
    return result;
}

} // namespace multiplayer
