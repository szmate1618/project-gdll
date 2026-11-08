#include "flashlights.hpp"

namespace multiplayer {
std::vector<std::size_t> remoteHeadlightVehicles(const std::vector<PlayerState>& players,
                                               std::uint32_t self) {
    std::vector<std::size_t> result;
    for (const auto& player : players)
        if (player.id != self && player.vehicle != noVehicle && player.seat == 0)
            result.push_back(player.vehicle);
    return result;
}

std::vector<viewer::FlashlightView> remoteFlashlights(const std::vector<PlayerState>& players,
                                                    std::uint32_t self) {
    std::vector<viewer::FlashlightView> result;
    for (const auto& player : players) {
        if (player.id == self || !player.flashlight || player.vehicle != noVehicle) continue;
        viewer::FPSController body;
        body.restoreMovement(player.movement);
        viewer::Camera camera;
        camera.setPosition(body.eyePosition());
        camera.setOrientation(player.yaw, player.pitch);
        result.push_back(viewer::flashlightView(camera));
    }
    return result;
}
} // namespace multiplayer
