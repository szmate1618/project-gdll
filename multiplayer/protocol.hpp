#pragma once

#include "fps_controller.hpp"
#include <glm/gtc/quaternion.hpp>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace multiplayer {
constexpr float tickSeconds = 1.0f / 60;
constexpr std::size_t maxPlayers = 8, maxZombies = 1024, maxVisibleZombies = 256;
constexpr std::size_t maxBoxes = 512, maxDoors = 4096, maxBones = 128;
constexpr std::size_t maxMessageBytes = 256 * 1024;
constexpr std::uint32_t protocolVersion = 3; // Slots 3/4 are shotgun/sniper.

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
};
struct Commands {
    std::vector<Command> commands;
    std::uint64_t snapshotTick = 0; // Last complete essential snapshot received.
};
struct PlayerState {
    std::uint32_t id = 0;
    std::uint64_t resolved = 0;
    viewer::FPSState movement;
    float yaw = -90, pitch = 0;
    std::uint8_t slot = 0;
    std::uint32_t loaded = 0, reserve = 0;
    std::uint64_t shots = 0;
};
struct ZombieState {
    std::uint32_t id = 0, asset = 0;
    glm::mat4 transform{1}, root{1};
    float phase = 0, walkSeconds = 0;
    bool walking = false, running = false, dead = false;
};
// A complete, coherent corpse pose, delivered independently of essential state.
// Versions increase for each zombie; an acknowledgement releases a bounded
// transfer slot. Intermediate versions are coalesced while a transfer is active.
struct CorpsePose {
    std::uint32_t id = 0, asset = 0;
    std::uint64_t version = 0;
    glm::mat4 transform{1}, root{1};
    std::vector<glm::mat4> bones;
};
struct PoseAck { std::uint32_t id = 0; std::uint64_t version = 0; };
struct DoorState { std::uint32_t id = 0; float angle = 0; };
struct BoxState {
    std::uint32_t id = 0;
    glm::vec3 center{0}, halfExtents{.5f};
    glm::quat rotation{1, 0, 0, 0};
};
// Self-contained interested-entity set. Absence hides a zombie; it does not kill it.
struct Snapshot {
    std::uint64_t tick = 0;
    std::vector<PlayerState> players;
    std::vector<ZombieState> zombies;
    std::vector<DoorState> doors;
    std::vector<BoxState> boxes;
};
using Message = std::variant<Hello, Welcome, Commands, Snapshot, Goodbye, Fragment, CorpsePose, PoseAck>;
std::vector<std::uint8_t> encode(const Message& message);
Message decode(const void* data, std::size_t bytes);
} // namespace multiplayer
