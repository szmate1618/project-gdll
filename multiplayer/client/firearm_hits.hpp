#pragma once

#include "protocol.hpp"
#include "weapon_config.hpp"
#include "blood_mist.hpp"

#include <functional>
#include <vector>

namespace multiplayer {

struct FirearmImpact {
    glm::vec3 position{0}; // Runtime world meters, independent of the claim's bone-local point.
    viewer::BloodMistSize size = viewer::BloodMistSize::small;
};

// Trace each authored pellet against the last displayed scene. An empty report
// records a miss; weapon eligibility and the firing cadence remain server-owned.
// Optional presentation impacts replace the caller's vector only after every
// trace succeeds. A complete miss clears it; exceptions preserve its old value.
FirearmShot reportFirearmHits(std::uint8_t slot, const viewer::WeaponConfig& weapon,
    glm::vec3 origin, glm::vec3 direction,
    const std::function<std::optional<viewer::ZombieShotTrace>(glm::vec3, glm::vec3)>& trace,
    std::vector<FirearmImpact>* impacts = nullptr);

} // namespace multiplayer
