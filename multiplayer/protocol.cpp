#include "protocol.hpp"
#include "weapons.hpp"
#include "zombie_skeleton.hpp"
#include "zombie_cavity_physics.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <glm/gtc/matrix_transform.hpp>

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
    void text(std::string& value, std::size_t maximum) {
        if (value.size() > maximum) throw std::runtime_error("Multiplayer string too long");
        auto length = static_cast<std::uint16_t>(value.size());
        integer(length);
        if (length > maximum || (reading && length > size - offset)) throw std::runtime_error("Invalid multiplayer string");
        if (reading) { value.assign(reinterpret_cast<const char*>(input + offset), length); offset += length; }
        else bytes.insert(bytes.end(), value.begin(), value.end());
    }
    void vector(glm::vec3& p) { number(p.x); number(p.y); number(p.z); }
    void matrix(glm::mat4& m) { for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) number(m[c][r]); }
    // Physical body boxes have rigid orientation and positive dimensions.
    // TRS needs ten floats instead of sixteen and keeps eight fists players
    // inside the existing full-fleet essential snapshot budget.
    void bodyPart(glm::mat4& m) {
        glm::vec3 center{0}, size{1}; glm::quat rotation{1,0,0,0};
        if (!reading) {
            center=glm::vec3(m[3]);
            size={glm::length(glm::vec3(m[0])),glm::length(glm::vec3(m[1])),glm::length(glm::vec3(m[2]))};
            rotation=glm::normalize(glm::quat_cast(glm::mat3{glm::vec3(m[0])/size.x,glm::vec3(m[1])/size.y,glm::vec3(m[2])/size.z}));
        }
        vector(center); vector(size);
        number(rotation.w); number(rotation.x); number(rotation.y); number(rotation.z);
        if (glm::any(glm::lessThan(size,glm::vec3(.02f))) || glm::any(glm::greaterThan(size,glm::vec3(3))) ||
            std::abs(glm::length(rotation)-1)>.001f) throw std::runtime_error("Invalid physical body box");
        if (reading) m=glm::translate(glm::mat4(1),center)*glm::mat4_cast(glm::normalize(rotation))*glm::scale(glm::mat4(1),size);
    }
    template<class T, class F> void list(std::vector<T>& values, std::size_t limit, F item) {
        if (values.size() > limit) throw std::runtime_error("Too many multiplayer records");
        auto count = static_cast<std::uint32_t>(values.size());
        integer(count);
        if (count > limit) throw std::runtime_error("Too many multiplayer records");
        if (reading && count > size - offset) throw std::runtime_error("Truncated multiplayer list");
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
    w.flag(c.enterVehicle);
    w.list(c.inventoryActions, 64, [&](viewer::InventoryDragAction& action) {
        auto kind = static_cast<std::uint8_t>(action.kind);
        w.integer(kind);
        if (kind > static_cast<std::uint8_t>(viewer::InventoryDragActionKind::Cancel))
            throw std::runtime_error("Invalid inventory action");
        action.kind = static_cast<viewer::InventoryDragActionKind>(kind);
        switch (action.kind) {
        case viewer::InventoryDragActionKind::PickUp: w.text(action.item, 128); break;
        case viewer::InventoryDragActionKind::Rotate: {
            auto turns = static_cast<std::uint8_t>((action.turns % 4 + 4) % 4);
            w.integer(turns);
            if (turns > 3) throw std::runtime_error("Invalid inventory rotation");
            action.turns = turns;
            break;
        }
        case viewer::InventoryDragActionKind::Drop: {
            w.text(action.storage, 192);
            if (!w.reading && (action.x < 0 || action.x > 15 || action.y < 0 || action.y > 15))
                throw std::runtime_error("Invalid inventory cell");
            auto x = static_cast<std::uint8_t>(action.x), y = static_cast<std::uint8_t>(action.y);
            w.integer(x); w.integer(y); action.x = x; action.y = y;
            if (x > 15 || y > 15) throw std::runtime_error("Invalid inventory cell");
            break;
        }
        case viewer::InventoryDragActionKind::Cancel: w.text(action.item, 128); break;
        }
    });
    auto signal = static_cast<std::uint8_t>(c.signal); w.integer(signal);
    if (signal > 3) throw std::runtime_error("Invalid signal command");
    c.signal = static_cast<SignalKind>(signal);
    if (c.sequence == 0 || std::abs(c.movement.forward) > 1 || std::abs(c.movement.right) > 1 ||
        std::abs(c.movement.yawDegrees) > 180 || std::abs(c.pitch) > 89 || c.slot > viewer::punchSlot ||
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
// Signed fixed-point values use explicit zigzag encoding, independent of
// signed shifts, machine endianness and compiler struct padding.
void fixed(Wire& w, std::int32_t& value) {
    std::uint32_t bits=0;
    if (!w.reading) bits=value<0?static_cast<std::uint32_t>(-std::int64_t(value)*2-1):static_cast<std::uint32_t>(value)*2;
    w.integer(bits);
    if (w.reading) value=static_cast<std::int32_t>((bits&1)?-std::int64_t(bits/2)-1:std::int64_t(bits/2));
}
void fixed(Wire& w, viewer::boid::Vec3& value) { fixed(w,value.x); fixed(w,value.y); fixed(w,value.z); }
void control(Wire& w, viewer::boid::Control& c) {
    fixed(w,c.target); fixed(w,c.aim); fixed(w,c.exit); fixed(w,c.attackDirection);
    auto mode=static_cast<std::uint8_t>(c.mode); w.integer(mode); c.mode=static_cast<viewer::boid::Mode>(mode);
    w.integer(c.remainingTicks); w.integer(c.pass); viewer::boid::validate(c);
}
void payload(Wire&, BirdResync&) {}
void payload(Wire& w, BirdFrame& frame) {
    w.integer(frame.tick); w.integer(frame.checksum); w.flag(frame.hasCheckpoint);
    if (frame.tick>UINT64_MAX-36000) throw std::runtime_error("Invalid boid frame tick");
    if (frame.hasCheckpoint) {
        auto& c=frame.checkpoint;
        w.integer(c.version); w.integer(c.seed); w.integer(c.tick);
        w.list(c.birds,maxNetworkBirds,[&](viewer::boid::Bird& b) {
            fixed(w,b.position); fixed(w,b.velocity); fixed(w,b.positionRemainder); fixed(w,b.velocityRemainder); w.flag(b.passedPlayer);
        });
        w.list(c.flocks,viewer::boid::maxFlocks,[&](viewer::boid::Flock& f) {
            w.integer(f.firstBird); w.integer(f.birdCount); fixed(w,f.center); fixed(w,f.velocity); control(w,f.control);
        });
        viewer::boid::validate(c);
        if (c.tick!=frame.tick) throw std::runtime_error("Boid checkpoint tick mismatch");
    }
    w.list(frame.commands,maxBirdCommands,[&](viewer::boid::Command& c) {
        w.integer(c.tick); w.integer(c.flock); control(w,c.control);
        if (!c.tick || c.tick>frame.tick || c.flock>=viewer::boid::maxFlocks)
            throw std::runtime_error("Invalid boid command envelope");
    });
    if (frame.hasCheckpoint && !frame.commands.empty()) throw std::runtime_error("Checkpoint carries boid commands");
    std::uint64_t previous=0;
    for (const auto& c:frame.commands) {
        if (c.tick<previous) throw std::runtime_error("Unordered boid commands");
        previous=c.tick;
    }
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
    bool hasView = batch.view.has_value();
    w.flag(hasView);
    if (!hasView) { batch.view.reset(); return; }
    if (w.reading) batch.view.emplace();
    auto& view = *batch.view;
    w.vector(view.eye); w.number(view.yaw); w.number(view.pitch);
    w.number(view.verticalFov); w.number(view.aspect); w.number(view.maxDistance);
    if (std::abs(view.yaw) > 180 || std::abs(view.pitch) > 89 ||
        view.verticalFov < 1 || view.verticalFov > 150 || view.aspect < .2f || view.aspect > 8 ||
        view.maxDistance < 1 || view.maxDistance > 10000)
        throw std::runtime_error("Invalid replication view");
}

// Signed millimetres occupy three bytes in two's-complement form. Conversion
// uses wide arithmetic; no signed shifts or implementation-defined extension.
void signed24(Wire& w, std::int32_t& value) {
    std::uint32_t bits = w.reading ? 0 : static_cast<std::uint32_t>(value) & 0xffffffu;
    for (int byte = 0; byte < 3; ++byte) {
        auto part = static_cast<std::uint8_t>(bits >> (byte * 8));
        w.integer(part);
        if (w.reading) bits |= std::uint32_t(part) << (byte * 8);
    }
    if (w.reading) value = static_cast<std::int32_t>((bits & 0x800000u) ?
        std::int64_t(bits) - 0x1000000 : std::int64_t(bits));
}
std::uint8_t zombieCadence(std::uint8_t period) {
    if (period == 3) return 0;
    if (period == 12) return 1;
    if (period == 60) return 2;
    throw std::runtime_error("Invalid zombie update cadence");
}
void zombieMotion(Wire& w, ZombieState& z) {
    constexpr double tau = 6.28318530717958647692;
    constexpr std::uint8_t floatPosition = 8;
    glm::vec3 position{0};
    std::array<std::int32_t, 3> millimetres{};
    std::uint8_t flags = 0, heading = 0;
    if (!w.reading) {
        for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r)
            if (!std::isfinite(z.root[c][r]) || std::abs(z.root[c][r]) > 1e8f)
                throw std::runtime_error("Invalid zombie root");
        const double yaw = std::atan2(double(z.root[2].x), double(z.root[2].z));
        const auto rigid = glm::rotate(glm::mat4(1), static_cast<float>(yaw), glm::vec3(0, 1, 0));
        for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r)
            if (c != 3 && std::abs(z.root[c][r] - rigid[c][r]) > .001f)
                throw std::runtime_error("Compact zombie root must be upright and rigid");
        if (std::abs(z.root[3][3] - 1) > .001f) throw std::runtime_error("Invalid zombie root translation");
        position = glm::vec3(z.root[3]);
        bool fallback = false;
        for (int axis = 0; axis < 3; ++axis) {
            const auto rounded = std::llround(double(position[axis]) * 1000);
            if (rounded < -8388608 || rounded > 8388607) fallback = true;
            else millimetres[axis] = static_cast<std::int32_t>(rounded);
        }
        const auto rounded = std::llround(yaw * 256 / tau);
        heading = static_cast<std::uint8_t>((rounded % 256 + 256) % 256);
        flags = static_cast<std::uint8_t>((z.walking ? 1 : 0) | (z.running ? 2 : 0) |
            (z.dead ? 4 : 0) | (fallback ? floatPosition : 0) | (zombieCadence(z.updatePeriodTicks) << 4));
    }
    w.integer(z.id); w.integer(flags);
    if (flags & 0xc0u || (flags >> 4) == 3) throw std::runtime_error("Invalid zombie motion flags");
    if (flags & floatPosition) w.vector(position);
    else for (int axis = 0; axis < 3; ++axis) {
        signed24(w, millimetres[axis]);
        if (w.reading) position[axis] = static_cast<float>(double(millimetres[axis]) / 1000);
    }
    w.integer(heading); w.number(z.walkSeconds);
    if (z.id >= maxZombies || z.walkSeconds < 0) throw std::runtime_error("Invalid zombie motion");
    if (w.reading) {
        z.walking = (flags & 1) != 0; z.running = (flags & 2) != 0; z.dead = (flags & 4) != 0;
        z.updatePeriodTicks = std::array<std::uint8_t, 3>{3, 12, 60}.at(flags >> 4);
        z.root = glm::rotate(glm::translate(glm::mat4(1), position),
            static_cast<float>(double(heading) * tau / 256), glm::vec3(0, 1, 0));
        z.transform = z.root;
    }
}
void payload(Wire& w, Snapshot& s) {
    w.integer(s.tick);
    w.list(s.players, maxPlayers, [&](PlayerState& p) {
        w.integer(p.id); w.integer(p.resolved); w.number(p.health);
        if (p.health < 0 || p.health > 100) throw std::runtime_error("Invalid player health");
        movement(w, p.movement);
        w.number(p.yaw); w.number(p.pitch); w.integer(p.slot); w.integer(p.loaded); w.integer(p.reserve); w.integer(p.shots);
        w.flag(p.weaponCoolingDown);
        w.integer(p.vehicle); w.integer(p.seat);
        w.flag(p.melee.enabled);
        if (p.melee.enabled) {
            if (!w.reading && !viewer::validPlayerBodyPose(p.melee)) throw std::runtime_error("Invalid melee player pose");
            w.vector(p.melee.eye); w.integer(p.melee.striking); w.integer(p.melee.returning);
            for (auto& part : p.melee.parts) w.bodyPart(part);
        }
        if (!viewer::validPlayerBodyPose(p.melee) ||
            (p.melee.enabled && (p.slot != viewer::punchSlot || p.vehicle != noVehicle)))
            throw std::runtime_error("Invalid melee player pose");
        w.number(p.weaponSwitchRemaining); w.number(p.weaponSwitchDuration);
        if (p.weaponSwitchRemaining < 0 || p.weaponSwitchDuration < p.weaponSwitchRemaining)
            throw std::runtime_error("Invalid weapon switch progress");
        w.number(p.reloadRemaining); w.number(p.reloadDuration);
        if (p.reloadRemaining < 0 || p.reloadDuration < p.reloadRemaining)
            throw std::runtime_error("Invalid reload progress");
        w.list(p.ammoQuantities, 1024, [&](std::uint32_t& count) { w.integer(count); });
        w.flag(p.inventoryDragging);
        w.list(p.inventoryPlacements, 1024, [&](viewer::ItemPlacement& placement) {
            w.text(placement.storage, 192);
            if (!w.reading && (placement.x < 0 || placement.x > 15 || placement.y < 0 || placement.y > 15 ||
                placement.rotation < 0 || placement.rotation > 270 || placement.rotation % 90))
                throw std::runtime_error("Invalid inventory placement");
            auto x = static_cast<std::uint8_t>(placement.x), y = static_cast<std::uint8_t>(placement.y);
            auto rotation = static_cast<std::uint8_t>(placement.rotation / 90);
            w.integer(x); w.integer(y); w.integer(rotation);
            if (x > 15 || y > 15 || rotation > 3) throw std::runtime_error("Invalid inventory placement");
            placement.x = x; placement.y = y; placement.rotation = rotation * 90;
        });
        if (p.inventoryDragging && !p.inventoryPlacements.empty()) throw std::runtime_error("Invalid held inventory snapshot");
        if (!p.id || p.slot > viewer::punchSlot || std::abs(p.yaw) > 180 || std::abs(p.pitch) > 89 ||
            (p.vehicle != noVehicle && (p.vehicle >= maxVehicles || p.seat >= viewer::maxVehicleSeats)) ||
            (p.vehicle == noVehicle && p.seat != noVehicleSeat))
            throw std::runtime_error("Invalid player snapshot");
    });
    w.list(s.zombies, maxZombieUpdatesPerSnapshot, [&](ZombieState& z) { zombieMotion(w, z); });
    w.list(s.doors, maxDoors, [&](DoorState& d) {
        w.integer(d.id); w.number(d.angle);
        if (std::abs(d.angle) > 4) throw std::runtime_error("Invalid door snapshot");
    });
    w.list(s.boxes, maxBoxes, [&](BoxState& b) {
        // IDs use only nine bits. Pack retirement into the high bit to retain
        // the existing full-fleet snapshot byte budget.
        constexpr std::uint32_t removedBit = 0x80000000u;
        if (!w.reading && b.id >= maxBoxes) throw std::runtime_error("Invalid box snapshot ID");
        std::uint32_t id = b.id | (b.removed ? removedBit : 0);
        w.integer(id);
        if (w.reading) { b.id = id & ~removedBit; b.removed = (id & removedBit) != 0; }
        w.vector(b.center); w.vector(b.halfExtents);
        w.number(b.rotation.w); w.number(b.rotation.x); w.number(b.rotation.y); w.number(b.rotation.z);
        if (b.id >= maxBoxes || glm::any(glm::lessThan(b.halfExtents, glm::vec3(.05f))) ||
            glm::any(glm::greaterThan(b.halfExtents, glm::vec3(2))) || std::abs(glm::length(b.rotation) - 1) > .001f)
            throw std::runtime_error("Invalid box snapshot");
    });
    w.list(s.grenades, viewer::Grenades::maxActive, [&](viewer::GrenadePose& g) {
        w.integer(g.id); w.vector(g.center);
        w.number(g.rotation.w); w.number(g.rotation.x); w.number(g.rotation.y); w.number(g.rotation.z);
        if (!g.id || std::abs(glm::length(g.rotation) - 1) > .001f) throw std::runtime_error("Invalid grenade snapshot");
    });
    w.list(s.explosions, viewer::Grenades::maxEvents, [&](viewer::GrenadeExplosion& e) {
        w.integer(e.id); w.vector(e.position); w.number(e.age);
        if (!e.id || e.age < 0 || e.age > 2) throw std::runtime_error("Invalid explosion event");
    });
    uniqueIds(s.players); uniqueIds(s.zombies); uniqueIds(s.doors); uniqueIds(s.boxes);
    uniqueIds(s.grenades); uniqueIds(s.explosions);
    w.list(s.woundContents, 1024 * viewer::ZombieCavityPhysics::maxObjectCount,
        [&](glm::mat4& transform) { w.matrix(transform); });
    w.list(s.bloodMist, viewer::BloodMist::capacity, [&](viewer::BloodMistBurst& burst) {
        w.integer(burst.id); w.vector(burst.position); w.number(burst.age);
        auto size = static_cast<std::uint8_t>(burst.size);
        w.integer(size);
        if (!burst.id || size > 2 || burst.age < 0 || burst.age > viewer::BloodMist::historySeconds)
            throw std::runtime_error("Invalid blood mist event");
        burst.size = static_cast<viewer::BloodMistSize>(size);
    });
    uniqueIds(s.bloodMist);

    w.list(s.vehicles, maxVehicles, [&](VehicleState& v) {
        w.integer(v.id); w.integer(v.type); w.vector(v.velocity);
        const auto pose = [&](RigidPose& p) {
            w.vector(p.center);
            w.number(p.rotation.w); w.number(p.rotation.x); w.number(p.rotation.y); w.number(p.rotation.z);
            if (std::abs(glm::length(p.rotation) - 1) > .001f)
                throw std::runtime_error("Invalid vehicle rotation");
        };
        pose(v.chassis);
        for (auto& wheel : v.wheels) pose(wheel);
        if (v.id >= maxVehicles || v.type >= viewer::maxVehicleTypes) throw std::runtime_error("Invalid vehicle ID");
    });
    w.list(s.signals, maxSignals, [&](SignalState& signal) {
        w.integer(signal.id);
        auto kind = static_cast<std::uint8_t>(signal.kind); w.integer(kind);
        w.vector(signal.position); w.integer(signal.expires);
        if (!signal.id || kind < 1 || kind > 3 || signal.expires <= s.tick || signal.expires - s.tick > signalLifetimeTicks)
            throw std::runtime_error("Invalid signal snapshot");
        signal.kind = static_cast<SignalKind>(kind);
    });
    std::map<std::uint32_t, std::size_t> signalCounts;
    for (const auto& signal : s.signals)
        if (++signalCounts[signal.id] > maxSignalsPerPlayer) throw std::runtime_error("Too many signals from one player");
    if (signalCounts.size() > maxPlayers) throw std::runtime_error("Too many signal senders");
    uniqueIds(s.vehicles);
    std::set<std::uint64_t> soundIds;
    w.list(s.sounds, maxSoundEvents, [&](SoundEvent& sound) {
        w.integer(sound.id); w.integer(sound.tick);
        auto kind = static_cast<std::uint8_t>(sound.kind); w.integer(kind);
        w.integer(sound.source); w.integer(sound.weapon); w.vector(sound.position);
        if (kind > static_cast<std::uint8_t>(SoundKind::chase)) throw std::runtime_error("Invalid sound kind");
        sound.kind = static_cast<SoundKind>(kind);
        if (!sound.id || sound.tick > s.tick || s.tick - sound.tick > soundHistoryTicks ||
            !soundIds.insert(sound.id).second || (playerSound(sound.kind) ? !sound.source : sound.source >= maxZombies) ||
            ((sound.kind == SoundKind::shot || sound.kind == SoundKind::reload) ? sound.weapon >= 4 : sound.weapon != 0))
            throw std::runtime_error("Invalid sound event");
    });
    std::set<std::pair<std::uint32_t, std::uint8_t>> occupied;
    for (const auto& p : s.players) if (p.vehicle != noVehicle) {
        if (!occupied.emplace(p.vehicle, p.seat).second || std::none_of(s.vehicles.begin(), s.vehicles.end(),
                [&](const auto& v) { return v.id == p.vehicle; }))
            throw std::runtime_error("Invalid vehicle occupancy");
    }
}
void payload(Wire& w, CorpsePose& pose) {
    w.integer(pose.id); w.integer(pose.asset); w.integer(pose.version);
    w.matrix(pose.transform); w.matrix(pose.root);
    w.list(pose.bones, maxBones, [&](glm::mat4& m) { w.matrix(m); });
    if (!w.reading && (pose.severedJoint < -1 || pose.severedJoint >= int(viewer::ragdollPartCount - 1)))
        throw std::runtime_error("Invalid severed joint");
    std::uint8_t separation = static_cast<std::uint8_t>(pose.severedJoint + 1);
    w.integer(separation);
    pose.severedJoint = static_cast<std::int16_t>(int(separation) - 1);
    if (pose.id >= maxZombies || pose.asset >= maxZombies || !pose.version)
        throw std::runtime_error("Invalid corpse pose");
    if (pose.severedJoint < -1 || pose.severedJoint >= int(viewer::ragdollPartCount - 1))
        throw std::runtime_error("Invalid severed joint");
}
void payload(Wire& w, PoseAck& ack) {
    w.integer(ack.id); w.integer(ack.version);
    if (ack.id >= maxZombies || !ack.version) throw std::runtime_error("Invalid corpse acknowledgement");
}
void payload(Wire& w, ZombieSetups& batch) {
    w.list(batch.zombies, maxZombieSetupsPerBatch, [&](ZombieSetup& z) {
        w.integer(z.id); w.integer(z.asset); w.integer(z.version);
        w.matrix(z.normalization); w.number(z.phase);
        w.list(z.woundMask, 1024, [&](std::uint8_t& value) { w.integer(value); });
        if (z.id >= maxZombies || z.asset >= maxZombies || !z.version || z.phase < 0 || z.phase >= 1)
            throw std::runtime_error("Invalid zombie setup");
        if (z.normalization[0][3] != 0 || z.normalization[1][3] != 0 ||
            z.normalization[2][3] != 0 || z.normalization[3][3] != 1)
            throw std::runtime_error("Zombie normalization must be affine");
        const double determinant = glm::determinant(glm::dmat3(z.normalization));
        if (!std::isfinite(determinant) || determinant == 0)
            throw std::runtime_error("Zombie normalization must be invertible");
    });
    uniqueIds(batch.zombies);
}
void payload(Wire& w, ZombieSetupAck& ack) {
    w.list(ack.zombies, maxZombieSetupsPerBatch, [&](ZombieSetupReceipt& receipt) {
        w.integer(receipt.id); w.integer(receipt.version);
        if (receipt.id >= maxZombies || !receipt.version) throw std::runtime_error("Invalid zombie setup acknowledgement");
    });
    uniqueIds(ack.zombies);
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
    if (magic != 0x4c4c4447 || version != protocolVersion || type > 11) throw std::runtime_error("Incompatible multiplayer protocol");
    Message result;
    switch (type) { case 0: result = Hello{}; break; case 1: result = Welcome{}; break;
        case 2: result = Commands{}; break; case 3: result = Snapshot{}; break;
        case 4: result = Goodbye{}; break; case 5: result = Fragment{}; break;
        case 6: result = CorpsePose{}; break; case 7: result = PoseAck{}; break;
        case 8: result = BirdFrame{}; break; case 9: result = BirdResync{}; break;
        case 10: result = ZombieSetups{}; break; case 11: result = ZombieSetupAck{}; break; }
    std::visit([&](auto& value) { payload(w, value); }, result);
    if (w.offset != bytes) throw std::runtime_error("Trailing multiplayer message data");
    return result;
}
} // namespace multiplayer
