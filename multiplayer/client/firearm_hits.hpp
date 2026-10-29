#pragma once

#include "protocol.hpp"
#include "weapon_config.hpp"

#include <functional>

namespace multiplayer {

// Trace each authored pellet against the last displayed scene. An empty report
// records a miss; weapon eligibility and the firing cadence remain server-owned.
FirearmShot reportFirearmHits(std::uint8_t slot, const viewer::WeaponConfig& weapon,
    glm::vec3 origin, glm::vec3 direction,
    const std::function<std::optional<viewer::ZombieShotTrace>(glm::vec3, glm::vec3)>& trace);

} // namespace multiplayer
