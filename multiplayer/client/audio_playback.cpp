#include "audio_playback.hpp"
#include "physics_prop.hpp"
#include "object_interaction.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <set>

namespace multiplayer {
namespace {
float height(viewer::PlayerStance stance) {
    const viewer::FPSConfig config;
    return stance == viewer::PlayerStance::Prone ? config.proneHeight :
        stance == viewer::PlayerStance::Crouched ? config.crouchHeight : config.height;
}
bool near(glm::vec3 a, glm::vec3 b) {
    const auto delta = a - b;
    return glm::dot(delta, delta) <= soundRange * soundRange;
}
}
void ClientAudio::receive(const Snapshot& snapshot, std::uint32_t self) {
    if (!enabled()) return;
    for (const auto& player : snapshot.players) if (player.id == self) weapons_.reconcile(player);
    for (const auto& cue : events_.receive(snapshot, self)) {
        if (pending_.size() == maxSoundEvents) pending_.pop_front();
        pending_.push_back(cue);
    }
}
void ClientAudio::weaponCommand(const Command& command, double now, const viewer::Weapons& definitions,
                               const viewer::PlayerInventory& inventory) {
    if (!enabled()) return;
    for (const auto cue : weapons_.advance(command, now, definitions, inventory))
        system_.playLocal(cue, cue == viewer::AudioEffect::emptyClick ? 1.f :
            cue == viewer::AudioEffect::reload || cue == viewer::AudioEffect::reloadRifle ||
            cue == viewer::AudioEffect::reloadShotgun ? .5f : .8f);
}
void ClientAudio::movementSounds(const std::vector<viewer::NoiseEvent>& noises) {
    constexpr std::array<viewer::AudioEffect, 4> clips{viewer::AudioEffect::footstep, viewer::AudioEffect::footstep2,
        viewer::AudioEffect::footstep3, viewer::AudioEffect::footstep4};
    for (const auto& noise : noises) {
        if (noise.kind == viewer::NoiseEvent::Kind::landing) system_.play(viewer::AudioEffect::landing, noise.position, .65f);
        else if (noise.kind == viewer::NoiseEvent::Kind::footstep) {
            const auto variation = step_++;
            system_.play(clips[variation % clips.size()], noise.position, .4f, .97f + .02f * (variation % 4));
        }
    }
}
void ClientAudio::localMovement(const viewer::FPSController& controller, const viewer::UndergrowthMotion& before,
        const viewer::UndergrowthIndex& shrubs, bool running, std::uint32_t self) {
    if (!enabled()) return;
    if (localPlayer_ != self) { localShrubs_ = viewer::UndergrowthEmitter(self); localPlayer_ = self; }
    movementSounds(controller.noises()); // Fresh prediction only; replay never calls this.
    if (const auto event = localShrubs_.update(tickSeconds, before,
            {controller.feet(), controller.bodyHeight(), controller.config().radius, controller.grounded(), running}, shrubs))
        viewer::playUndergrowth(system_, *event);
}
void ClientAudio::localDoor(const Command& command, const viewer::Camera& camera, const Scene& scene) {
    const bool pressed = command.interaction && command.left && !doorHeld_;
    doorHeld_ = command.interaction && command.left;
    if (!enabled() || !pressed || command.enterVehicle) return;
    const auto door = scene.doors->raycast(camera.position(), camera.direction(), 1.5f);
    const auto prop = viewer::raycastPhysicsProp(scene.boxes->physicsWorld(), camera.position(), camera.direction(),
                                                viewer::ObjectInteraction::rayReach(camera.direction()));
    if (door && (!prop || door->distance <= prop->distance) && std::abs(scene.doors->angle(door->door)) <= .025f &&
        scene.collision->lineOfSight(camera.position(), door->point))
        system_.play(viewer::AudioEffect::doorLatch, scene.doors->panel(door->door).center, .6f);
}
void ClientAudio::remoteMovement(const Scene& scene, const Snapshots& snapshots, std::uint32_t self, float seconds, glm::vec3 eye) {
    std::set<std::uint32_t> present;
    for (const auto& player : snapshots.players()) {
        if (player.id == self) continue;
        present.insert(player.id);
        auto [it, inserted] = players_.try_emplace(player.id);
        auto& audio = it->second;
        if (inserted) audio.shrubs = viewer::UndergrowthEmitter(player.id);
        const auto& state = player.movement;
        const auto delta = state.feet - audio.previous.feet;
        if (audio.ready && player.vehicle == noVehicle && seconds > 0 && seconds <= .1f &&
            glm::length(delta) < 5 && near(state.feet, eye)) {
            // Range is checked by the backend; player count is capped at eight.
            const bool running = state.stance == viewer::PlayerStance::Standing &&
                glm::length(glm::vec2(delta.x, delta.z)) / seconds > 4;
            movementSounds(audio.steps.update(seconds, {audio.previous.feet, audio.previous.grounded},
                                               {state.feet, state.grounded}, running));
            if (const auto event = audio.shrubs.update(seconds,
                    {audio.previous.feet, height(audio.previous.stance), .3f, audio.previous.grounded, false},
                    {state.feet, height(state.stance), .3f, state.grounded, running}, scene.undergrowth))
                viewer::playUndergrowth(system_, *event);
        } else { audio.steps.reset(); audio.shrubs.reset(); }
        audio.previous = state;
        audio.ready = player.vehicle == noVehicle;
    }
    for (auto it = players_.begin(); it != players_.end();) {
        if (!present.count(it->first)) it = players_.erase(it); else ++it;
    }
}
void ClientAudio::zombieSounds(const Scene& scene, const Snapshots& snapshots, glm::vec3 eye, double now) {
    const auto eligible = [&](std::uint32_t id) {
        if (id >= scene.zombies.instances.size()) return false;
        const auto& zombie = scene.zombies.instances[id];
        return !zombie.removed && !zombie.ragdoll && near(eye, glm::vec3(zombie.restRoot[3]) + glm::vec3(0, 1.5f, 0));
    };
    sampledZombies_.erase(std::remove_if(sampledZombies_.begin(), sampledZombies_.end(),
        [&](auto id) { return !eligible(id); }), sampledZombies_.end());
    if (now >= nextSample_) {
        const auto& tracked = snapshots.trackedZombieIds();
        for (int attempt = 0; attempt < 10 && sampledZombies_.size() < 10 && !tracked.empty(); ++attempt) {
            random_ ^= random_ << 13; random_ ^= random_ >> 17; random_ ^= random_ << 5;
            const auto id = tracked[random_ % tracked.size()];
            if (eligible(id) && std::find(sampledZombies_.begin(), sampledZombies_.end(), id) == sampledZombies_.end())
                sampledZombies_.push_back(id);
        }
        nextSample_ = now + .2;
    }
    std::vector<viewer::AudioEmitter> emitters;
    for (const auto id : sampledZombies_)
        emitters.push_back({id, glm::vec3(scene.zombies.instances[id].restRoot[3]) + glm::vec3(0, 1.5f, 0)});
    for (auto it = reactions_.begin(); it != reactions_.end();) {
        if (now >= it->second.expires || (it->first < scene.zombies.instances.size() && scene.zombies.instances[it->first].ragdoll))
            it = reactions_.erase(it);
        else { emitters.push_back({it->first, it->second.position}); ++it; }
    }
    std::vector<viewer::ZombieAudioSample> samples;
    for (const auto id : system_.setZombieEmitters(emitters)) {
        if (id >= scene.zombies.instances.size()) continue;
        const auto& zombie = scene.zombies.instances[id];
        if (!zombie.removed && !zombie.ragdoll)
            samples.push_back({id, glm::vec3(zombie.restRoot[3]), zombie.walkSeconds, zombie.walking, zombie.running, 0, 0, -1, -1});
    }
    for (const auto& cue : zombies_.update(now, samples)) {
        if (cue.zombie) system_.playZombie(*cue.zombie, cue.effect, cue.priority, cue.gain, cue.pitch);
        else system_.play(cue.effect, cue.position, cue.gain, cue.pitch);
    }
}
void ClientAudio::update(const viewer::Camera& camera, const Scene& scene, const Snapshots& snapshots,
                         std::uint32_t self, float seconds, double now) {
    if (!enabled()) return;
    system_.setListener(camera.position(), camera.direction());
    remoteMovement(scene, snapshots, self, seconds, camera.position());
    for (const auto& cue : pending_) if (cue.zombie) {
        if (reactions_.size() >= maxSoundEvents) reactions_.erase(reactions_.begin());
        reactions_[*cue.zombie] = {cue.position, now + 1.5};
    }
    zombieSounds(scene, snapshots, camera.position(), now);
    for (const auto& cue : pending_) {
        if (cue.zombie) system_.playZombie(*cue.zombie, cue.effect, cue.priority, cue.gain, cue.pitch);
        else system_.play(cue.effect, cue.position, cue.gain, cue.pitch);
    }
    pending_.clear();
    std::vector<viewer::DoorAudioSample> samples;
    for (const auto& door : scene.doors->colliders(camera.position() - glm::vec3(40), camera.position() + glm::vec3(40)))
        samples.push_back({door.door, scene.doors->angle(door.door), door.center});
    for (const auto position : doors_.update(seconds, samples)) system_.play(viewer::AudioEffect::doorSlam, position, .8f);
    system_.update();
}
} // namespace multiplayer
