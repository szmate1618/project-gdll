#pragma once

#include "client/snapshots.hpp"
#include "server/world.hpp"
#include <algorithm>

namespace test_replication {
inline multiplayer::ZombieSetupAck acknowledge(const multiplayer::ZombieSetups& setups) {
    multiplayer::ZombieSetupAck ack;
    for (const auto& setup : setups.zombies) ack.zombies.push_back({setup.id, setup.version});
    return ack;
}

inline void appendSamples(multiplayer::Snapshot& state, multiplayer::Snapshot newer) {
    for (const auto& sample : state.zombies)
        if (std::none_of(newer.zombies.begin(), newer.zombies.end(), [&](const auto& next) { return next.id == sample.id; }))
            newer.zombies.push_back(sample);
    state = std::move(newer);
}

// Exercise the real per-peer reliable setup handshake before asserting that a
// fresh observer can receive motion. Small fixtures drain it synchronously.
inline multiplayer::Snapshot readySnapshot(multiplayer::World& world, std::uint32_t observer) {
    auto state = world.snapshot(observer);
    bool initialized = false;
    while (const auto setups = world.nextSetups(observer)) {
        world.setupsSent(observer, *setups);
        world.acknowledgeSetups(observer, acknowledge(*setups));
        initialized = true;
    }
    if (initialized) appendSamples(state, world.snapshot(observer));
    return state;
}

inline multiplayer::Snapshot readySnapshot(multiplayer::Replication& replication, multiplayer::Scene& scene,
                                            glm::vec3 feet, multiplayer::Snapshot base) {
    auto state = replication.snapshot(scene, feet, base);
    bool initialized = false;
    while (const auto setups = replication.nextSetups(scene)) {
        replication.setupsSent(*setups);
        replication.acknowledgeSetups(acknowledge(*setups));
        initialized = true;
    }
    if (initialized) appendSamples(state, replication.snapshot(scene, feet, std::move(base)));
    return state;
}

inline multiplayer::ZombieSetup setup(const multiplayer::Scene& scene, std::uint32_t id) {
    const auto& instance = scene.zombies.instances.at(id);
    multiplayer::ZombieSetup result;
    result.id = id; result.asset = static_cast<std::uint32_t>(instance.asset); result.version = 1;
    result.normalization = glm::inverse(instance.restRoot) * instance.restTransform;
    result.phase = instance.phase;
    if (const auto* wound = scene.zombies.wounds().find(id)) result.woundMask = wound->vertexMask;
    return result;
}

inline void installSetups(multiplayer::Snapshots& playback, multiplayer::Scene& client,
                          const multiplayer::Scene& authority) {
    multiplayer::ZombieSetups batch;
    for (std::size_t id = 0; id < authority.zombies.instances.size(); ++id) {
        batch.zombies.push_back(setup(authority, static_cast<std::uint32_t>(id)));
        if (batch.zombies.size() == multiplayer::maxZombieSetupsPerBatch) {
            playback.pushSetups(client, batch);
            batch.zombies.clear();
        }
    }
    if (!batch.zombies.empty()) playback.pushSetups(client, batch);
}
} // namespace test_replication
