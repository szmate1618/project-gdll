#include "fps_controller.hpp"
#include "collision_world.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void propagation() {
    const viewer::NoiseEvent walk{{0, 0, 0}, viewer::walkingFootstepDecibels};
    require(viewer::noiseLevelAt(walk, {10, 0, 0}) == viewer::zombieHearingThresholdDecibels,
            "Walking at ten meters meets the hearing threshold");
    require(viewer::noiseLevelAt(walk, {0, 10, 0}) == viewer::noiseLevelAt(walk, {10, 0, 0}),
            "Sound distance is three dimensional");
    require(std::abs(viewer::noiseLevelAt(walk, {100, 0, 0})) < 1e-4f,
            "Tenfold distance loses twenty decibels");
    require(viewer::noiseLevelAt(walk, {0, 0, 0}) == walk.decibels &&
            viewer::noiseLevelAt(walk, {0.1f, 0, 0}) == walk.decibels, "Near-source levels remain finite");
    require(viewer::noiseLevelAt({walk.position, viewer::runningFootstepDecibels}, {30, 0, 0}) >=
                viewer::zombieHearingThresholdDecibels, "Running is audible farther than walking");
    require(viewer::noiseLevelAt({walk.position, viewer::landingDecibels}, {50, 0, 0}) >=
                viewer::zombieHearingThresholdDecibels, "Landing is louder than running");
    const float nan = std::numeric_limits<float>::quiet_NaN();
    require(viewer::noiseLevelAt(walk, {nan, 0, 0}) == -std::numeric_limits<float>::infinity(),
            "Invalid listener cannot hear noise");
    require(viewer::noiseLevelAt({{0, 0, 0}, nan}, {0, 0, 0}) == -std::numeric_limits<float>::infinity(),
            "Invalid level cannot produce noise");
}

void cadenceAndMovement() {
    for (const bool running : {false, true}) {
        viewer::PlayerNoiseEmitter emitter;
        glm::vec3 feet(0);
        std::vector<viewer::NoiseEvent> events;
        for (int frame = 0; frame < 120; ++frame) {
            const auto next = feet + glm::vec3(running ? 0.1f : 0.05f, 0, 0);
            const auto emitted = emitter.update(1.0f / 60, {feet, true}, {next, true}, running);
            events.insert(events.end(), emitted.begin(), emitted.end());
            feet = next;
        }
        require(events.size() == (running ? 6 : 3), "Two seconds emit six running or three walking footsteps");
        for (std::size_t step = 0; step < events.size(); ++step) {
            require(std::abs(events[step].position.x - 2 * static_cast<float>(step + 1)) < 2e-4f,
                    "Footsteps occur along actual movement, not repeatedly at the frame endpoint");
            require(events[step].decibels == (running ? viewer::runningFootstepDecibels : viewer::walkingFootstepDecibels),
                    "Footstep loudness follows walk/run mode");
            require(events[step].kind == viewer::NoiseEvent::Kind::footstep, "Footstep action is explicit for playback routing");
        }
        viewer::PlayerNoiseEmitter single;
        require(single.update(2, {{0, 0, 0}, true}, {feet, true}, running).size() == events.size(),
                "Cadence retains every event across different step sizes");
    }
    viewer::PlayerNoiseEmitter emitter;
    require(emitter.update(1, {{0, 0, 0}, true}, {{0, 0, 0}, true}, true).empty(), "Standing makes no footsteps");
    require(emitter.update(1, {{0, 0, 0}, false}, {{6, -2, 0}, false}, true).empty(), "Airborne motion makes no footsteps");
    require(emitter.update(1, {{0, 0, 0}, true}, {{0, 1, 0}, true}, false).empty(), "Vertical motion is not a footstep");
    emitter.update(0.2f, {{0, 0, 0}, true}, {{1, 0, 0}, true}, false);
    emitter.update(1, {{1, 0, 0}, true}, {{1, 0, 0}, true}, false);
    require(emitter.update(0.2f, {{1, 0, 0}, true}, {{2, 0, 0}, true}, false).empty(),
            "Stopping clears partial cadence; restarting does not replay stale footsteps");
    emitter.reset();
    emitter.update(1.0f / 3, {{0, 0, 0}, true}, {{1, 0, 0}, true}, false);
    require(emitter.update(1.0f / 6, {{1, 0, 0}, true}, {{2, 0, 0}, true}, true).size() == 1,
            "Changing speed retains fractional cadence and applies the new rate");
    emitter.reset();
    const auto landing = emitter.update(0.1f, {{0, 1, 0}, false}, {{0, 0, 0}, true}, true);
    require(landing.size() == 1 && landing[0].decibels == viewer::landingDecibels && landing[0].position == glm::vec3(0),
            "Landing emits exactly one louder noise at the landing feet");
    require(landing[0].kind == viewer::NoiseEvent::Kind::landing, "Landing action is explicit for playback routing");
    require(emitter.update(1, {{0, 0, 0}, true}, {{0, 0, 0}, true}, true).empty(), "A landing event is not repeated");
}

