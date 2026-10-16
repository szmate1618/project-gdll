#include "protocol.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <stdexcept>
#include <type_traits>

namespace multiplayer {
namespace {
// Explicit little-endian fields: never transmit native structs or GLM padding.
struct Wire {
    std::vector<std::uint8_t> bytes;
    const std::uint8_t* input = nullptr;
    std::size_t size = 0, offset = 0;
    bool reading = false;
    template<class T> void integer(T& value) {
        static_assert(std::is_unsigned_v<T>);
        if (reading && size - offset < sizeof(T)) throw std::runtime_error("Truncated multiplayer message");
        if (reading) value = 0;
        for (std::size_t i = 0; i < sizeof(T); ++i) {
            if (reading) value |= T(input[offset++]) << (8 * i);
            else bytes.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
        }
    }
    void number(float& value) {
        std::uint32_t bits = 0;
        if (!reading) std::memcpy(&bits, &value, 4);
        integer(bits);
        if (reading) std::memcpy(&value, &bits, 4);
        if (!std::isfinite(value) || std::abs(value) > 1e8f) throw std::runtime_error("Invalid multiplayer number");
    }
    void flag(bool& value) {
        std::uint8_t n = value;
        integer(n);
        if (n > 1) throw std::runtime_error("Invalid multiplayer flag");
        value = n != 0;
    }
    void vector(glm::vec3& p) { number(p.x); number(p.y); number(p.z); }
    void matrix(glm::mat4& m) { for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) number(m[c][r]); }
    template<class T, class F> void list(std::vector<T>& values, std::size_t limit, F item) {
        if (values.size() > limit) throw std::runtime_error("Too many multiplayer records");
        auto count = static_cast<std::uint32_t>(values.size());
        integer(count);
        if (count > limit) throw std::runtime_error("Too many multiplayer records");
        if (reading) values.resize(count);
        for (auto& value : values) item(value);
    }
};
void movement(Wire& w, viewer::FPSState& s) {
    w.vector(s.feet); w.vector(s.groundNormal); w.number(s.verticalVelocity);
    // Fixed stepping leaves at most one player substep of remainder.
    float remainder = static_cast<float>(s.accumulator); w.number(remainder); s.accumulator = remainder;
    std::uint8_t stance = static_cast<std::uint8_t>(s.stance), takeoff = static_cast<std::uint8_t>(s.takeoffStance);
    w.integer(stance); w.integer(takeoff);
    if (stance > 2 || takeoff > 2 || remainder < 0 || remainder > .01f)
        throw std::runtime_error("Invalid multiplayer movement state");
    s.stance = static_cast<viewer::PlayerStance>(stance); s.takeoffStance = static_cast<viewer::PlayerStance>(takeoff);
    w.flag(s.grounded); w.flag(s.jumpHeld); w.flag(s.jumpQueued); w.flag(s.proneRequested); w.flag(s.proneQueued);
}
void command(Wire& w, Command& c) {
    w.integer(c.sequence); w.number(c.movement.forward); w.number(c.movement.right);
    w.number(c.movement.yawDegrees); w.number(c.pitch);
    w.number(c.drag.x); w.number(c.drag.y);
    w.flag(c.movement.run); w.flag(c.movement.jump); w.flag(c.movement.crouch); w.flag(c.movement.toggleProne);
    w.flag(c.interaction); w.flag(c.left); w.flag(c.right); w.flag(c.reload); w.integer(c.slot);
    if (c.sequence == 0 || std::abs(c.movement.forward) > 1 || std::abs(c.movement.right) > 1 ||
        std::abs(c.movement.yawDegrees) > 180 || std::abs(c.pitch) > 89 || c.slot > 3 ||
        std::abs(c.drag.x) > 2000 || std::abs(c.drag.y) > 2000)
        throw std::runtime_error("Invalid multiplayer command");
}
template<class T> void uniqueIds(const std::vector<T>& values) {
    std::set<std::uint32_t> ids;
    for (const auto& item : values) if (!ids.insert(item.id).second) throw std::runtime_error("Duplicate multiplayer entity ID");
}
void payload(Wire& w, Hello& hello) {
    if (w.reading) hello.fingerprint.resize(64);
    if (hello.fingerprint.size() != 64) throw std::runtime_error("Invalid content fingerprint");
    for (auto& c : hello.fingerprint) {
        auto n = static_cast<std::uint8_t>(c); w.integer(n); c = static_cast<char>(n);
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) throw std::runtime_error("Invalid content fingerprint");
    }
}
void payload(Wire& w, Welcome& welcome) {
    w.integer(welcome.player);
    if (!welcome.player) throw std::runtime_error("Invalid player ID");
}
void payload(Wire&, Goodbye&) {}
void payload(Wire& w, Fragment& fragment) {
    w.integer(fragment.message); w.integer(fragment.totalBytes); w.integer(fragment.index);
    w.list(fragment.bytes, fragmentBytes, [&](std::uint8_t& b) { w.integer(b); });
    const auto offset = static_cast<std::size_t>(fragment.index) * fragmentBytes;
    if (!fragment.message || fragment.totalBytes <= fragmentBytes || fragment.totalBytes > maxMessageBytes ||
        offset >= fragment.totalBytes || fragment.bytes.size() != std::min(fragmentBytes, fragment.totalBytes - offset))
        throw std::runtime_error("Invalid multiplayer fragment");
}
void payload(Wire& w, Commands& batch) {
    w.integer(batch.snapshotTick);
    w.list(batch.commands, 32, [&](Command& c) { command(w, c); });
    std::uint64_t previous = 0;
    for (const auto& c : batch.commands) {
        if (c.sequence <= previous) throw std::runtime_error("Unordered commands");
        previous = c.sequence;
    }
}
void payload(Wire& w, Snapshot& s) {
    w.integer(s.tick);
    w.list(s.players, maxPlayers, [&](PlayerState& p) {
        w.integer(p.id); w.integer(p.resolved); movement(w, p.movement);
        w.number(p.yaw); w.number(p.pitch); w.integer(p.slot); w.integer(p.loaded); w.integer(p.reserve); w.integer(p.shots);
        if (!p.id || p.slot > 3 || std::abs(p.yaw) > 180 || std::abs(p.pitch) > 89)
            throw std::runtime_error("Invalid player snapshot");
    });
    w.list(s.zombies, maxVisibleZombies, [&](ZombieState& z) {
        w.integer(z.id); w.integer(z.asset); w.matrix(z.transform); w.matrix(z.root);
        w.number(z.phase); w.number(z.walkSeconds); w.flag(z.walking); w.flag(z.running); w.flag(z.dead);
        if (z.id >= maxZombies || z.asset >= maxZombies || z.phase < 0 || z.phase >= 1 || z.walkSeconds < 0)
            throw std::runtime_error("Invalid zombie snapshot");
    });
    w.list(s.doors, maxDoors, [&](DoorState& d) {
        w.integer(d.id); w.number(d.angle);
        if (std::abs(d.angle) > 4) throw std::runtime_error("Invalid door snapshot");
    });
    w.list(s.boxes, maxBoxes, [&](BoxState& b) {
        w.integer(b.id); w.vector(b.center); w.vector(b.halfExtents);
        w.number(b.rotation.w); w.number(b.rotation.x); w.number(b.rotation.y); w.number(b.rotation.z);
        if (b.id >= maxBoxes || glm::any(glm::lessThan(b.halfExtents, glm::vec3(.05f))) ||
            glm::any(glm::greaterThan(b.halfExtents, glm::vec3(2))) || std::abs(glm::length(b.rotation) - 1) > .001f)
            throw std::runtime_error("Invalid box snapshot");
    });
    uniqueIds(s.players); uniqueIds(s.zombies); uniqueIds(s.doors); uniqueIds(s.boxes);
}
void payload(Wire& w, CorpsePose& pose) {
    w.integer(pose.id); w.integer(pose.asset); w.integer(pose.version);
    w.matrix(pose.transform); w.matrix(pose.root);
    w.list(pose.bones, maxBones, [&](glm::mat4& m) { w.matrix(m); });
    if (pose.id >= maxZombies || pose.asset >= maxZombies || !pose.version)
        throw std::runtime_error("Invalid corpse pose");
}
void payload(Wire& w, PoseAck& ack) {
    w.integer(ack.id); w.integer(ack.version);
    if (ack.id >= maxZombies || !ack.version) throw std::runtime_error("Invalid corpse acknowledgement");
}
}
std::vector<std::uint8_t> encode(const Message& message) {
    Wire w;
    std::uint32_t magic = 0x4c4c4447, version = protocolVersion;
    std::uint8_t type = static_cast<std::uint8_t>(message.index());
    w.integer(magic); w.integer(version); w.integer(type);
    std::visit([&](auto value) { payload(w, value); }, message);
    if (w.bytes.size() > maxMessageBytes) throw std::runtime_error("Multiplayer message exceeds budget");
    return w.bytes;
}
Message decode(const void* data, std::size_t bytes) {
    if (!data || bytes < 9 || bytes > maxMessageBytes) throw std::runtime_error("Invalid multiplayer message size");
    Wire w; w.input = static_cast<const std::uint8_t*>(data); w.size = bytes; w.reading = true;
    std::uint32_t magic = 0, version = 0; std::uint8_t type = 0;
    w.integer(magic); w.integer(version); w.integer(type);
    if (magic != 0x4c4c4447 || version != protocolVersion || type > 7) throw std::runtime_error("Incompatible multiplayer protocol");
    Message result;
    switch (type) { case 0: result = Hello{}; break; case 1: result = Welcome{}; break;
        case 2: result = Commands{}; break; case 3: result = Snapshot{}; break;
        case 4: result = Goodbye{}; break; case 5: result = Fragment{}; break;
        case 6: result = CorpsePose{}; break; case 7: result = PoseAck{}; break; }
    std::visit([&](auto& value) { payload(w, value); }, result);
    if (w.offset != bytes) throw std::runtime_error("Trailing multiplayer message data");
    return result;
}
} // namespace multiplayer
