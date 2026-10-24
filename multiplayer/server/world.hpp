#pragma once
#include "scene.hpp"
#include "interior_objects.hpp"
#include "physics_scene.hpp"
#include "player_interaction.hpp"
#include "weapons.hpp"
#include "zombie_door_collision.hpp"
#include "replication.hpp"
#include "vehicle_seats.hpp"
#include <map>
#include <set>

namespace multiplayer {
struct ServerPlayer {
    explicit ServerPlayer(const viewer::Weapons& initialWeapons);
    viewer::FPSController controller;
    viewer::Camera camera;
    viewer::Weapons weapons;
    viewer::PlayerInteraction interaction;
    viewer::UndergrowthEmitter undergrowth;
    std::map<std::uint64_t, Command> pending;
    Command held;
    std::uint64_t resolved = 0, lastPacketTick = 0, shots = 0;
    Replication replication;
    bool started = false, triggerNeedsRelease = false;
};
class World {
public:
    explicit World(Scene& scene);
    std::uint32_t join();
    void leave(std::uint32_t id);
    void submit(std::uint32_t id, const Commands& commands);
    void step();
    Snapshot snapshot(std::uint32_t id);
    std::optional<CorpsePose> nextPose(std::uint32_t id) const;
    void poseSent(std::uint32_t id, const CorpsePose& pose);
    void acknowledgePose(std::uint32_t id, const PoseAck& ack);
    std::uint64_t tick() const { return tick_; }
    const std::map<std::uint32_t, ServerPlayer>& players() const { return players_; }
private:
    Scene& scene_;
    VehicleSeats vehicleSeats_;
    viewer::PhysicsScene physicsScene_;
    viewer::InteriorObjects interiors_;
    viewer::ZombieDoorCollision zombieDoors_;
    std::map<std::uint32_t, ServerPlayer> players_;
    std::uint32_t nextPlayer_ = 1;
    std::uint64_t tick_ = 0;
};
} // namespace multiplayer
