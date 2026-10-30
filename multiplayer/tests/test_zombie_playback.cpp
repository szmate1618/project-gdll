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
void independentSlowPrediction(Scene& scene) {
    reset(scene, 1);
    Snapshots playback;
    playback.pushSetups(scene, ZombieSetups{{setup(0)}});
    playback.push(motion(60, 0, 0, 60, false, .25f), 0); playback.apply(scene, 0);
    playback.push(motion(120, 0, 3, 60, false, 1.25f), 1); playback.apply(scene, 1);
    require(std::abs(scene.zombies.instances[0].restRoot[3].x - 3) < .001f,
        "One-Hz zombie retained an interpolation delay on receiving motion");
    Snapshot empty; empty.tick = 123;
    playback.push(empty, 1.05); playback.apply(scene, 1.5);
    require(std::abs(scene.zombies.instances[0].restRoot[3].x - 4.5f) < .001f &&
            std::abs(scene.zombies.instances[0].walkSeconds - 1.75) < .001,
        "One-Hz zombie did not predict independently of empty world snapshots");
    for (int i = 0; i < 35; ++i) {
        empty.tick = static_cast<std::uint64_t>(126 + i * 3);
        playback.push(empty, 1.1 + i * .05);
        playback.apply(scene, 1.1 + i * .05);
    }
    require(!scene.zombies.instances[0].removed &&
            std::abs(scene.zombies.instances[0].restRoot[3].x - 6) < .001f,
        "Sparse zombie prediction did not hold at its bounded horizon");
}
void nearbyPredictionAndTransit(Scene& scene) {
    reset(scene, 1);
    Snapshots playback;
    auto metadata = setup(0);
    metadata.normalization = glm::scale(glm::mat4(1), {-2, 3, 4});
    metadata.normalization[1].x = .3f;
    playback.pushSetups(scene, ZombieSetups{{metadata}});
    playback.push(motion(60, 0, 0, 3, false, .25f), 0);
    playback.apply(scene, 0);
    playback.push(motion(63, 0, .15f, 3, false, .275f), .05, .02);
    playback.apply(scene, .05);
    const auto& instance = scene.zombies.instances[0];
    require(std::abs(instance.restRoot[3].x - .21f) < .001f &&
            std::abs(instance.walkSeconds - .285) < .001 &&
            std::abs(instance.idleSeconds - 1.07) < .001,
        "Nearby prediction did not include incoming transit or the measured clip rate");
    require(std::abs(playback.animationSeconds() - 1) < .001,
        "Living prediction changed the separate buffered world clock");
    playback.apply(scene, .075);
    require(std::abs(instance.restRoot[3].x - .285f) < .001f &&
            std::abs(instance.walkSeconds - .2975) < .001 &&
            std::abs(glm::determinant(glm::mat3(instance.transform)) + 24) < .001,
        "Predicted frame motion lost clip synchronization or affine normalization");
    playback.apply(scene, .5);
    require(std::abs(instance.restRoot[3].x - .45f) < .001f &&
            std::abs(instance.walkSeconds - .325) < .001 &&
            std::abs(instance.idleSeconds - 1.15) < .001,
        "Packet starvation did not freeze position and both animation clocks together");
    const auto last = instance.restRoot;
    require(!playback.push(motion(62, 0, 10), .51, .5), "Stale motion changed prediction");
    playback.apply(scene, .51);
    require(instance.restRoot == last, "Rejected snapshot changed the prediction clock");
    rejects([&] { playback.push(motion(66, 0, .3f), .6, -1); },
        "Negative network transit estimate was accepted");
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
    require(std::abs(instance.restRoot[3].x - 10) < .001f &&
            std::abs(glm::length(glm::vec3(instance.transform[0])) - 2) < .001f &&
            std::abs(glm::determinant(glm::mat3(instance.transform)) + 24) < .001f,
        "Immediate motion publication changed root position or fixed affine normalization");
    const auto forward = glm::normalize(glm::vec3(instance.restRoot[2]));
    require(std::abs(forward.x - 1) < .001f,
        "Latest rigid root turn retained a playback delay");
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
void authoredAttackPlayback(Scene& scene) {
    reset(scene, 1);
    auto& asset = scene.zombies.assets[0];
    asset.attackLeftClip = viewer::BakedAnimationClip{"attack_left", 2.3333333f, 71, 0, {}, false};
    Snapshots playback;
    playback.pushSetups(scene, ZombieSetups{{setup(0)}});
    auto snapshot = motion(60, 0, 0);
    snapshot.zombies[0].walking = false;
    snapshot.zombies[0].attackClip = 1; snapshot.zombies[0].attackSeconds = .4f;
    playback.push(snapshot, 0, .025); playback.apply(scene, .05);
    auto& zombie = scene.zombies.instances[0];
    require(zombie.attackClip == 1 && std::abs(zombie.attackSeconds - .475) < .0001 && !zombie.walking,
        "Attack variant/clock was not reconstructed on the unbuffered server timeline");
    playback.apply(scene, 3);
    require(std::abs(zombie.attackSeconds - asset.attackLeftClip->duration) < .0001,
        "Starved attack playback wrapped instead of holding its final frame");
    playback.push(motion(66, 0, .1f, 3, false, .1f), 3.01); playback.apply(scene, 3.01);
    require(zombie.attackClip == 0, "Locomotion retained an obsolete attack animation");
}
void completePhysicsPoses(Scene& scene) {
    reset(scene, 1);
    Snapshots playback;
    playback.pushSetups(scene, ZombieSetups{{setup(0)}});
    playback.push(motion(60, 0, 1, 3, true), 0); playback.apply(scene, 0);
    require(scene.zombies.instances[0].removed, "Physics actor displayed without its first complete pose");
    CorpsePose pose; pose.version = 1; pose.bones.resize(1, glm::mat4(1));
    pose.transform[3] = {3, 0, 0, 1}; pose.bones[0][0].x = 2;
    playback.pushPose(scene, pose, .01); playback.apply(scene, .01);
    require(!scene.zombies.instances[0].removed && scene.zombies.instances[0].transform[3].x == 3 &&
            scene.zombies.instances[0].ragdollBones[0][0].x == 2,
        "First physics root and palette were not applied together");
    auto next = pose; next.version = 2; next.transform[3].x = 7; next.bones[0][1].x = .6f;
    playback.pushPose(scene, next, .02); playback.pushPose(scene, pose, .02); playback.apply(scene, .2);
    require(scene.zombies.instances[0].transform[3].x == 7 &&
            scene.zombies.instances[0].ragdollBones[0][1].x == .6f,
        "Physics palettes were blended, reordered or paired with an old root");
    playback.apply(scene, 5);
    next.version = 3; next.transform[3].x = 11;
    playback.pushPose(scene, next, 6); playback.apply(scene, 6);
    require(scene.zombies.instances[0].removed && playback.trackedZombieIds().empty(),
        "A pose-only message resurrected an expired actor");
    playback.push(motion(420, 0, 1, 3, true), 6.1); playback.apply(scene, 6.1);
    require(!scene.zombies.instances[0].removed && scene.zombies.instances[0].transform[3].x == 11,
        "Re-entering actor lost its retained complete physics pose");
}
void smoothPhysicsPoses(Scene& scene) {
    reset(scene, 1);
    Snapshots playback;
    playback.pushSetups(scene, ZombieSetups{{setup(0)}});
    playback.push(motion(60, 0, 0, 3, true), 0);
    CorpsePose first; first.version = 1; first.tick = 60;
    first.bones.push_back(glm::translate(glm::mat4(1), {2, 0, 0}));
    playback.pushPose(scene, first, 0); playback.apply(scene, 0);
    auto next = first; next.version = 2; next.tick = 72; next.transform[3].x = 2;
    playback.pushPose(scene, next, .2); playback.apply(scene, .2);
    auto& instance = scene.zombies.instances[0];
    require(instance.transform[3].x == 2, "New ragdoll sample was delayed by a playback buffer");
    playback.apply(scene, .3);
    require(std::abs(instance.transform[3].x - 3) < .001f &&
            std::abs((instance.transform * instance.ragdollBones[0])[3].x - 5) < .001f,
        "Five-Hz ragdoll did not predict root and skin together");
    playback.apply(scene, .35);
    require(std::abs(instance.transform[3].x - 3.5f) < .001f,
        "Same-version predicted palette was not published on the next render frame");
    playback.pushPose(scene, first, .36); playback.apply(scene, .36);
    require(std::abs(instance.transform[3].x - 3.6f) < .001f,
        "Stale pose reset prediction or overwrote newer motion");
    playback.apply(scene, .7);
    const auto frozenVersion = instance.ragdollPoseVersion;
    playback.apply(scene, 1);
    require(std::abs(instance.transform[3].x - 6) < .001f && instance.ragdollPoseVersion == frozenVersion,
        "Starved ragdoll predicted indefinitely or kept uploading a frozen palette");
    next.version = 3; next.tick = 84; next.transform[3].x = 2.5f; next.sleeping = true;
    playback.pushPose(scene, next, 1.1); playback.apply(scene, 1.1);
    require(instance.transform == next.transform, "Sleeping corpse retained predicted motion");
    const auto settledVersion = instance.ragdollPoseVersion;
    playback.apply(scene, 1.5);
    require(instance.ragdollPoseVersion == settledVersion, "Settled corpse kept publishing palettes");
    next.version = 4; next.tick = 180; next.transform[3].x = 4; next.sleeping = false;
    playback.pushPose(scene, next, 2); playback.apply(scene, 2);
    require(instance.transform == next.transform, "Waking corpse interpolated across its idle interval");
}
void changingPhysicsCadence(Scene& scene) {
    reset(scene, 1);
    Snapshots playback;
    playback.pushSetups(scene, ZombieSetups{{setup(0)}});
    playback.push(motion(60, 0, 0, 3, true), 0);
    CorpsePose pose; pose.version = 1; pose.tick = 60; pose.bones.emplace_back(1);
    playback.pushPose(scene, pose, 0); playback.apply(scene, 0);
    pose.version = 2; pose.tick = 72; pose.transform[3].x = 1;
    playback.pushPose(scene, pose, .3); // Arrival jitter must not change measured speed.
    pose.version = 3; pose.tick = 84; pose.transform[3].x = 2;
    playback.pushPose(scene, pose, .3, .05); playback.apply(scene, .35);
    require(std::abs(scene.zombies.instances[0].transform[3].x - 2.5f) < .001f,
        "Batched five-Hz poses used arrival spacing or ignored estimated transit time");
    pose.version = 4; pose.tick = 96; pose.transform[3].x = 50;
    playback.pushPose(scene, pose, .4); playback.apply(scene, .5);
    require(scene.zombies.instances[0].transform == pose.transform,
        "A teleport was extrapolated into an unbounded launch");
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
        independentSlowPrediction(scene);
        nearbyPredictionAndTransit(scene);
        metadataAndNormalization(scene);
        authoredAttackPlayback(scene); completePhysicsPoses(scene);
        smoothPhysicsPoses(scene);
        changingPhysicsCadence(scene);
        populationCapacityAndOrder(scene);
        std::cout << "Persistent sparse zombie playback, setup versions, expiry and ID order passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
