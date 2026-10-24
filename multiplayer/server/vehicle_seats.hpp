#pragma once
#include "scene.hpp"
#include "camera.hpp"
#include "vehicle_player.hpp"
#include <map>

namespace multiplayer {
// Server-only ownership of distinct (car, seat) pairs. Physics and seat geometry are shared
// with single-player; clients may only request seat transitions and controls.
class VehicleSeats {
public:
    explicit VehicleSeats(Scene& scene) : scene_(scene) {}
    std::uint32_t car(std::uint32_t player) const;
    std::uint8_t seat(std::uint32_t player) const;
    bool toggle(std::uint32_t player, viewer::FPSController& controller, viewer::Camera& camera);
    void leave(std::uint32_t player);
    void drive(std::uint32_t player, const Command& command);
    void updateSeat(std::uint32_t player, viewer::FPSController& controller, viewer::Camera& camera) const;
private:
    Scene& scene_;
    std::map<std::uint32_t, viewer::VehicleSeatRef> occupants_;
};
} // namespace multiplayer
