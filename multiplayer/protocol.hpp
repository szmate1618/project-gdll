#pragma once
#include "inventory/inventory_action.hpp"
#include "inventory/player_inventory.hpp"

#include "fps_controller.hpp"
#include "grenades.hpp"
#include "blood_mist.hpp"
#include "player_body_pose.hpp"

#include "vehicle_config.hpp"
#include "boid/simulation.hpp"
#include <glm/gtc/quaternion.hpp>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>
#include <array>

namespace multiplayer {
constexpr float tickSeconds = 1.0f / 60;
constexpr std::size_t maxPlayers = 8, maxZombies = 1024, maxVisibleZombies = 256;
constexpr std::size_t maxBoxes = 512, maxDoors = 4096, maxBones = 128;
constexpr std::size_t maxVehicles = viewer::maxVehicleInstances;
constexpr std::uint32_t noVehicle = UINT32_MAX;
constexpr std::uint8_t noVehicleSeat = UINT8_MAX;
constexpr std::size_t maxMessageBytes = 256 * 1024;
constexpr std::uint32_t protocolVersion = 17; // Deterministic boid command stream.
constexpr std::size_t maxNetworkBirds = 4096, maxBirdCommands = 1024;
using BirdFrame = viewer::boid::Frame;
struct BirdResync {};

struct Hello { std::string fingerprint; };
struct Welcome { std::uint32_t player = 0; };
struct Goodbye {};
constexpr std::size_t fragmentBytes = 900;
struct Fragment {
    std::uint64_t message = 0;
    std::uint32_t totalBytes = 0;
    std::uint16_t index = 0;
    std::vector<std::uint8_t> bytes;
};
struct Command {
    std::uint64_t sequence = 0;
    viewer::FPSInput movement;
    float pitch = 0;
    glm::vec2 drag{0}; // Mouse pixels accumulated during this command.
    bool interaction = false, left = false, right = false, reload = false;
    std::uint8_t slot = 0;
    bool enterVehicle = false; // One Enter press; deduplicated with the command sequence.
    std::vector<viewer::InventoryDragAction> inventoryActions{};
};
struct Commands {
    std::vector<Command> commands;
    std::uint64_t snapshotTick = 0; // Last complete essential snapshot received.
};
struct PlayerState {
    std::uint32_t id = 0;
    std::uint64_t resolved = 0;
    float health = 100.f; // Authoritative hit points, in [0, 100].
    viewer::FPSState movement;
    float yaw = -90, pitch = 0;
    std::uint8_t slot = 0;
    std::uint32_t loaded = 0, reserve = 0;
    std::uint64_t shots = 0;
    bool weaponCoolingDown = false;

    std::uint32_t vehicle = noVehicle;
    std::uint8_t seat = noVehicleSeat;
    viewer::PlayerBodyPose melee;
    float weaponSwitchRemaining = 0, weaponSwitchDuration = 0; // Server simulation seconds.
    float reloadRemaining = 0, reloadDuration = 0;
    std::vector<std::uint32_t> ammoQuantities{}; // Stack IDs in fingerprinted inventory map order.
    bool inventoryDragging = false;
    std::vector<viewer::ItemPlacement> inventoryPlacements{}; // All item IDs in the same map order, when idle.
};
struct ZombieState {
    std::uint32_t id = 0, asset = 0;
    glm::mat4 transform{1}, root{1};
    float phase = 0, walkSeconds = 0;
    bool walking = false, running = false, dead = false;
    std::vector<std::uint8_t> woundMask{};
};
// A complete, coherent corpse pose, delivered independently of essential state.
// Versions increase for each zombie; an acknowledgement releases a bounded
// transfer slot. Intermediate versions are coalesced while a transfer is active.
struct CorpsePose {
    std::uint32_t id = 0, asset = 0;
    std::uint64_t version = 0;
    glm::mat4 transform{1}, root{1};
    std::vector<glm::mat4> bones;
    std::int16_t severedJoint = -1;
};
struct PoseAck { std::uint32_t id = 0; std::uint64_t version = 0; };
struct DoorState { std::uint32_t id = 0; float angle = 0; };
struct BoxState {
    std::uint32_t id = 0;
    glm::vec3 center{0}, halfExtents{.5f};
    glm::quat rotation{1, 0, 0, 0};
    bool removed = false;
};
struct RigidPose {
    glm::vec3 center{0};
    glm::quat rotation{1, 0, 0, 0};
};
struct VehicleState {
    std::uint32_t id = 0;
    std::uint32_t type = 0;
    RigidPose chassis;
    std::array<RigidPose, 4> wheels;
    glm::vec3 velocity{0};
};
// Self-contained interested-entity set. Absence hides a zombie; it does not kill it.
struct Snapshot {
    std::uint64_t tick = 0;
    std::vector<PlayerState> players;
    std::vector<ZombieState> zombies;
    std::vector<DoorState> doors;
    std::vector<BoxState> boxes;
    std::vector<viewer::GrenadePose> grenades;
    std::vector<viewer::GrenadeExplosion> explosions;
    std::vector<glm::mat4> woundContents;
    std::vector<viewer::BloodMistBurst> bloodMist{};

    std::vector<VehicleState> vehicles;
};
using Message = std::variant<Hello, Welcome, Commands, Snapshot, Goodbye, Fragment, CorpsePose, PoseAck, BirdFrame, BirdResync>;
std::vector<std::uint8_t> encode(const Message& message);
Message decode(const void* data, std::size_t bytes);
} // namespace multiplayer
