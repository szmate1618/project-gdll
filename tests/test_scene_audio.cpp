#include "scene_audio.hpp"
#include "camera_rig.hpp"
#include "noise.hpp"
#include "zombie_layer.hpp"
#include "simulation_clock.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
viewer::Model ground() {
    viewer::Model model;
    viewer::Primitive p;
    for (const auto point : {glm::vec3(-100, 0, -100), glm::vec3(-100, 0, 100),
                             glm::vec3(100, 0, 100), glm::vec3(100, 0, -100)}) {
        viewer::Vertex vertex; vertex.position = point; p.vertices.push_back(vertex);
    }
    p.indices = {0, 1, 2, 0, 2, 3};
    model.primitives.push_back(std::move(p));
    model.draws.push_back({0, glm::mat4(1), "terrain"});
    model.boundsMin = {-100, 0, -100}; model.boundsMax = {100, 0, 100};
    return model;
}
viewer::ZombieLayer crowd() {
    viewer::ZombieLayer zombies;
    zombies.assets.emplace_back();
    for (const auto feet : {glm::vec3(3, 0, 0), glm::vec3(5, 0, 0), glm::vec3(200, 0, 0)}) {
        viewer::ZombieInstance zombie;
        zombie.restRoot = glm::translate(glm::mat4(1), feet);
        zombie.transform = zombie.restTransform = zombie.restRoot;
        zombies.instances.push_back(zombie);
    }
    zombies.rebuildSpatialIndex();
    return zombies;
}
}
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected audio assets directory");
        const auto model = ground();
        const viewer::CollisionWorld world(model);
        for (const bool enabled : {true, false}) {
            viewer::SimulationClock::instance().reset();
            viewer::AudioOptions options;
            options.directory = argv[1]; options.enabled = enabled;
            options.zombieVoices = 1;
            viewer::SceneAudio audio(options);
            viewer::CameraRig rig(world, model.boundsMin, model.boundsMax, 1.5f);
            rig.camera().setPosition({0, 1.7f, 0});
            auto zombies = crowd();
            zombies.behaviors.create(0, 0);
            std::vector<std::size_t> ids;
            zombies.queryLivingNear({}, 4, ids);
            require(ids == std::vector<std::size_t>{0}, "Living query applies exact range after grid lookup");
            audio.update(rig, &zombies, 0.01f);
            audio.shot(0);
            audio.reload();
            require(zombies.behaviors.find(0)->state == viewer::ZombieState::idle,
                    "Audio playback alone never produces an AI hearing event");
            if (enabled) require(audio.stats().zombieEmitters == std::vector<std::size_t>{0},
                                 "Nearest zombie reserves a voice independently of activated behavior or rendering");
            zombies.makeNoise({0, 0, 0}, viewer::walkingFootstepDecibels);
            require(zombies.behaviors.find(0)->state == viewer::ZombieState::looking,
                    "Simulation hearing works identically with playback enabled or muted");
            audio.update(rig, &zombies, .01f);
            if (enabled) require(audio.stats().zombies == std::vector<std::size_t>{0},
                                 "An accepted hearing event plays a positional reaction");
            zombies.instances[0].ragdoll = true;
            // Do not wait for the 5 Hz grid refresh: death must stop the voice now.
            audio.update(rig, &zombies, 0.01f);
            if (enabled) require(audio.stats().zombieEmitters == std::vector<std::size_t>{1} && audio.stats().zombies.empty(),
                                 "Dead voice stops immediately and the next nearby zombie takes the slot");
            zombies.instances[1].removed = true;
            audio.update(rig, &zombies, 0.01f);
            require(audio.stats().zombies.empty(), "Removed characters cannot retain a voice");
            zombies.queryLivingNear({}, 42, ids);
            require(ids.empty(), "Living query filters corpses and removals");
            rig.camera().setPosition({200, 1.7f, 0});
            audio.update(rig, &zombies, 0.21f);
            if (enabled) require(audio.stats().zombieEmitters == std::vector<std::size_t>{2},
                                 "Grid refresh discovers a new neighborhood after moving the camera");
        }
        // Route real fixed-step movement actions independently of zombie presence.
        viewer::AudioOptions options; options.directory = argv[1];
        viewer::SceneAudio audio(options);
        viewer::CameraRig rig(world, model.boundsMin, model.boundsMax, 1.5f);
        require(rig.enterWalking(glm::vec3(0, 5, 0)), "Walking fixture spawns");
        std::size_t steps = 0;
        for (int i = 0; i < 90; ++i) {
            rig.update(1.f / 60, 1, 0, 0, false, false);
            for (const auto& event : rig.player().noises()) {
                require(event.kind == viewer::NoiseEvent::Kind::footstep, "Movement marks footsteps independently of dB tuning");
                ++steps;
            }
            audio.update(rig, nullptr, 1.f / 60);
        }
        require(steps == 2 && audio.stats().effects > 0, "Walking produces audible steps without a zombie layer");
        std::cout << "Scene audio tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