viewer::Model ground() {
    viewer::Model model;
    viewer::Primitive p;
    for (auto point : {glm::vec3(-100, 0, -100), glm::vec3(-100, 0, 100),
                       glm::vec3(100, 0, 100), glm::vec3(100, 0, -100)}) {
        viewer::Vertex vertex;
        vertex.position = point;
        p.vertices.push_back(vertex);
    }
    p.indices = {0, 1, 2, 0, 2, 3};
    model.primitives.push_back(std::move(p));
    model.draws.push_back({0, glm::mat4(1), "terrain"});
    return model;
}

void physicsIntegration() {
    const viewer::CollisionWorld world(ground());
    for (const bool running : {false, true}) {
        // Exercise render updates both faster and slower than fixed physics.
        for (const float dt : {1.0f / 30, 1.0f / 240}) {
            viewer::FPSController player;
            require(player.spawn({0, 5, 0}, world), "Player must spawn on ground");
            std::size_t steps = 0;
            const int frames = static_cast<int>(std::round(2 / dt));
            for (int frame = 0; frame < frames; ++frame) {
                player.update(dt, {1, 0, 0, running, false}, world);
                for (const auto& event : player.noises()) {
                    require(event.decibels != viewer::landingDecibels, "Spawning is not a landing noise");
                    ++steps;
                }
            }
            require(steps == (running ? 6 : 3), "Fixed-step physics preserves cadence at low and high frame rates");
            player.update(0, {}, world);
            require(player.noises().empty(), "No stale noises after an update without simulation time");
        }
    }
    for (const bool jump : {false, true}) {
        viewer::FPSController player;
        require(player.spawn({0, 5, 0}, world), "Player must spawn before jump/fall");
        if (!jump) player.teleport({0, 4, 0});
        std::size_t landings = 0;
        for (int frame = 0; frame < 240; ++frame) {
            player.update(1.0f / 120, {0, 0, 0, false, jump && frame == 0}, world);
            for (const auto& event : player.noises()) {
                require(event.decibels == viewer::landingDecibels, "Stationary jump/fall only emits landing noise");
                require(std::abs(event.position.y) < 0.01f, "Landing noise uses the actual supporting ground");
                ++landings;
            }
        }
        require(landings == 1 && player.grounded(), "Jump and fall each produce one landing noise");
    }

    auto model = ground();
    viewer::Primitive wall;
    for (auto point : {glm::vec3(0.5f, 0, -20), glm::vec3(0.5f, 4, -20),
                       glm::vec3(0.5f, 4, 20), glm::vec3(0.5f, 0, 20)}) {
        viewer::Vertex vertex;
        vertex.position = point;
        wall.vertices.push_back(vertex);
    }
    wall.indices = {0, 1, 2, 0, 2, 3};
    model.draws.push_back({model.primitives.size(), glm::mat4(1), "wall"});
    model.primitives.push_back(std::move(wall));
    const viewer::CollisionWorld blocked(model);
    viewer::FPSController player;
    require(player.spawn({0, 5, 0}, blocked), "Player must spawn next to wall");
    for (int frame = 0; frame < 240; ++frame) {
        player.update(1.0f / 120, {1, 0, 0, true, false}, blocked);
        require(player.noises().empty(), "Holding run against a blocking wall does not emit footsteps");
    }
}
}

int main() {
    try {
        propagation();
        cadenceAndMovement();
        physicsIntegration();
        std::cout << "Player noise tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Player noise test failed: " << error.what() << '\n';
        return 1;
    }
}
