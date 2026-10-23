#include "world.hpp"
#include "simulation_clock.hpp"
#include "zombie_animation.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace multiplayer {
ServerPlayer::ServerPlayer(const viewer::Weapons& initialWeapons) : weapons(initialWeapons) {
    held.slot = static_cast<std::uint8_t>(weapons.slot());
}
World::World(Scene& scene) : scene_(scene),
    physicsScene_(scene.physics, *scene.collision, scene.model.boundsMin, scene.model.boundsMax),
    interiors_(scene.model, *scene.doors, 1), zombieDoors_(scene.physics) {
    viewer::SimulationClock::instance().reset();
    scene.zombies.attachPhysics(scene.physics);
}
std::uint32_t World::join() {
    if (players_.size() >= maxPlayers || nextPlayer_ == 0) throw std::runtime_error("Server full");
    const auto id = nextPlayer_++;
    auto& player = players_.try_emplace(id, scene_.initialWeapons).first->second;
    player.undergrowth = viewer::UndergrowthEmitter(id);
    auto spawn = scene_.spawnEye;
    spawn.x += static_cast<float>((id - 1) % maxPlayers) * 1.0f;
    if (!player.controller.spawn(spawn, *scene_.collision)) { players_.erase(id); throw std::runtime_error("No safe player spawn"); }
    player.camera.setPosition(player.controller.eyePosition());
    player.camera.setOrientation(scene_.spawnOrientation.x, scene_.spawnOrientation.y);
    player.held.movement.yawDegrees = scene_.spawnOrientation.x;
    player.held.pitch = scene_.spawnOrientation.y;
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
    struct Shot { glm::vec3 origin; std::vector<glm::vec3> directions; viewer::ShotCaliber caliber; };
    std::vector<Shot> shots;
    for (auto& [id, player] : players_) {
        // A small startup buffer absorbs ordinary packet jitter without delaying local prediction.
        if (!player.started && (player.pending.size() >= 3 || tick_ - player.lastPacketTick > 3)) player.started = true;
        // Drain jitter backlogs using the existing 120 Hz substeps, never extra movement time.
        const int inputs = player.pending.size() > 3 ? 2 : 1;
        for (int input = 0; input < inputs; ++input) {
            auto command = player.held;
            bool consumed = false;
            command.movement.toggleProne = false; command.reload = false; command.drag = {};
            if (player.started && !player.pending.empty()) {
                const auto first = player.pending.begin();
                if (first->first == player.resolved + 1) {
                    command = first->second; player.resolved = first->first; player.pending.erase(first);
                    consumed = true;
                }
            }
            // Timeout only synthesized held input; queued actions must execute before acknowledgement.
            if (!consumed && tick_ - player.lastPacketTick > 12) {
                command.movement = {}; command.movement.yawDegrees = player.camera.yawDegrees();
                command.left = command.right = command.reload = false;
                player.interaction.cancel();
            }
            const auto previousFeet = player.controller.feet();
            const viewer::UndergrowthMotion shrubBefore{previousFeet, player.controller.bodyHeight(),
                player.controller.config().radius, player.controller.grounded(), false};
            player.controller.update(tickSeconds / inputs, command.movement, *scene_.collision);
            const auto& controller = player.controller;
            const bool running = command.movement.run && controller.stance() == viewer::PlayerStance::Standing;
            if (const auto event = player.undergrowth.update(tickSeconds / inputs, shrubBefore,
                    {controller.feet(), controller.bodyHeight(), controller.config().radius, controller.grounded(), running},
                    scene_.undergrowth))
                noises.push_back(event->noise);
            const auto playerVelocity = (player.controller.feet() - previousFeet) / (tickSeconds / inputs);
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
                player.interaction.update(player.camera, tickSeconds / inputs);
            }
            const bool switchedWeapon = player.weapons.slot() != command.slot && player.weapons.equip(command.slot);
            if (command.reload) player.weapons.reload();
            // Resetting Weapons must not turn an already-held button into a new press.
            if (consumed && !command.left) player.triggerNeedsRelease = false;
            if ((switchedWeapon || command.reload) && command.left && player.held.left) player.triggerNeedsRelease = true;
            player.weapons.trigger(command.left && !command.interaction && !player.triggerNeedsRelease);
            // Accept before the next input can reload/switch weapons or change aim.
            const auto& config = player.weapons.equipped().config;
            const bool canThrow = !config.grenade || scene_.grenades->canThrow(player.camera.position(), *scene_.collision);
            if (!canThrow) {
                player.weapons.cancelTrigger();
                player.triggerNeedsRelease |= command.left;
            }
            if (canThrow && player.weapons.fireAttempt(now) == viewer::WeaponFireResult::fired) {
                ++player.shots;
                if (config.grenade) {
                    if (!scene_.grenades->throwFrom(player.camera.position(), player.camera.direction(), playerVelocity,
                            now, *config.grenade, *scene_.collision)) throw std::logic_error("Accepted grenade throw failed");
                } else shots.push_back({player.camera.position(), viewer::weaponShotDirections(config, player.camera.direction()), config.caliber});
            }
            player.held = command;
        }
        scene_.doors->activateNear(player.controller.feet());
        targets.push_back({player.controller.feet(), player.controller.eyeHeight(), player.controller.bodyHeight(), id});
    }
    physicsScene_.prepare(*scene_.boxes);
    scene_.grenades->prepare(physicsScene_, *scene_.doors);
    for (std::size_t i = 0; i < scene_.boxes->count(); ++i) scene_.doors->activateNear(scene_.boxes->pose(i).center, 8);
    for (const auto id : scene_.zombies.ragdollIds()) scene_.doors->activateNear(glm::vec3(scene_.zombies.instances[id].transform[3]));
    zombieDoors_.prepare(*scene_.doors, scene_.zombies);
    scene_.zombies.prepareTorsoPhysics(tickSeconds, now, &physicsScene_);
    scene_.physics->step(tickSeconds);
    scene_.grenades->update(now, *scene_.collision, &scene_.zombies, now);
    scene_.zombies.synchronizePhysics();
    scene_.zombies.updateBehaviorForPlayers(tickSeconds, targets, *scene_.collision, now);
    for (const auto& noise : noises) scene_.zombies.makeNoise(noise.position, noise.decibels);
    for (const auto& shot : shots) {
        scene_.zombies.makeNoise(shot.origin, 110);
        // Match the current prototype's server-tick hit pose; bounded historical hit testing can be added independently.
        for (const auto direction : shot.directions) {
            scene_.zombies.shootWithOcclusion(shot.origin, direction, now, *scene_.collision, shot.caliber);
        }
    }
}
Snapshot World::snapshot(std::uint32_t id) {
    auto& observer = players_.at(id);
    Snapshot s; s.tick = tick_;
    s.grenades = scene_.grenades->poses();
    s.explosions = scene_.grenades->explosions(viewer::SimulationClock::instance().seconds());
    s.bloodMist = scene_.zombies.bloodMist().recent(viewer::SimulationClock::instance().seconds());
    for (const auto& [playerId, player] : players_) {
        const auto& weapon = player.weapons.equipped();
        s.players.push_back({playerId, player.resolved, player.controller.movementState(),
            player.camera.yawDegrees(), player.camera.pitchDegrees(), static_cast<std::uint8_t>(player.weapons.slot()),
            static_cast<std::uint32_t>(weapon.loaded), static_cast<std::uint32_t>(weapon.reserve), player.shots,
            viewer::SimulationClock::instance().seconds() < weapon.readyAt});
    }
    return observer.replication.snapshot(scene_, observer.controller.feet(), std::move(s));
}
std::optional<CorpsePose> World::nextPose(std::uint32_t id) const {
    return players_.at(id).replication.nextPose(scene_);
}
void World::poseSent(std::uint32_t id, const CorpsePose& pose) {
    players_.at(id).replication.sent(pose);
}
void World::acknowledgePose(std::uint32_t id, const PoseAck& ack) {
    players_.at(id).replication.acknowledge(ack);
}
} // namespace multiplayer
