// Headless deterministic input sequence for comparing contact implementations.
// An older melee_combat.cpp can be linked here to measure the same scene.
#include "melee_combat.hpp"
#include "player_body.hpp"
#include "physics_world.hpp"
#include "zombie_layer.hpp"
#include <box3d/box3d.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <stdexcept>

namespace {
using Clock = std::chrono::steady_clock;
double milliseconds(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
}
int main(int argc, char** argv) {
    try {
        // Keep physics diagnostics visible without mixing them into CSV rows.
        b3SetLogFcn([](const char* message) { std::cerr << "Box3D: " << message << '\n'; });
        const std::filesystem::path path = argc > 1 ? argv[1] : "assets/zombies/models/ZombieMale_A.glb";
        const int population = argc > 2 ? std::stoi(argv[2]) : 1;
        const int frames = argc > 3 ? std::stoi(argv[3]) : 240;
        if (population < 1 || population > 8 || frames < 60)
            throw std::runtime_error("Usage: benchmark_melee_collision [model.glb] [population 1..8] [frames >=60]");
        viewer::ZombieLayer zombies;
        zombies.assets.push_back(viewer::loadIdleModel(path));
        const auto& asset = zombies.assets[0];
        const auto lo = asset.model.boundsMin, hi = asset.model.boundsMax;
        const auto normalization = glm::scale(glm::mat4(1), glm::vec3(1.8f / (hi.y - lo.y))) *
            glm::translate(glm::mat4(1), -glm::vec3((lo.x + hi.x) * .5f, lo.y, (lo.z + hi.z) * .5f));
        const std::array<glm::vec3, 8> positions{{{0, 0, -.8f}, {-.85f, 0, -.8f}, {.85f, 0, -.8f},
            {-1.3f, 0, -1.8f}, {-.45f, 0, -1.8f}, {.45f, 0, -1.8f}, {1.3f, 0, -1.8f}, {0, 0, -2.65f}}};
        for (int i = 0; i < population; ++i) {
            viewer::ZombieInstance instance;
            instance.restRoot = glm::translate(glm::mat4(1), positions[i]);
            instance.transform = instance.restTransform = instance.restRoot * normalization;
            instance.displayedAnimationFrame = 0;
            zombies.instances.push_back(std::move(instance));
        }
        auto world = std::make_shared<viewer::PhysicsWorld>();
        zombies.attachPhysics(world); zombies.rebuildSpatialIndex();
        viewer::PlayerBody player(world, 1.7f);
        viewer::MeleeCombat melee(world);
        player.follow({0, 1.7f, 0}, {0, 0, -1}, true);
        std::cout << "model,population,phase,mean_ms,p95_ms,prepare_ms,bodies,shapes,cached_bone_meshes\n"
                  << std::fixed << std::setprecision(6);
        const auto report = [&](const char* phase, std::vector<double> samples, double prepare) {
            const double mean = std::accumulate(samples.begin(), samples.end(), 0.) / samples.size();
            std::sort(samples.begin(), samples.end());
            const auto counters = b3World_GetCounters(world->id());
            std::cout << path.stem().string() << ',' << population << ',' << phase << ',' << mean << ','
                      << samples[static_cast<std::size_t>((samples.size() - 1) * .95)] << ','
                      << prepare / samples.size() << ',' << counters.bodyCount << ',' << counters.shapeCount << ',';
#ifdef MELEE_BENCH_RIGID_STATS
            std::cout << melee.contactStats().cachedBoneMeshes;
#else
            std::cout << 0;
#endif
            std::cout << '\n' << std::flush;
        };
        const auto cold = Clock::now();
        melee.prepare(player, &zombies, 0);
        const auto coldTime = milliseconds(cold);
        report("construction", {coldTime}, coldTime);
        int tick = 0;
        const auto step = [&](bool living, bool agitate, double& prepare) {
            if (living && asset.frameCount)
                for (int i = 0; i < population; ++i)
                    zombies.instances[i].displayedAnimationFrame =
                        std::fmod(float(tick) * .5f + i * .37f, float(asset.frameCount));
            if (agitate && tick % 60 == 0) {
                player.punch({0, 0, -1});
                for (int i = 0; i < population; ++i)
                    zombies.applyRagdollImpulse(i, viewer::RagdollPart::torso, {0, 0, tick % 120 ? 6.f : -6.f});
            }
            const auto start = Clock::now();
            world->step(1.f / 60, [&](float dt) {
                player.beforeStep(dt);
                const auto before = Clock::now();
                melee.prepare(player, &zombies, tick / 60.);
                prepare += milliseconds(before);
            }, [&](float) {
                melee.finish(player, &zombies, tick / 60.); player.afterStep();
            });
            zombies.synchronizePhysics();
            ++tick;
            return milliseconds(start);
        };
        const auto measure = [&](const char* label, bool living, bool agitate) {
            std::vector<double> samples;
            samples.reserve(frames);
            double prepare = 0;
            for (int f = 0; f < frames; ++f) samples.push_back(step(living, agitate, prepare));
            report(label, std::move(samples), prepare);
        };
        double ignored = 0;
        for (int f = 0; f < 60; ++f) step(true, false, ignored);
        measure("living", true, false);
        for (int i = 0; i < population; ++i)
            zombies.meleeHit(i, {0, 0, -8}, positions[i] + glm::vec3(0, 1.4f, 0), tick / 60.);
        // Exclude the one-time living-to-ragdoll handoff construction.
        step(false, true, ignored);
        measure("moving_ragdoll", false, true);
        for (int f = 0; f < 900; ++f) step(false, false, ignored);
        measure("settled_ragdoll", false, false);
        player.follow({0, 1.7f, 0}, {0, 0, -1}, false);
        melee.prepare(player, &zombies, tick / 60.);
        measure("fists_disabled", false, false);
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n'; return 1;
    }
}
