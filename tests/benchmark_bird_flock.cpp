#include "bird_flock.hpp"
#include "collision_world.hpp"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <stdexcept>

int main() {
    // A fixed simulated minute in a small synthetic open world. Construction
    // and its placement queries are outside the measured steady-state loop.
    constexpr int steps = 3600;
    constexpr float dt = 1.f / 60.f;
    const viewer::CollisionWorld world(viewer::Model{});
    viewer::BirdFlockPlayer player;
    player.feet = {0, 0, 0};
    std::cout << "scenario,birds,flocks,steps,mean_tick_us\n";
    for (const std::size_t birdsPerFlock : {192u, 2048u}) {
        for (const bool targetable : {false, true}) {
            viewer::BirdFlockSettings settings;
            settings.flockCount = 6;
            settings.birdsPerFlock = birdsPerFlock;
            viewer::BirdFlocks birds(world, nullptr, {-500, 0, -500}, {500, 100, 500},
                                    {0, 25, 0}, settings);
            if (birds.birds().size() != settings.flockCount * settings.birdsPerFlock)
                throw std::runtime_error("Benchmark fixture failed to construct the full bird population");
            player.targetable = targetable;
            for (int tick = 0; tick < 120; ++tick) birds.update(dt, player);
            const auto start = std::chrono::steady_clock::now();
            for (int tick = 0; tick < steps; ++tick) {
                birds.update(dt, player);
                if (birds.stats().sightChecks > 1)
                    throw std::runtime_error("The global one-bird sight budget was exceeded");
            }
            const auto elapsed = std::chrono::duration<double, std::micro>(
                std::chrono::steady_clock::now() - start).count();
            std::cout << (targetable ? "active" : "idle") << ',' << birds.birds().size()
                      << ',' << birds.flocks().size() << ',' << steps
                      << ',' << std::fixed << std::setprecision(2) << elapsed / steps << '\n';
        }
    }
}
