#pragma once
#include "scene.hpp"
#include "interior_objects.hpp"
#include "physics_scene.hpp"
#include "player_interaction.hpp"
#include "weapons.hpp"
#include "weapon_switch.hpp"
#include "weapon_reload.hpp"
#include "inventory/inventory_drag.hpp"
#include "zombie_door_collision.hpp"
#include "replication.hpp"
#include "vehicle_seats.hpp"
#include "birds.hpp"
#include "player_body.hpp"
#include "player_vitals.hpp"
#include "melee_combat.hpp"
#include "vehicle_zombie_collision.hpp"
#include <map>
#include <deque>
#include <set>

namespace multiplayer {
struct ServerPlayer {
    ServerPlayer(const viewer::Weapons& initialWeapons, const viewer::PlayerInventory& initialInventory,
                 std::shared_ptr<viewer::PhysicsWorld> world);
    viewer::FPSController controller;
    viewer::PlayerVitals vitals;
    glm::vec3 velocity{0}; // Measured over the last complete solved server tick, m/s.
    viewer::Camera camera;
    viewer::Weapons weapons;
    viewer::PlayerInventory inventory;
    viewer::InventoryDrag inventoryDrag;
    std::deque<viewer::InventoryDragAction> pendingInventoryActions;
    viewer::WeaponSwitch weaponSwitch;
    viewer::WeaponReload weaponReload;
    viewer::PlayerBody body;
    viewer::PlayerInteraction interaction;
    viewer::UndergrowthEmitter undergrowth;
    std::map<std::uint64_t, Command> pending;
    Command held;
    std::uint64_t resolved = 0, lastPacketTick = 0, shots = 0;
    Replication replication;
    std::optional<ReplicationView> replicationView;
    std::uint64_t lastViewSequence = 0;
    std::vector<SignalState> signals;
    std::uint64_t nextSignalTick = 0;
    std::optional<std::uint64_t> birdTick;
    bool started = false, triggerNeedsRelease = false, reloadRequested = false;
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
    std::optional<ZombieSetups> nextSetups(std::uint32_t id) const;
    void setupsSent(std::uint32_t id, const ZombieSetups& setups);
    void acknowledgeSetups(std::uint32_t id, const ZombieSetupAck& ack);
    std::optional<BirdFrame> birdFrame(std::uint32_t id) const;
    void birdFrameSent(std::uint32_t id, std::uint64_t tick);
    void requestBirdCheckpoint(std::uint32_t id);
    std::uint64_t tick() const { return tick_; }
    const std::map<std::uint32_t, ServerPlayer>& players() const { return players_; }
private:
    Scene& scene_;
    ServerBirds birds_;
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
