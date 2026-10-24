#include "vehicle_seats.hpp"
#include "vehicle_player.hpp"
#include <cmath>

namespace multiplayer {
std::uint32_t VehicleSeats::car(std::uint32_t player) const {
    const auto it = occupants_.find(player);
    return it == occupants_.end() ? noVehicle : static_cast<std::uint32_t>(it->second.car);
}
std::uint8_t VehicleSeats::seat(std::uint32_t player) const {
    const auto it = occupants_.find(player);
    return it == occupants_.end() ? noVehicleSeat : static_cast<std::uint8_t>(it->second.seat);
}
void VehicleSeats::leave(std::uint32_t player) {
    const auto it = occupants_.find(player);
    if (it == occupants_.end()) return;
    if (it->second.seat == 0) scene_.vehicles->setDriven(it->second.car, false);
    occupants_.erase(it);
}
bool VehicleSeats::toggle(std::uint32_t player, viewer::FPSController& controller, viewer::Camera& camera) {
    const auto occupied = car(player);
    if (occupied != noVehicle) {
        const auto& config = controller.config();
        for (const auto feet : viewer::vehicleExits(*scene_.vehicles, occupied, *scene_.collision, config, seat(player))) {
            if (!controller.spawn(feet + glm::vec3(0, config.eyeHeight, 0), *scene_.collision)) continue;
            leave(player); camera.setPosition(controller.eyePosition());
            return true;
        }
        return false;
    }
    const auto nearest = viewer::vehicleEntry(*scene_.vehicles, camera.position(), *scene_.collision,
        [&](std::size_t car, std::size_t seat) {
            for (const auto& entry : occupants_)
                if (entry.second.car == car && entry.second.seat == seat) return false;
            return true;
        });
    if (!nearest) return false;
    if (nearest->seat == 0) scene_.vehicles->setDriven(nearest->car, true);
    occupants_.emplace(player, *nearest);
    const auto forward = scene_.vehicles->pose(nearest->car).chassis.rotation * glm::vec3(0, 0, -1);
    camera.setOrientation(glm::degrees(std::atan2(forward.z, forward.x)), 0);
    updateSeat(player, controller, camera);
    return true;
}
void VehicleSeats::drive(std::uint32_t player, const Command& command) {
    if (seat(player) == 0)
        scene_.vehicles->drive(car(player), command.movement.forward, command.movement.right, command.movement.jump);
}
void VehicleSeats::updateSeat(std::uint32_t player, viewer::FPSController& controller, viewer::Camera& camera) const {
    const auto vehicle = car(player);
    if (vehicle == noVehicle) return;
    const auto eye = viewer::vehicleSeat(*scene_.vehicles, vehicle, seat(player));
    viewer::FPSState seated;
    seated.feet = eye - glm::vec3(0, controller.config().eyeHeight, 0);
    controller.restoreMovement(seated);
    camera.setPosition(eye);
}
} // namespace multiplayer
