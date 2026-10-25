#pragma once
#include "scene.hpp"
#include "interior_objects.hpp"
#include "physics_scene.hpp"
#include "player_interaction.hpp"
#include "weapons.hpp"
#include "weapon_switch.hpp"
#include "zombie_door_collision.hpp"
#include "replication.hpp"
#include "vehicle_seats.hpp"
#include "player_body.hpp"
#include "melee_combat.hpp"
#include "vehicle_zombie_collision.hpp"
#include <map>
#include <set>

namespace multiplayer {
struct ServerPlayer {
    ServerPlayer(const viewer::Weapons& initialWeapons, const viewer::PlayerInventory& initialInventory,
                 std::shared_ptr<viewer::PhysicsWorld> world);
    viewer::FPSController controller;
    viewer::Camera camera;
    viewer::Weapons weapons;
    viewer::PlayerInventory inventory;
    viewer::WeaponSwitch weaponSwitch;
    viewer::PlayerBody body;
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
    viewer::MeleeCombat melee_;
    viewer::VehicleZombieCollision vehicleZombies_;
    std::map<std::uint32_t, ServerPlayer> players_;
    std::uint32_t nextPlayer_ = 1;
    std::uint64_t tick_ = 0;
};
} // namespace multiplayer
