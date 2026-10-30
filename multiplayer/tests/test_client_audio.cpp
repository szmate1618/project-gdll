#include "client/audio_playback.hpp"
#include "client/sound_cues.hpp"
#include "client/weapon_feedback.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
}
int main(int argc, char** argv) {
    try {
        require(argc == 3, "Expected test map and audio directory");
        multiplayer::Options options; options.map = argv[1]; options.zombieCount = 0;
        options.trees = options.forests = options.cars = false;
        multiplayer::Scene scene(options);
        viewer::Camera camera; camera.setPosition(scene.spawnEye);
        viewer::AudioOptions settings; settings.directory = argv[2]; settings.hrtf = false;
        multiplayer::ClientAudio audio(settings);
        require(audio.enabled(), "Null audio device failed to initialize");
        require(audio.stats().playingStreams == 1 && audio.stats().streamBuffers == 4, "Local wind ambience did not start");
        multiplayer::Snapshot initial; initial.tick = 100;
        auto& self = initial.players.emplace_back(); self.id = 1; self.loaded = 2; self.reserve = 120;
        self.movement.feet = scene.spawnEye - glm::vec3(0, 1.7f, 0); self.movement.grounded = true;
        multiplayer::RemoteSoundCues remoteEvents;
        std::vector<multiplayer::SoundEvent> remoteShots;
        const auto receive = [&](const multiplayer::Snapshot& snapshot) {
            audio.queueRemote(remoteEvents.receive(snapshot, 1, &remoteShots));
        };
        receive(initial);
        multiplayer::WeaponFeedbackPrediction feedback;
        feedback.reconcile(self);
        const auto weaponCommand = [&](const multiplayer::Command& command, double now) {
            const auto result = feedback.advance(command, now, scene.initialWeapons, scene.initialInventory);
            audio.playWeapon(result.sounds);
            return result;
        };
        multiplayer::Command shot; shot.sequence = 1; shot.left = true;
        require(weaponCommand(shot, 0).firearmSlot == 0,
                "Local shot sound and visual feedback used different firing decisions");
        require(audio.stats().effects == 1, "Own shot did not reach playback before server acknowledgement");
        multiplayer::Snapshot next = initial; next.tick = 103;
        next.players[0].resolved = 1; next.players[0].loaded = 1; next.players[0].shots = 1;
        feedback.reconcile(next.players[0]);
        multiplayer::Snapshots snapshots; snapshots.push(next, 0); snapshots.apply(scene, 0);
        receive(next); audio.update(camera, scene, snapshots, 1, .016f, .016);
        require(audio.stats().effects == 1, "Shot confirmation duplicated local playback");
        next.tick = 106;
        next.sounds.push_back({1, 105, multiplayer::SoundKind::shot, 2, 2, camera.position()});
        receive(next); audio.update(camera, scene, snapshots, 1, .016f, .032);
        require(audio.stats().effects == 2, "Remote event did not reach positional playback");
        receive(next); audio.update(camera, scene, snapshots, 1, .016f, .048);
        require(audio.stats().effects == 2, "Repeated remote event duplicated playback");
        next.tick = 107;
        next.sounds.push_back({2, 107, multiplayer::SoundKind::shot, 2, 1,
                               camera.position() + glm::vec3(multiplayer::soundRange + 1, 0, 0)});
        receive(next);
        require(remoteShots.size() == 1 && remoteShots[0].id == 2,
                "An inaudible remote shot lost its visual event");
        audio.update(camera, scene, snapshots, 1, .016f, .056);
        require(audio.stats().effects == 2, "A distant gunshot consumed an audio effect voice");
        multiplayer::Command reload; reload.sequence = 2; reload.reload = true;
        weaponCommand(reload, 1);
        require(audio.stats().effects == 3, "Own reload did not play before server confirmation");
        next.tick = 109; next.explosions.push_back({1, camera.position(), .1f});
        receive(next); audio.update(camera, scene, snapshots, 1, .016f, .064);
        require(audio.stats().effects == 4, "Replicated explosion did not reach playback");

        auto remote = next.players.front(); remote.id = 2;
        remote.movement.feet += glm::vec3(2, 0, 0);
        next.players.push_back(remote); next.sounds.clear(); next.explosions.clear();
        for (int frame = 1; frame <= 60; ++frame) {
            next.tick = 110 + frame;
            next.players[1].movement.feet.x += .05f;
            const double now = .064 + frame / 60.;
            snapshots.push(next, now); snapshots.apply(scene, now);
            audio.update(camera, scene, snapshots, 1, 1.f/60, now);
        }
        require(audio.stats().effects > 4, "Remote movement did not synthesize local footsteps without sound events");
        scene.zombies.assets.emplace_back().skeleton.resize(1);
        scene.zombies.instances.emplace_back().removed = true;
        multiplayer::ZombieSetup setup; setup.version = 1;
        snapshots.pushSetups(scene, multiplayer::ZombieSetups{{setup}});
        next.tick = 200;
        auto& zombie = next.zombies.emplace_back();
        zombie.root[3] = glm::vec4(camera.position() + glm::vec3(3, -1.7f, 0), 1);
        snapshots.push(next, 2); snapshots.apply(scene, 2);
        audio.update(camera, scene, snapshots, 1, .016f, 2);
        require(audio.stats().zombieEmitters == std::vector<std::size_t>{0}, "Local sampler did not select an audible tracked zombie");
        audio.update(camera, scene, snapshots, 1, .016f, 12);
        require(audio.stats().zombies == std::vector<std::size_t>{0} && next.sounds.empty(),
                "Ordinary moan needed a server sound event or failed to reach playback");
        require(scene.zombies.behaviors.entries().empty() && scene.zombies.instances[0].heardNoiseRevision == 0,
                "Local moan playback changed gameplay perception");
        scene.zombies.instances[0].removed = true;
        audio.update(camera, scene, snapshots, 1, .016f, 12.1);
        require(audio.stats().zombieEmitters.empty(), "A hidden/dead ambient candidate retained a voice");
        settings.enabled = false;
        multiplayer::ClientAudio disabled(settings);
        multiplayer::WeaponFeedbackPrediction mutedFeedback;
        mutedFeedback.reconcile(initial.players[0]);
        const auto mutedShot = mutedFeedback.advance(shot, 0, scene.initialWeapons, scene.initialInventory);
        multiplayer::RemoteSoundCues mutedEvents;
        mutedEvents.receive(initial, 1);
        auto mutedSnapshot = next; mutedSnapshot.tick = 201;
        mutedSnapshot.sounds.push_back({3, 201, multiplayer::SoundKind::shot, 2, 3, camera.position()});
        std::vector<multiplayer::SoundEvent> mutedShots;
        disabled.queueRemote(mutedEvents.receive(mutedSnapshot, 1, &mutedShots));
        disabled.playWeapon(mutedShot.sounds);
        require(mutedShot.firearmSlot == 0, "--no-audio disabled local shot visuals");
        require(mutedShots.size() == 1 && mutedShots[0].source == 2,
                "--no-audio disabled remote shot visuals");
        disabled.update(camera, scene, snapshots, 1, .016f, 2);
        require(!disabled.enabled() && disabled.stats().effects == 0, "--no-audio still played effects");
        std::cout << "Client playback: immediate local actions, remote events, footsteps, ambience and mute passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
