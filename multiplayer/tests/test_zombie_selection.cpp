#include "server/replication.hpp"
#include "server/zombie_selection.hpp"
#include "zombie_spatial_index.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <stdexcept>

namespace {
using namespace multiplayer;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

void cellOrder() {
    viewer::ZombieSpatialIndex index;
    index.reset(4);
    index.update(0, {80.1f, 0, .1f}); index.update(1, {20.1f, 0, .1f});
    index.update(2, {-.1f, 0, .1f}); index.update(3, {.1f, 0, .1f});
    std::vector<std::size_t> result;
    const auto all = [&](std::size_t id) { result.push_back(id); return true; };
    index.visitCellsNear({}, 100, all);
    require(result == std::vector<std::size_t>({2, 3, 1, 0}), "Cells are not ordered by proximity with deterministic ties");
    result.clear();
    index.visitCellsNear({}, 100, [&](std::size_t id) { result.push_back(id); return result.size() < 2; });
    require(result == std::vector<std::size_t>({2, 3}), "Cell traversal did not stop at its caller's budget");
    index.erase(2); index.update(0, {-.2f, 0, .1f}); result.clear();
    index.visitCellsNear({}, 100, all);
    require(result == std::vector<std::size_t>({0, 3, 1}), "Cell order retained removed or stale moved members");
}

void placements(Scene& scene, const std::vector<glm::vec3>& feet) {
    scene.zombies = viewer::ZombieLayer{};
    scene.zombies.assets.emplace_back();
    scene.zombies.instances.resize(feet.size());
    for (std::size_t id = 0; id < feet.size(); ++id) {
        auto& z = scene.zombies.instances[id];
        z.restRoot = z.restTransform = z.transform = glm::translate(glm::mat4(1), feet[id]);
    }
    scene.zombies.rebuildSpatialIndex();
}

void visibility(Scene& scene) {
    placements(scene, {{0, 0, -100}, {0, 0, 100}, {0, 0, 20}, {0, 0, -500}, {200, 0, -100}});
    ReplicationView view; view.eye = {0, 1.7f, 0};
    std::vector<std::size_t> selected;
    selectZombieInterest(scene.zombies, {}, view, {}, selected);
    const auto contains = [&](std::size_t id) { return std::find(selected.begin(), selected.end(), id) != selected.end(); };
    require(contains(0) && contains(2), "Visible zombie or nearby offscreen threat was omitted");
    require(!contains(1) && !contains(3) && !contains(4), "Far offscreen or distance-culled zombie was selected");
    selectZombieInterest(scene.zombies, {}, view, {}, selected, glm::vec3(0, 1, 0));
    require(contains(1), "Offscreen sun-shadow caster was omitted from the renderer's light volume");
    view.verticalFov = 10;
    selectZombieInterest(scene.zombies, {}, view, {}, selected);
    require(contains(3), "Scope interest did not expand with the rendering distance policy");
    scene.zombies.instances[0].ragdoll = true;
    scene.zombies.instances[0].transform = glm::translate(glm::mat4(1), glm::vec3(0, 0, 300));
    scene.zombies.markRenderChanged(0);
    selectZombieInterest(scene.zombies, {}, view, {}, selected);
    require(!contains(0), "Moving ragdoll left stale all-actor replication membership");
    scene.zombies.instances[0].transform = glm::translate(glm::mat4(1), glm::vec3(0, 0, 10));
    scene.zombies.markRenderChanged(0);
    selectZombieInterest(scene.zombies, {}, view, {}, selected);
    require(contains(0), "Ragdoll did not re-enter nearby interest after moving");
}

void boundsAcrossCells(Scene& scene) {
    placements(scene, {{45, 0, 45}});
    auto& model = scene.zombies.assets[0].model;
    model.boundsMin = {-30, 0, -30}; model.boundsMax = {30, 2, 30};
    scene.zombies.rebuildSpatialIndex();
    std::vector<std::size_t> candidates;
    scene.zombies.visitReplicationNear({}, 25, [&](std::size_t id) { candidates.push_back(id); return true; });
    require(candidates == std::vector<std::size_t>({0}),
            "Full geometry crossing cell corners was rejected by its center radius");
}

void acknowledge(Replication& replication, const ZombieSetups& batch) {
    replication.setupsSent(batch);
    ZombieSetupAck ack;
    for (const auto& setup : batch.zombies) ack.zombies.push_back({setup.id, setup.version});
    replication.acknowledgeSetups(ack);
}

void affineSetup(Scene& scene) {
    placements(scene, {{3, 2, -7}});
    auto& zombie = scene.zombies.instances[0];
    zombie.restRoot = glm::rotate(zombie.restRoot, glm::radians(37.f), glm::vec3(0, 1, 0));
    auto normalization = glm::scale(glm::mat4(1), glm::vec3(-2, 3, 4));
    normalization[1].x = .3f; normalization[3] = {1, 2, 3, 1};
    zombie.transform = zombie.restTransform = zombie.restRoot * normalization;
    scene.zombies.markRenderChanged(0);
    Replication replication;
    replication.snapshot(scene, {}, {});
    const auto setup = replication.nextSetups(scene);
    require(setup && setup->zombies.size() == 1, "Affine actor lost its initial setup");
    const auto bytes = encode(*setup);
    const auto copy = std::get<ZombieSetups>(decode(bytes.data(), bytes.size()));
    const auto& decoded = copy.zombies[0].normalization;
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            require(std::abs(decoded[column][row] - normalization[column][row]) < .0001f,
                    "Setup lost normalization while separating a rotated root");
}

void capacityAndCadence(Scene& scene) {
    std::vector<glm::vec3> feet(maxZombies);
    for (std::size_t id = 0; id < feet.size(); ++id)
        feet[id] = {.01f * float(id % 10), 0, .01f * float((id / 10) % 10)};
    placements(scene, feet);
    Replication replication;
    Snapshot initial; initial.tick = 3;
    require(replication.snapshot(scene, {}, initial).zombies.empty(), "Motion arrived before its initial setup acknowledgement");
    require(replication.interestedCount() == maxVisibleZombies && scene.zombies.instances.size() == maxZombies,
            "100k world and 10k peer capacities were not kept separate");
    std::vector<ZombieSetups> blocked;
    for (int n = 0; n < 2; ++n) {
        auto batch = replication.nextSetups(scene);
        require(batch && batch->zombies.size() == maxZombieSetupsPerBatch, "Initial setup batch was missing or unbounded");
        replication.setupsSent(*batch); blocked.push_back(*batch);
    }
    require(!replication.nextSetups(scene), "Slow peer exceeded its setup-transfer window");
    for (const auto& batch : blocked) {
        ZombieSetupAck ack;
        for (const auto& setup : batch.zombies) ack.zombies.push_back({setup.id, setup.version});
        replication.acknowledgeSetups(ack);
    }
    std::size_t established = 2 * maxZombieSetupsPerBatch;
    while (const auto batch = replication.nextSetups(scene)) {
        established += batch->zombies.size(); acknowledge(replication, *batch);
    }
    require(established == maxVisibleZombies, "Setup traversal lost selected actors or included the other 90k");
    for (std::uint64_t tick = 6; tick <= 180; tick += 3) {
        Snapshot frame; frame.tick = tick;
        require(replication.snapshot(scene, {}, frame).zombies.size() <= maxZombieUpdatesPerSnapshot,
                "Initial synchronization exceeded its per-snapshot record budget");
    }
    std::vector<unsigned> counts(maxVisibleZombies);
    std::size_t total = 0;
    for (std::uint64_t tick = 183; tick <= 240; tick += 3) {
        Snapshot frame; frame.tick = tick;
        const auto snapshot = replication.snapshot(scene, {}, frame);
        require(snapshot.zombies.size() <= maxZombieUpdatesPerSnapshot && encode(snapshot).size() <= maxMessageBytes,
                "Scheduled state exceeded its record or message budget");
        for (const auto& z : snapshot.zombies) {
            require(z.id < maxVisibleZombies, "Synchronization leaked outside the selected 10k population");
            ++counts[z.id]; ++total;
        }
    }
    for (std::size_t id = 0; id < counts.size(); ++id)
        require(counts[id] == (id < 256 ? 20u : id < 1024 ? 5u : 1u),
                "Idle actors did not receive their normal 20/5/1 Hz tier updates");
    require(total == 17936, "One-second steady-state cadence counts changed");
    scene.zombies.instances[9000].walking = true;
    Snapshot urgent; urgent.tick = 243;
    const auto changed = replication.snapshot(scene, {}, urgent);
    require(std::any_of(changed.zombies.begin(), changed.zombies.end(), [](const auto& z) { return z.id == 9000; }),
            "A far actor's animation-mode transition waited for its 1 Hz slot");
}

void adversarialIds(Scene& scene) {
    // Five thousand selected IDs all share the same modulo-twenty residue.
    // Scheduling by ID would put every far update behind the full near/middle
    // population in a single pass, starving the far tier indefinitely.
    for (std::size_t id = 0; id < scene.zombies.instances.size(); ++id) {
        scene.zombies.instances[id].removed = id % 20 != 0;
        scene.zombies.instances[id].walking = false;
    }
    scene.zombies.rebuildSpatialIndex();
    Replication replication;
    Snapshot initial; initial.tick = 300;
    replication.snapshot(scene, {}, initial);
    while (const auto batch = replication.nextSetups(scene)) acknowledge(replication, *batch);
    for (std::uint64_t tick = 303; tick <= 600; tick += 3) {
        Snapshot frame; frame.tick = tick;
        replication.snapshot(scene, {}, frame);
    }
    std::vector<unsigned> counts(maxZombies / 20);
    for (std::uint64_t tick = 603; tick <= 720; tick += 3) {
        Snapshot frame; frame.tick = tick;
        const auto snapshot = replication.snapshot(scene, {}, frame);
        for (const auto& z : snapshot.zombies) {
            require(z.id % 20 == 0, "Hidden actor entered adversarial-ID interest");
            ++counts[z.id / 20];
        }
    }
    for (std::size_t rank = 0; rank < counts.size(); ++rank)
        require(counts[rank] == (rank < 256 ? 40u : rank < 1024 ? 10u : 2u),
                "Adversarial ID residues starved a cadence tier");
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("Expected test map");
        Options options; options.map = argv[1]; options.zombieCount = 0;
        options.trees = options.forests = false;
        Scene scene(options);
        cellOrder(); visibility(scene); boundsAcrossCells(scene); affineSetup(scene);
        capacityAndCadence(scene); adversarialIds(scene);
        std::cout << "Cell ordering, visibility, 100k/10k capacity and idle update tiers passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
