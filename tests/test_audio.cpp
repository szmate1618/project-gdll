#include "audio.hpp"
#include "camera.hpp"
#include "audio_fixture.hpp"

#include <AL/al.h>
#include <AL/alc.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
std::vector<std::size_t> ids(const viewer::AudioSystem& audio) {
    auto result = audio.stats().zombieEmitters;
    std::sort(result.begin(), result.end());
    return result;
}
}

int main(int argc, char** argv) {
    try {
        require(argc >= 2, "Expected scratch directory");
        viewer::AudioOptions options;
        options.directory = argv[1];
        options.effectVoices = 2;
        options.zombieVoices = 2;
        options.rain = true;
        options.hrtf = false;
        if (argc == 3) {
            // A separate process with an unavailable driver exercises the real
            // device-failure path, rather than only the explicit mute switch.
            viewer::AudioSystem audio(options);
            require(!audio.enabled(), "Unavailable device must fall back to silent playback");
            audio.setListener({}, {0, 0, -1});
            audio.playLocal(viewer::AudioEffect::pistol);
            audio.setZombieEmitters({{1, {0, 0, 1}}});
            audio.update();
            require(audio.stats().effects == 0, "Disabled backend has no sources");
            return 0;
        }
        std::filesystem::create_directories(options.directory);
        const std::vector<std::int16_t> samples(48000, 300); // One-second mono fixtures.
        for (const auto* name : {"footstep", "landing", "pistol", "smg", "sniper", "shotgun", "reload", "zombie",
                                "footstep2", "footstep3", "footstep4", "zombie2", "zombie3", "huh", "intrigued",
                                "chase", "chase2", "door_latch", "door_slam", "empty_click", "reload_rifle", "reload_shotgun"})
            writeAudioFixture(options.directory / (std::string(name) + ".wav"), 1, samples);
        writeAudioFixture(options.directory / "wind.wav", 2, std::vector<std::int16_t>(96000, 100));
        writeAudioFixture(options.directory / "rain.wav", 2, {100, -100, 200, -200});
        {
            viewer::AudioSystem audio(options);
            require(audio.enabled(), "OpenAL null backend must initialize without hardware");
            require(!audio.hrtfEnabled(), "Ordinary stereo option disables HRTF");
            viewer::Camera camera;
            camera.setPosition({12, 3, -8});
            camera.look(430, 150);
            audio.setListener(camera.position(), camera.direction());
            glm::vec3 eye;
            alGetListener3f(AL_POSITION, &eye.x, &eye.y, &eye.z);
            require(eye == camera.position(), "Listener uses the camera eye in world meters, including Z sign");
            ALfloat orientation[6]{};
            alGetListenerfv(AL_ORIENTATION, orientation);
            const glm::vec3 forward(orientation[0], orientation[1], orientation[2]);
            const glm::vec3 up(orientation[3], orientation[4], orientation[5]);
            require(glm::length(forward - camera.direction()) < 1e-5f, "Listener follows camera yaw and pitch");
            require(std::abs(glm::dot(forward, up)) < 1e-5f && std::abs(glm::length(up) - 1) < 1e-5f,
                    "Listener orientation remains orthonormal while looking up/down");
            const auto view = camera.view();
            const glm::vec3 cameraUp(view[0][1], view[1][1], view[2][1]);
            require(glm::length(cameraUp - up) < 1e-5f, "Audio up matches the rendered camera basis");
            audio.setListener({}, {0, 0, -1});
            std::vector<viewer::AudioEmitter> candidates{{1, {0, 0, 30}}, {2, {0, 0, 20}},
                                                       {3, {-5, 0, 0}}, {4, {2, 0, 0}}};
            audio.setZombieEmitters(candidates);
            require(ids(audio) == std::vector<std::size_t>({3, 4}), "Only the nearest two zombies receive voices");
            require(audio.stats().zombies.empty(), "Reserving a voice never starts a continuous loop");
            require(audio.playZombie(3, viewer::AudioEffect::zombie), "An ordinary moan plays as a one-shot");
            require(!audio.playZombie(3, viewer::AudioEffect::zombie2), "Ordinary moans do not interrupt each other");
            require(audio.playZombie(3, viewer::AudioEffect::huh, 2), "A hearing reaction interrupts an ordinary moan");
            require(!audio.playZombie(3, viewer::AudioEffect::zombie), "An ordinary moan cannot interrupt a reaction");
            require(audio.playZombie(3, viewer::AudioEffect::chase, 3), "A chase-start grunt takes priority over hearing");
            require(!audio.playZombie(1, viewer::AudioEffect::zombie), "A far unselected zombie cannot allocate another voice");
            candidates.push_back({99, {0, 0, 1}});
            candidates.push_back({99, {0, 0, 1}});
            audio.setZombieEmitters(candidates);
            require(ids(audio) == std::vector<std::size_t>({4, 99}), "A nearer zombie replaces the farthest; IDs never duplicate");
            require(audio.stats().zombies.empty(), "Replacing an emitter stops its old vocalization immediately");
            audio.setListener({0, 0, 29}, {0, 0, 1});
            audio.setZombieEmitters(candidates);
            require(ids(audio) == std::vector<std::size_t>({1, 2}), "Moving the camera reprioritizes voices");
            audio.setZombieEmitters({{1, {0, 100, 29}}, {2, {0, 0, 100}},
                                     {3, {std::numeric_limits<float>::quiet_NaN(), 0, 0}}});
            require(ids(audio).empty(), "Out-of-range and invalid voices stop; range includes height");
            std::filesystem::remove(options.directory / "footstep.wav");
            audio.play(viewer::AudioEffect::footstep, {0, 0, 29});
            require(audio.stats().effects == 1, "Preloaded effects play after their file is removed");
            for (int i = 0; i < 10; ++i) audio.playLocal(viewer::AudioEffect::pistol);
            require(audio.stats().effects == 2, "Repeated gunfire respects the fixed effect pool");
            const auto before = audio.stats();
            require(before.streamBuffers == 8 && before.streamFramesRead == 32768,
                    "Two ambience files use four 4096-frame buffers each, including very short loops");
            // Longer than the entire queue at 48 kHz: exercise actual OpenAL
            // consumption, complete underrun, requeue and restart without a GPU.
            std::this_thread::sleep_for(std::chrono::milliseconds(650));
            audio.update();
            const auto after = audio.stats();
            require(after.streamBuffers == 8 && after.playingStreams == 2 &&
                    after.streamFramesRead > before.streamFramesRead,
                    "Ambience remains bounded and resumes after a stall");
            require(alGetError() == AL_NO_ERROR, "Playback, voice stealing and streaming produce no AL errors");
        }
        require(alcGetCurrentContext() == nullptr, "Audio destruction restores the previous context");
        writeAudioFixture(options.directory / "footstep.wav", 1, samples);
        options.hrtf = true;
        {
            viewer::AudioSystem audio(options);
            require(audio.enabled() && audio.hrtfEnabled(), "Pinned OpenAL Soft supplies its built-in HRTF without external data");
        }
        writeAudioFixture(options.directory / "footstep.wav", 2, {100, -100});
        std::filesystem::remove(options.directory / "zombie.wav");
        std::filesystem::remove(options.directory / "wind.wav");
        {
            viewer::AudioSystem audio(options);
            require(audio.enabled(), "Missing/unsupported assets cannot disable other playback");
            audio.playLocal(viewer::AudioEffect::footstep);
            audio.setZombieEmitters({{1, {0, 0, 1}}});
            require(audio.stats().effects == 0 && audio.stats().zombies.empty(),
                    "Invalid stereo effect and absent zombie sample are skipped independently");
            audio.playLocal(viewer::AudioEffect::pistol);
            require(audio.stats().effects == 1 && audio.stats().playingStreams == 1,
                    "Valid effects and rain still play without wind or zombie assets");
            audio.update();
            require(alGetError() == AL_NO_ERROR, "Partial asset availability produces no AL errors");
        }
        options.enabled = false;
        viewer::AudioSystem muted(options);
        require(!muted.enabled() && alcGetCurrentContext() == nullptr, "Explicit mute creates no output device/context");
        std::cout << "OpenAL audio tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
