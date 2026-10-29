#include "client/snapshots.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace multiplayer;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class F> void rejects(F run, const char* message) {
    bool rejected = false;
    try { run(); } catch (const std::exception&) { rejected = true; }
    require(rejected, message);
}
void reset(Scene& scene, std::size_t count) {
    scene.zombies = viewer::ZombieLayer{};
    auto& asset = scene.zombies.assets.emplace_back();
    asset.skeleton.resize(1);
    asset.skeleton[0].name = "torso";
    asset.ragdollBones[std::size_t(viewer::RagdollPart::torso)] = 0;
    auto& primitive = asset.model.primitives.emplace_back();
    for (const auto point : {glm::vec3(-1, 0, 0), glm::vec3(1, 0, 0), glm::vec3(0, 2, 0)}) {
        viewer::Vertex vertex; vertex.position = point; primitive.vertices.push_back(vertex);
    }
    primitive.indices = {0, 1, 2};
    asset.animation.resize(1); asset.animation[0].skinning.resize(3);
    for (auto& skin : asset.animation[0].skinning) skin.weights[0] = 1;
    scene.zombies.instances.resize(count);
    for (auto& instance : scene.zombies.instances) instance.removed = true;
    const auto revision = scene.zombies.renderUpdates().since(0).revision;
    scene.zombies.renderUpdates().acknowledge(revision);
}
ZombieSetup setup(std::uint32_t id, std::uint64_t version = 1, float phase = .25f) {
    ZombieSetup result; result.id = id; result.version = version; result.phase = phase;
    return result;
}
Snapshot motion(std::uint64_t tick, std::uint32_t id, float x, std::uint8_t period = 3,
                bool physics = false, float walkSeconds = 0) {
    Snapshot result; result.tick = tick;
    auto& state = result.zombies.emplace_back();
    state.id = id; state.root[3] = {x, 0, 0, 1};
    state.updatePeriodTicks = period; state.dead = physics;
    state.walking = walkSeconds != 0; state.walkSeconds = walkSeconds;
    return result;
}
void persistenceAndExpiry(Scene& scene) {
    reset(scene, 1);
    Snapshots playback;
    playback.pushSetups(scene, ZombieSetups{{setup(0)}});
    playback.push(motion(60, 0, 2), 0); playback.apply(scene, 0);
    require(!scene.zombies.instances[0].removed, "Initial established motion was hidden");
    Snapshot other; other.tick = 63;
    playback.push(other, .05); playback.apply(scene, .05);
    require(!scene.zombies.instances[0].removed, "Omitted motion incorrectly removed persistent zombie");
    playback.apply(scene, 4.99);
    require(!scene.zombies.instances[0].removed, "Zombie expired before its five-second timeout");
    playback.apply(scene, 5);
    require(scene.zombies.instances[0].removed && playback.trackedZombieIds().empty(),
        "Expired zombie remained tracked or visible");
    playback.push(motion(366, 0, 12), 5.1); playback.apply(scene, 5.1);
    require(!scene.zombies.instances[0].removed && scene.zombies.instances[0].restRoot[3].x == 12 &&
            scene.zombies.instances[0].phase == .25f,
        "Fresh motion did not restore retained metadata without blending expired position");
    require(!playback.push(motion(365, 0, 90), 5.2), "Stale motion batch was accepted");
    playback.apply(scene, 10.2);
    require(scene.zombies.instances[0].removed, "Rejected stale motion extended presence timeout");
}
void independentSlowHistory(Scene& scene) {
    reset(scene, 1);
    Snapshots playback;
    playback.pushSetups(scene, ZombieSetups{{setup(0)}});
    playback.push(motion(60, 0, 0, 60), 0); playback.apply(scene, 0);
    playback.push(motion(120, 0, 10, 60, false, 1), 1); playback.apply(scene, 1);
    Snapshot empty; empty.tick = 123;
    playback.push(empty, 1.05); playback.apply(scene, 1.5);
    require(std::abs(scene.zombies.instances[0].restRoot[3].x - 5) < .001f &&
            std::abs(scene.zombies.instances[0].walkSeconds - .5) < .001,
        "One-Hz zombie did not interpolate independently of empty world snapshots");
    for (int i = 0; i < 35; ++i) {
        empty.tick = static_cast<std::uint64_t>(126 + i * 3);
        playback.push(empty, 1.1 + i * .05);
        playback.apply(scene, 1.1 + i * .05);
    }
    require(!scene.zombies.instances[0].removed && scene.zombies.instances[0].restRoot[3].x == 10,
        "Trimming world history discarded persistent actor history");
}
void metadataAndNormalization(Scene& scene) {
    reset(scene, 1);
    Snapshots playback;
    auto first = setup(0);
    first.normalization = glm::scale(glm::mat4(1), glm::vec3(-2, 3, 4));
    first.normalization[1].x = .3f; first.normalization[3] = {1, 2, 3, 1};
    playback.push(motion(60, 0, 0), 0); playback.apply(scene, 0);
    require(scene.zombies.instances[0].removed, "Motion displayed before asset metadata was established");
    playback.pushSetups(scene, ZombieSetups{{first}}); playback.apply(scene, 0);
    auto next = motion(90, 0, 10);
    next.zombies[0].root = glm::rotate(glm::mat4(1), glm::radians(90.f), glm::vec3(0, 1, 0));
    next.zombies[0].root[3] = {10, 0, 0, 1};
    playback.push(next, .5); playback.apply(scene, .5);
    const auto& instance = scene.zombies.instances[0];
    require(std::abs(instance.restRoot[3].x - 8) < .001f &&
            std::abs(glm::length(glm::vec3(instance.transform[0])) - 2) < .001f &&
            std::abs(glm::determinant(glm::mat3(instance.transform)) + 24) < .001f,
        "Compact interpolation changed root position or fixed affine normalization");
    const auto forward = glm::normalize(glm::vec3(instance.restRoot[2]));
    require(std::abs(forward.x - std::sin(glm::radians(72.f))) < .001f,
        "Quaternion playback did not interpolate the rigid root turn");
    auto wounded = first; wounded.version = 2; wounded.phase = .75f; wounded.woundMask = {1};
    playback.pushSetups(scene, ZombieSetups{{wounded}}); playback.apply(scene, .5);
    require(instance.phase == .75f && scene.zombies.wounds().find(0) &&
            scene.zombies.wounds().find(0)->vertexMask == std::vector<std::uint8_t>{1},
        "Versioned setup did not publish authoritative wound metadata");
    auto cleared = wounded; cleared.version = 3; cleared.woundMask.clear();
    playback.pushSetups(scene, ZombieSetups{{cleared}});
    playback.pushSetups(scene, ZombieSetups{{wounded}}); playback.apply(scene, .5);
    require(!scene.zombies.wounds().find(0) && instance.phase == .75f,
        "Stale metadata restored an older wound or phase");
    auto bad = setup(0, 4); bad.asset = 1;
    rejects([&] { playback.pushSetups(scene, ZombieSetups{{bad}}); }, "Unknown setup asset was accepted");
    bad = setup(0, 4); bad.normalization[0][3] = 1;
    rejects([&] { playback.pushSetups(scene, ZombieSetups{{bad}}); }, "Projective normalization was accepted");
}
void completePhysicsPoses(Scene& scene) {
    reset(scene, 1);
    Snapshots playback;
    playback.pushSetups(scene, ZombieSetups{{setup(0)}});
    playback.push(motion(60, 0, 1, 3, true), 0); playback.apply(scene, 0);
    require(scene.zombies.instances[0].removed, "Physics actor displayed without its first complete pose");
    CorpsePose pose; pose.version = 1; pose.bones.resize(1, glm::mat4(1));
    pose.transform[3] = {3, 0, 0, 1}; pose.bones[0][0].x = 2;
    playback.pushPose(scene, pose); playback.apply(scene, .01);
    require(!scene.zombies.instances[0].removed && scene.zombies.instances[0].transform[3].x == 3 &&
            scene.zombies.instances[0].ragdollBones[0][0].x == 2,
        "First physics root and palette were not applied together");
    auto next = pose; next.version = 2; next.transform[3].x = 7; next.bones[0][1].x = .6f;
    playback.pushPose(scene, next); playback.pushPose(scene, pose); playback.apply(scene, .02);
    require(scene.zombies.instances[0].transform[3].x == 7 &&
            scene.zombies.instances[0].ragdollBones[0][1].x == .6f,
        "Physics palettes were blended, reordered or paired with an old root");
    playback.apply(scene, 5);
    next.version = 3; next.transform[3].x = 11;
    playback.pushPose(scene, next); playback.apply(scene, 6);
    require(scene.zombies.instances[0].removed && playback.trackedZombieIds().empty(),
        "A pose-only message resurrected an expired actor");
    playback.push(motion(420, 0, 1, 3, true), 6.1); playback.apply(scene, 6.1);
    require(!scene.zombies.instances[0].removed && scene.zombies.instances[0].transform[3].x == 11,
        "Re-entering actor lost its retained complete physics pose");
}
void populationCapacityAndOrder(Scene& scene) {
    reset(scene, maxZombies);
    Snapshots playback;
    playback.pushSetups(scene, ZombieSetups{{setup(99999), setup(7)}});
    auto first = motion(60, 99999, 1);
    auto low = motion(60, 7, 2);
    first.zombies.push_back(low.zombies.front());
    playback.push(first, 0); playback.apply(scene, 0);
    require(playback.trackedZombieIds() == std::vector<std::uint32_t>({7, 99999}),
        "High stable IDs were truncated or tracked order followed packet order");
    require(!scene.zombies.instances[7].removed && !scene.zombies.instances[99999].removed,
        "High-ID actor was not reconstructed in the full population buffer");
    auto revision = scene.zombies.renderUpdates().since(0).revision;
    scene.zombies.renderUpdates().acknowledge(revision);
    playback.apply(scene, .01);
    const auto changes = scene.zombies.renderUpdates().since(revision);
    require(!changes.full && changes.ids && *changes.ids == std::vector<std::size_t>({7, 99999}),
        "Render-state publication lost ascending stable-ID order");
    Snapshots limited;
    std::uint64_t tick = 3;
    for (std::size_t firstId = 0; firstId <= maxVisibleZombies; firstId += maxZombieUpdatesPerSnapshot) {
        Snapshot batch; batch.tick = tick;
        const auto end = std::min(maxVisibleZombies + 1, firstId + maxZombieUpdatesPerSnapshot);
        for (std::size_t id = end; id-- > firstId;) {
            ZombieState state; state.id = static_cast<std::uint32_t>(id); batch.zombies.push_back(state);
        }
        limited.push(std::move(batch), double(tick) / 60);
        tick += 3;
    }
    const auto& ids = limited.trackedZombieIds();
    require(ids.size() == maxVisibleZombies && std::is_sorted(ids.begin(), ids.end()) &&
            std::binary_search(ids.begin(), ids.end(), 0u) &&
            !std::binary_search(ids.begin(), ids.end(), static_cast<std::uint32_t>(maxZombieUpdatesPerSnapshot - 1)) &&
            std::binary_search(ids.begin(), ids.end(), 10000u),
        "Tracked population exceeded 10000 or did not evict the oldest presence sample");
    auto invalid = motion(tick, static_cast<std::uint32_t>(maxZombies), 0);
    rejects([&] { limited.push(invalid, 1); }, "Motion beyond the global ID capacity was accepted");
}
}
int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("Test asset argument missing");
        Options options; options.map = argv[1]; options.trees = options.forests = false; options.zombieCount = 0;
        Scene scene(options);
        persistenceAndExpiry(scene);
        independentSlowHistory(scene);
        metadataAndNormalization(scene);
        completePhysicsPoses(scene);
        populationCapacityAndOrder(scene);
        std::cout << "Persistent sparse zombie playback, setup versions, expiry and ID order passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
