#include "world.hpp"
#include "simulation_clock.hpp"
#include "zombie_animation.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace multiplayer {
ServerPlayer::ServerPlayer() : weapons(std::filesystem::path(MP_ROOT) / "items", std::filesystem::path(MP_ROOT) / "inventory.toml") {}
World::World(Scene& scene) : scene_(scene),
    physicsScene_(scene.physics, *scene.collision, scene.model.boundsMin, scene.model.boundsMax),
    interiors_(scene.model, *scene.doors, 1), zombieDoors_(scene.physics) {
    viewer::SimulationClock::instance().reset();
    scene.zombies.attachPhysics(scene.physics);
}
std::uint32_t World::join() {
    if (players_.size() >= maxPlayers || nextPlayer_ == 0) throw std::runtime_error("Server full");
    const auto id = nextPlayer_++;
    auto& player = players_.try_emplace(id).first->second;
    auto spawn = scene_.spawnEye;
    spawn.x += static_cast<float>((id - 1) % maxPlayers) * 1.0f;
    if (!player.controller.spawn(spawn, *scene_.collision)) { players_.erase(id); throw std::runtime_error("No safe player spawn"); }
    player.camera.setPosition(player.controller.eyePosition());
    player.camera.setOrientation(-90, 0);
    player.lastPacketTick = tick_;
    return id;
}
void World::leave(std::uint32_t id) {
    const auto it = players_.find(id);
    if (it != players_.end()) { it->second.interaction.cancel(); players_.erase(it); }
}
void World::submit(std::uint32_t id, const Commands& commands) {
    auto& player = players_.at(id);
    if (commands.snapshotTick > tick_) throw std::runtime_error("Client acknowledged a future snapshot");
    player.acknowledgedSnapshot = std::max(player.acknowledgedSnapshot, commands.snapshotTick);
    for (const auto& c : commands.commands) {
        if (c.sequence <= player.resolved) continue;
        if (c.sequence - player.resolved > 180) throw std::runtime_error("Client command window exceeded");
        player.pending.emplace(c.sequence, c);
    }
    player.lastPacketTick = tick_;
}
void World::step() {
    ++tick_;
    auto& clock = viewer::SimulationClock::instance();
    clock.advance(tickSeconds);
    const double now = clock.seconds();
    std::vector<viewer::ZombiePlayer> targets;
    std::vector<viewer::NoiseEvent> noises;
    for (auto& [id, player] : players_) {
        auto command = player.held;
        command.movement.toggleProne = false; command.reload = false; command.drag = {};
        // A small startup buffer absorbs ordinary packet jitter without delaying local prediction.
        if (!player.started && (player.pending.size() >= 3 || tick_ - player.lastPacketTick > 3)) player.started = true;
        if (player.started && !player.pending.empty()) {
            const auto first = player.pending.begin();
            if (first->first == player.resolved + 1 || player.pending.size() >= 3 || tick_ - player.lastPacketTick > 3) {
                command = first->second; player.resolved = first->first; player.pending.erase(first);
            }
        }
        if (tick_ - player.lastPacketTick > 12) {
            command.movement = {}; command.movement.yawDegrees = player.camera.yawDegrees();
            command.left = command.right = command.reload = false;
            player.interaction.cancel();
        }
        player.controller.update(tickSeconds, command.movement, *scene_.collision);
        noises.insert(noises.end(), player.controller.noises().begin(), player.controller.noises().end());
        player.camera.setPosition(player.controller.eyePosition());
        player.camera.setOrientation(command.movement.yawDegrees, command.pitch);
        const bool mode = player.interaction.mode() == viewer::ControlMode::Interaction;
        if (mode != command.interaction) player.interaction.toggleMode();
        if (command.interaction) {
            for (const auto hand : {viewer::Hand::Left, viewer::Hand::Right}) {
                const bool held = hand == viewer::Hand::Left ? command.left : command.right;
                const bool wasHeld = hand == viewer::Hand::Left ? player.held.left : player.held.right;
                if (!held && wasHeld) player.interaction.release(hand, now);
                if (held && (!wasHeld || !player.held.interaction)) {
                    player.interaction.press(hand, player.camera, *scene_.doors, *scene_.boxes, *scene_.collision, now);
                    bool conflict = false;
                    for (const auto& [otherId, other] : players_) {
                        if (otherId == id) continue;
                        const auto box = player.interaction.heldBox(hand);
                        const auto door = player.interaction.grabbedDoor();
                        conflict |= box && (box == other.interaction.heldBox(viewer::Hand::Left) || box == other.interaction.heldBox(viewer::Hand::Right));
                        conflict |= door && door == other.interaction.grabbedDoor();
                    }
                    if (conflict) player.interaction.cancel();
                    if (const auto door = player.interaction.grabbedDoor()) {
                        // shortcut: stop populating before the fixed protocol prop budget; raise/version it for larger sessions.
                        if (scene_.boxes->count() + 64 < maxBoxes)
                            interiors_.populate(*door, *scene_.collision, physicsScene_, *scene_.boxes);
                    }
                }
            }
            player.interaction.look(player.camera, command.drag.x, command.drag.y, now);
            player.camera.setOrientation(command.movement.yawDegrees, command.pitch);
            player.interaction.update(player.camera, tickSeconds);
        }
        player.weapons.equip(command.slot);
        if (command.reload) player.weapons.reload();
        player.weapons.trigger(command.left && !command.interaction);
        scene_.doors->activateNear(player.controller.feet());
        targets.push_back({player.controller.feet(), player.controller.eyeHeight(), player.controller.bodyHeight(), id});
        player.held = command;
    }
    physicsScene_.prepare(*scene_.boxes);
    for (std::size_t i = 0; i < scene_.boxes->count(); ++i) scene_.doors->activateNear(scene_.boxes->pose(i).center, 8);
    for (const auto id : scene_.zombies.ragdollIds()) scene_.doors->activateNear(glm::vec3(scene_.zombies.instances[id].transform[3]));
    zombieDoors_.prepare(*scene_.doors, scene_.zombies);
    scene_.physics->step(tickSeconds);
    scene_.zombies.synchronizePhysics();
    scene_.zombies.updateBehaviorForPlayers(tickSeconds, targets, *scene_.collision, now);
    for (const auto& noise : noises) scene_.zombies.makeNoise(noise.position, noise.decibels);
    for (auto& [id, player] : players_) {
        (void)id;
        if (player.weapons.fireAttempt(now) != viewer::WeaponFireResult::fired) continue;
        ++player.shots;
        scene_.zombies.makeNoise(player.camera.position(), 110);
        // Match the current prototype's server-tick hit pose; bounded historical hit testing can be added independently.
        for (const auto direction : viewer::weaponShotDirections(player.weapons.equipped().config, player.camera.direction())) {
            scene_.zombies.shootWithOcclusion(player.camera.position(), direction, now, *scene_.collision);
        }
    }
}
Snapshot World::snapshot(std::uint32_t id) {
    auto& observer = players_.at(id);
    Snapshot s; s.tick = tick_;
    for (const auto& [playerId, player] : players_) {
        const auto& weapon = player.weapons.equipped();
        s.players.push_back({playerId, player.resolved, player.controller.movementState(),
            player.camera.yawDegrees(), player.camera.pitchDegrees(), static_cast<std::uint8_t>(player.weapons.slot()),
            static_cast<std::uint32_t>(weapon.loaded), static_cast<std::uint32_t>(weapon.reserve), player.shots});
    }
    for (const auto door : scene_.doors->activeDoors()) {
        const auto offset = scene_.doors->panel(door).center - observer.controller.feet();
        if (glm::dot(offset, offset) < 240.0f * 240 && s.doors.size() < maxDoors)
            s.doors.push_back({static_cast<std::uint32_t>(door), scene_.doors->angle(door)});
    }
    // Props are capped and persistent; sending all avoids missing late-join creations or collision changes.
    for (std::size_t i = 0; i < scene_.boxes->count(); ++i) {
        const auto p = scene_.boxes->pose(i);
        s.boxes.push_back({static_cast<std::uint32_t>(i), p.center, p.halfExtents, p.rotation});
    }
    std::vector<std::size_t> candidates;
    scene_.zombies.queryLivingNear(observer.controller.feet(), 240, candidates);
    candidates.insert(candidates.end(), scene_.zombies.ragdollIds().begin(), scene_.zombies.ragdollIds().end());
    const auto distance = [&](std::size_t z) {
        const auto& instance = scene_.zombies.instances[z];
        const glm::vec3 position(instance.ragdoll ? instance.transform[3] : instance.restRoot[3]);
        const auto d = position - observer.controller.feet(); return glm::dot(d, d);
    };
    std::sort(candidates.begin(), candidates.end(), [&](auto a, auto b) {
        const auto da = distance(a), db = distance(b); return da == db ? a < b : da < db;
    });
    std::set<std::size_t> interested;
    std::size_t budget = encode(s).size();
    for (const auto z : candidates) {
        const auto& instance = scene_.zombies.instances[z];
        const float radius = observer.interested.count(z) ? 240.0f : 220.0f;
        if (instance.removed || distance(z) > radius * radius || s.zombies.size() == maxVisibleZombies) continue;
        const auto sent = observer.sentPalettes.find(z);
        const bool palette = instance.ragdoll && !instance.ragdollBones.empty() &&
            (sent == observer.sentPalettes.end() || sent->second.first != instance.ragdollPoseVersion ||
             observer.acknowledgedSnapshot < sent->second.second);
        const auto bytes = 160 + (palette ? instance.ragdollBones.size() * sizeof(float) * 16 : 0);
        if (budget + bytes > maxMessageBytes - 1024) continue;
        if (instance.ragdollBones.size() > maxBones) throw std::runtime_error("Zombie skeleton exceeds protocol bone budget");
        budget += bytes;
        s.zombies.push_back({static_cast<std::uint32_t>(z), static_cast<std::uint32_t>(instance.asset),
            instance.transform, instance.restRoot, instance.phase, static_cast<float>(instance.walkSeconds),
            instance.walking, instance.running, instance.ragdoll, palette ? instance.ragdollBones : std::vector<glm::mat4>{}});
        if (palette && (sent == observer.sentPalettes.end() || sent->second.first != instance.ragdollPoseVersion))
            observer.sentPalettes[z] = {instance.ragdollPoseVersion, tick_};
        interested.insert(z);
    }
    // An acknowledgement of an out-of-interest snapshot proves nothing about
    // a previously lost palette. Reentry must resend any unconfirmed version.
    for (auto it = observer.sentPalettes.begin(); it != observer.sentPalettes.end();) {
        if (!interested.count(it->first) && observer.acknowledgedSnapshot < it->second.second)
            it = observer.sentPalettes.erase(it);
        else ++it;
    }
    observer.interested = std::move(interested);
    return s;
}
} // namespace multiplayer
