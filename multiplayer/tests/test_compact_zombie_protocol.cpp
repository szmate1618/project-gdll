#include "protocol.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace multiplayer;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class F> void rejects(F run, const char* message) {
    bool rejected = false;
    try { run(); } catch (const std::exception&) { rejected = true; }
    require(rejected, message);
}
Snapshot roundTrip(const Snapshot& snapshot) {
    const auto bytes = encode(snapshot);
    return std::get<Snapshot>(decode(bytes.data(), bytes.size()));
}
ZombieState motion(glm::vec3 position, float yaw, std::uint8_t period = 3) {
    ZombieState zombie;
    zombie.id = 99999;
    zombie.root = glm::rotate(glm::translate(glm::mat4(1), position), glm::radians(yaw), {0, 1, 0});
    zombie.transform = zombie.root;
    zombie.walkSeconds = 1234.567f;
    zombie.walking = zombie.running = true;
    zombie.updatePeriodTicks = period;
    return zombie;
}
void compactMotion() {
    Snapshot empty;
    const auto emptySize = encode(empty).size();
    Snapshot snapshot;
    snapshot.zombies.push_back(motion({1.234f, -1.234f, -3000.125f}, 179.9f));
    const auto bytes = encode(snapshot);
    require(bytes.size() == emptySize + 19, "Ordinary motion must occupy exactly 19 bytes");
    const auto copy = roundTrip(snapshot).zombies.at(0);
    require(copy.id == 99999 && copy.walking && copy.running && !copy.dead &&
            copy.walkSeconds == snapshot.zombies[0].walkSeconds, "Compact motion lost identity, state or float32 animation time");
    require(glm::length(glm::vec3(copy.root[3] - snapshot.zombies[0].root[3])) < .0006f,
            "Compact position exceeded millimetre quantization precision");
    const float decodedYaw = glm::degrees(std::atan2(copy.root[2].x, copy.root[2].z));
    require(std::abs(std::remainder(decodedYaw - 179.9f, 360.f)) <= .704f,
            "One-byte yaw did not use circular nearest rounding");
    // Header, tick, player count, ground-item count and zombie count precede the first record.
    constexpr std::size_t firstRecord = 9 + 8 + 4 + 4 + 4;
    require(bytes[firstRecord + 5] == 0xd2 && bytes[firstRecord + 6] == 0x04 && bytes[firstRecord + 7] == 0x00 &&
            bytes[firstRecord + 8] == 0x2e && bytes[firstRecord + 9] == 0xfb && bytes[firstRecord + 10] == 0xff,
            "Signed 24-bit coordinates must use explicit little-endian two's-complement bytes");
    for (const auto period : {3, 12, 60}) {
        snapshot.zombies[0].updatePeriodTicks = static_cast<std::uint8_t>(period);
        snapshot.zombies[0].dead = period == 60;
        require(roundTrip(snapshot).zombies[0].updatePeriodTicks == period,
                "Motion cadence was not preserved");
    }
    for (std::uint8_t side : {std::uint8_t(1), std::uint8_t(2)}) {
        auto attack = snapshot;
        auto& z = attack.zombies[0];
        z.walking = z.running = z.dead = false; z.attackClip = side; z.attackSeconds = .875f; z.activeAttackRevision = 17;
        const auto decoded = roundTrip(attack).zombies[0];
        require(decoded.attackClip == side && decoded.attackSeconds == .875f && decoded.activeAttackRevision == 17 && !decoded.walking &&
                encode(attack).size() == emptySize + 27,
            "Authored attack lost its variant/clock or inflated non-attacking records");
        z.walking = true;
        rejects([&] { encode(attack); }, "Walking and attacking were accepted together");
    }
    auto malformed = bytes;
    malformed[firstRecord + 4] |= 0xc0;
    rejects([&] { decode(malformed.data(), malformed.size()); }, "Unknown zombie flags were accepted");
    malformed = bytes;
    malformed[firstRecord + 4] |= 0x30;
    rejects([&] { decode(malformed.data(), malformed.size()); }, "Unknown zombie cadence was accepted");
    snapshot.zombies[0].updatePeriodTicks = 4;
    rejects([&] { encode(snapshot); }, "Unsupported send cadence was accepted");
    snapshot.zombies[0] = motion({}, 0);
    snapshot.zombies[0].root[0][0] = 2;
    rejects([&] { encode(snapshot); }, "Non-rigid roots cannot silently lose scale");
    snapshot.zombies[0] = motion({}, 0);
    snapshot.zombies[0].walkSeconds = std::numeric_limits<float>::quiet_NaN();
    rejects([&] { encode(snapshot); }, "Nonfinite animation time was accepted");
    for (std::size_t size = 0; size < bytes.size(); ++size)
        rejects([&] { decode(bytes.data(), size); }, "Truncated compact snapshot was accepted");
}
void positionRange() {
    Snapshot empty;
    const auto emptySize = encode(empty).size();
    Snapshot snapshot;
    snapshot.zombies.push_back(motion({-8388.608f, 8388.607f, 0}, -1));
    require(encode(snapshot).size() == emptySize + 19, "Signed 24-bit endpoint was rejected");
    const auto endpoints = roundTrip(snapshot).zombies[0];
    require(endpoints.root[3].x == -8388.608f && endpoints.root[3].y == 8388.607f,
            "Signed 24-bit endpoints changed sign or wrapped");
    snapshot.zombies[0] = motion({8388.608f, -9000.125f, 10000.5f}, 0);
    require(encode(snapshot).size() == emptySize + 22, "Float32 escape must add exactly three bytes");
    const auto fallback = roundTrip(snapshot).zombies[0];
    require(fallback.root[3] == snapshot.zombies[0].root[3], "Out-of-range positions were clipped or quantized");
    snapshot.zombies[0] = motion({}, 0);
    snapshot.zombies[0].root[3].x = std::numeric_limits<float>::infinity();
    rejects([&] { encode(snapshot); }, "Nonfinite positions were accepted through float32 escape");
}
void setupProtocol() {
    ZombieSetup setup;
    setup.id = 99999; setup.asset = 12; setup.version = 7; setup.phase = .25f;
    setup.normalization = glm::scale(glm::mat4(1), {-2, 3, 4});
    setup.normalization[1].x = .3f;
    setup.normalization[3] = {1, 2, 3, 1};
    setup.woundMask = {0, 1, 128, 255};
    const auto bytes = encode(ZombieSetups{{setup}});
    const auto copy = std::get<ZombieSetups>(decode(bytes.data(), bytes.size())).zombies.at(0);
    require(copy.id == setup.id && copy.asset == setup.asset && copy.version == setup.version &&
            copy.phase == setup.phase && copy.normalization == setup.normalization && copy.woundMask == setup.woundMask,
            "Setup lost fixed affine normalization, phase or wound data");
    const auto ack = encode(ZombieSetupAck{{{setup.id, setup.version}}});
    require(std::get<ZombieSetupAck>(decode(ack.data(), ack.size())).zombies.at(0).version == setup.version,
            "Setup acknowledgement lost its version");
    auto invalid = setup;
    invalid.version = 0;
    rejects([&] { encode(ZombieSetups{{invalid}}); }, "Zero setup version was accepted");
    invalid = setup; invalid.normalization[0][3] = 1;
    rejects([&] { encode(ZombieSetups{{invalid}}); }, "Non-affine setup transform was accepted");
    invalid = setup; invalid.normalization[0][3] = 1e-6f;
    rejects([&] { encode(ZombieSetups{{invalid}}); }, "A near-affine setup must match the client's exact affine contract");
    invalid = setup; invalid.normalization[0] = glm::vec4(0);
    rejects([&] { encode(ZombieSetups{{invalid}}); }, "Singular normalization was accepted");
    rejects([&] { encode(ZombieSetups{{setup, setup}}); }, "Duplicate setup IDs were accepted");
    rejects([&] { encode(ZombieSetupAck{{{setup.id, 0}}}); }, "Zero acknowledgement version was accepted");
    for (std::size_t size = 0; size < bytes.size(); ++size)
        rejects([&] { decode(bytes.data(), size); }, "Truncated setup was accepted");
}
void viewProtocol() {
    Commands batch;
    batch.view = ReplicationView{{1, 2, 3}, 10, -5, 1, 2, 180};
    const auto bytes = encode(batch);
    const auto copy = std::get<Commands>(decode(bytes.data(), bytes.size()));
    require(copy.view && copy.view->eye == batch.view->eye && copy.view->verticalFov == 1 && copy.view->aspect == 2,
            "Scope or vehicle camera view was not preserved");
    batch.view->verticalFov = 0;
    rejects([&] { encode(batch); }, "Invalid view FOV was accepted");
    batch.view->verticalFov = 60; batch.view->aspect = std::numeric_limits<float>::quiet_NaN();
    rejects([&] { encode(batch); }, "Nonfinite view aspect was accepted");
    batch.view.reset();
    const auto absent = encode(batch);
    require(!std::get<Commands>(decode(absent.data(), absent.size())).view, "Absent view became a zero-size camera");
}
}
int main() {
    try {
        compactMotion(); positionRange(); setupProtocol(); viewProtocol();
        std::cout << "Compact motion, setup metadata and view protocol passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
