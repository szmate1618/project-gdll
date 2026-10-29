#pragma once
#include "inventory/inventory_action.hpp"
#include "inventory/player_inventory.hpp"

#include "fps_controller.hpp"
#include "command_wheel.hpp"
#include "grenades.hpp"
#include "blood_mist.hpp"
#include "sound_events.hpp"
#include "player_body_pose.hpp"
#include "zombie_hit.hpp"

#include "vehicle_config.hpp"
#include "boid/simulation.hpp"
#include <glm/gtc/quaternion.hpp>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>
#include <array>
#include <optional>

namespace multiplayer {
constexpr float tickSeconds = 1.0f / 60;
constexpr std::size_t maxPlayers = 8, maxZombies = 100000, maxVisibleZombies = 10000;
constexpr std::size_t maxZombieUpdatesPerSnapshot = 1024, maxZombieSetupsPerBatch = 64;
constexpr double zombieTimeoutSeconds = 5;
constexpr std::size_t maxBoxes = 512, maxDoors = 4096, maxBones = 128;
constexpr std::size_t maxVehicles = viewer::maxVehicleInstances;
constexpr std::uint32_t noVehicle = UINT32_MAX;
constexpr std::uint8_t noVehicleSeat = UINT8_MAX;
constexpr std::size_t maxMessageBytes = 256 * 1024;
constexpr std::uint32_t protocolVersion = 22; // Persistent, acknowledged partial vehicle updates.
constexpr std::size_t maxFirearmHits = 64; // Matches the weapon configuration's pellet limit.
constexpr std::uint64_t signalLifetimeTicks = 6 * 60, signalCooldownTicks = 60;
constexpr std::size_t maxSignalsPerPlayer = signalLifetimeTicks / signalCooldownTicks;
constexpr std::size_t maxSignals = maxPlayers * maxSignalsPerPlayer;
using SignalKind = viewer::SignalKind;
struct SignalState {
    std::uint32_t id = 0; // Sender; multiple signals may share this player ID.
    SignalKind kind = SignalKind::None;
    glm::vec3 position{0}; // Runtime metres, X east/Y up/Z south.
    std::uint64_t expires = 0; // Authoritative server tick.
};
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
struct FirearmHit {
    std::uint8_t pellet = 0;
    viewer::ZombieHitClaim hit;
};
struct FirearmShot {
    std::uint8_t slot = 0; // Expected equipped slot; server owns its actual configuration.
    std::vector<FirearmHit> hits; // Empty is an explicit miss, never a server ray-test request.
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
    SignalKind signal = SignalKind::None; // One-shot, resolved with this command's aim.
    // Reports share this command's sequence and are retried/deduplicated with it.
    std::optional<FirearmShot> firearmShot;
};
struct ReplicationView {
    glm::vec3 eye{0}; // Runtime X east/Y up/Z south metres.
    float yaw = -90, pitch = 0;
    float verticalFov = 60, aspect = 16.f / 9, maxDistance = 180;
};
struct Commands {
    std::vector<Command> commands;
    std::uint64_t snapshotTick = 0; // Last complete essential snapshot received.
    std::optional<ReplicationView> view{};
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
    // The wire carries only ID, packed flags/cadence, root position/yaw and
    // walkSeconds. Asset, phase, normalization and wounds arrive via setup.
    std::uint8_t updatePeriodTicks = 3; // 3/12/60 authoritative ticks: 20/5/1 Hz.
};
struct ZombieSetup {
    std::uint32_t id = 0, asset = 0;
    std::uint64_t version = 0;
    glm::mat4 normalization{1}; // Complete fixed asset-to-character affine map.
    float phase = 0;
    std::vector<std::uint8_t> woundMask{};
};
struct ZombieSetups { std::vector<ZombieSetup> zombies; };
struct ZombieSetupReceipt { std::uint32_t id = 0; std::uint64_t version = 0; };
struct ZombieSetupAck { std::vector<ZombieSetupReceipt> zombies; };
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
// Zombies and vehicles are partial updates to persistent replicas; absence means
// no update. Other entity lists retain their complete-snapshot semantics.
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
    std::vector<SignalState> signals;
    std::vector<SoundEvent> sounds;
};
using Message = std::variant<Hello, Welcome, Commands, Snapshot, Goodbye, Fragment, CorpsePose, PoseAck, BirdFrame, BirdResync,
                             ZombieSetups, ZombieSetupAck>;
std::vector<std::uint8_t> encode(const Message& message);
Message decode(const void* data, std::size_t bytes);
} // namespace multiplayer
