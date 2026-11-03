#pragma once

#include "protocol.hpp"
#include "flashlight.hpp"

namespace multiplayer {
// World-space held lights from the same presented poses as remote players.
// Occupied vehicles suppress flashlights, as for the local player.
std::vector<viewer::FlashlightView> remoteFlashlights(const std::vector<PlayerState>& players,
                                                    std::uint32_t self);
} // namespace multiplayer
