#include "grenades.hpp"
#include "physics_world.hpp"
#include "physics_scene.hpp"
#include "door_layer.hpp"
#include "collision_world.hpp"
#include "interior_fixture.hpp"

#include <box3d/box3d.h>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
constexpr int waveSteps = 190, warmupWaves = 2, measuredWaves = 12;
constexpr float dt = 1.0f / 60;
double microseconds(Clock::duration duration) {
    return std::chrono::duration<double, std::micro>(duration).count();
}
struct Timings {
    std::vector<double> totals;
    double prepare = 0, physics = 0, update = 0;
    void print(const char* scenario, int grenades, int trees, int bodies) {
        std::sort(totals.begin(), totals.end());
        const auto count = static_cast<double>(totals.size());
        std::cout << scenario << ',' << grenades << ',' << trees << ',' << bodies << ',' << totals.size()
                  << ',' << prepare / count << ',' << physics / count << ',' << update / count
                  << ',' << (prepare + physics + update) / count << ',' << totals[totals.size() / 2]
                  << ',' << totals[totals.size() * 95 / 100] << '\n';
    }
};
void scenario(bool forest, int batch) {
    viewer::Model model;
    model.materials.push_back({});
    interior_fixture::quad(model, {-512, 0, -512}, {-512, 0, 512}, {512, 0, 512}, {512, 0, -512}, "terrain");
    interior_fixture::bounds(model);
    std::vector<viewer::TrunkCollider> trees;
    if (forest) for (int x = -100; x <= 100; ++x) for (int z = -100; z <= 100; ++z)
        trees.push_back({{x * 4.0f, 0, z * 4.0f}, .25f, 12});
    auto physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::DoorLayer doors(viewer::prepareDoorPanels(model), physics);
    viewer::CollisionWorld world(model, trees);
    viewer::PhysicsScene scene(physics, world, model.boundsMin, model.boundsMax);
    viewer::Grenades grenades(physics);
    // Exclude initial terrain upload and tree-index construction. The ground is
    // identical in both implementations; new trunk-body uploads remain timed.
    scene.ensureNear(model.boundsMin, model.boundsMax);
    int tick = 0;
    const auto step = [&](Timings* timings) {
        const auto a = Clock::now();
        grenades.prepare(scene, doors);
        const auto b = Clock::now();
        physics->step(dt);
        const auto c = Clock::now();
        grenades.update(++tick / 60.0, world);
        const auto d = Clock::now();
        if (timings) {
            timings->prepare += microseconds(b - a);
            timings->physics += microseconds(c - b);
            timings->update += microseconds(d - c);
            timings->totals.push_back(microseconds(d - a));
        }
    };
    Timings active;
    for (int wave = 0; wave < warmupWaves + measuredWaves; ++wave) {
        // Separate lanes and new forest regions each wave exercise retained
        // colliders as well as active contacts, without grenade/grenade hits.
        for (int lane = 0; lane < batch; ++lane) {
            const glm::vec3 eyes(-64 + lane * 8.0f, 1.3f, -96 + wave * 16.0f + 1.5f);
            if (!grenades.throwFrom(eyes, {0, 0, -1}, {0, 0, 0}, tick / 60.0, {3, 8, 14}, world))
                throw std::runtime_error("Benchmark throw failed");
        }
        for (int frame = 0; frame < waveSteps; ++frame) step(wave >= warmupWaves ? &active : nullptr);
        if (!grenades.poses().empty()) throw std::runtime_error("Benchmark fuse failed");
    }
    const int bodies = b3World_GetCounters(physics->id()).bodyCount;
    const int treeCount = static_cast<int>(trees.size());
    active.print(forest ? "forest_active" : "open_active", batch, treeCount, bodies);
    Timings idle;
    for (int frame = 0; frame < 2400; ++frame) step(&idle);
    idle.print(forest ? "forest_idle_after_throws" : "open_idle_after_throws", batch, treeCount, bodies);

    // Repeated identical approaches isolate contact handling from differences
    // in subsequent trajectories, floor rolling and sleep between versions.
    for (int lane = 0; lane < batch; ++lane)
        if (!grenades.throwFrom({-64 + lane * 8.0f, 1.3f, .45f}, {0, 0, -1}, {0, 0, 0},
                                tick / 60.0, {100000, 8, 14}, world))
            throw std::runtime_error("Impact benchmark throw failed");
    struct Reset { b3BodyId body; b3Pos position; };
    std::vector<Reset> resets;
    auto filter = b3DefaultQueryFilter();
    filter.maskBits = viewer::grenadeBodyCategory;
    b3World_OverlapAABB(physics->id(), {{-100, 0, -2}, {100, 4, 2}}, filter,
        [](b3ShapeId shape, void* context) {
            const auto body = b3Shape_GetBody(shape);
            static_cast<std::vector<Reset>*>(context)->push_back({body, b3Body_GetPosition(body)});
            return true;
        }, &resets);
    if (static_cast<int>(resets.size()) != batch) throw std::runtime_error("Missing impact benchmark body");
    grenades.prepare(scene, doors);
    Timings impacts;
    for (int trial = 0; trial < 1300; ++trial) {
        for (const auto& reset : resets) {
            b3Body_SetTransform(reset.body, reset.position, b3Quat_identity);
            b3Body_SetLinearVelocity(reset.body, {0, 2, -14});
            b3Body_SetAngularVelocity(reset.body, {8, 2, 3});
            b3Body_SetAwake(reset.body, true);
        }
        step(trial >= 100 ? &impacts : nullptr);
        step(trial >= 100 ? &impacts : nullptr);
    }
    impacts.print(forest ? "forest_repeated_impacts" : "open_repeated_passes", batch, treeCount,
                  b3World_GetCounters(physics->id()).bodyCount - batch);
}
} // namespace
int main() {
    try {
        std::cout << std::fixed << std::setprecision(3)
                  << "scenario,batch,trees,retained_bodies,ticks,prepare_us,physics_us,update_us,total_us,median_us,p95_us\n";
        scenario(false, 1); scenario(true, 1); scenario(false, 16); scenario(true, 16);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
