#pragma once

#include "protocol.hpp"
#include "zombie_layer.hpp"

#include <set>

namespace multiplayer {

// Selection visits occupied spatial cells approximately near-to-far. It never
// sorts actors by exact distance; callers retain this order for cadence ranks.
void selectZombieInterest(viewer::ZombieLayer& zombies, glm::vec3 feet,
                          const std::optional<ReplicationView>& view,
                          const std::set<std::size_t>& previous,
                          std::vector<std::size_t>& selected,
                          const std::optional<glm::vec3>& sunDirection = {});

} // namespace multiplayer
