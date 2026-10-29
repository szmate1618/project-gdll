#pragma once
#include "scene.hpp"
#include <deque>
#include <memory>

namespace multiplayer {
class Snapshots {
public:
    bool push(Snapshot snapshot, double receivedSeconds);
    void pushSetups(Scene& scene, const ZombieSetups& setups);
    void pushPose(const Scene& scene, const CorpsePose& pose);
    const Snapshot& latest() const { return frames_.back(); }
    bool empty() const { return frames_.empty(); }
    void apply(Scene& scene, double now);
    const std::vector<PlayerState>& players() const { return players_; }
    const std::vector<viewer::GrenadePose>& grenades() const { return grenades_; }
    std::vector<viewer::MuzzleFlashLight> explosionLights(double now) const;
    std::vector<viewer::GrenadeExplosion> explosions(double now) const;
    double animationSeconds() const { return renderTick_ / 60; }
    // Stable IDs in ascending order, including entries waiting for their first
    // complete setup/physics pose. Membership is bounded independently of the
    // authoritative population and is refreshed by ordinary motion samples.
    const std::vector<std::uint32_t>& trackedZombieIds() const { return trackedZombieIds_; }
private:
    struct ZombieSample {
        std::uint64_t tick = 0;
        ZombieState state;
    };
    struct ZombieReplica {
        std::optional<ZombieSetup> setup;
        std::optional<CorpsePose> pose;
        std::vector<ZombieSample> samples;
        double received = 0, renderTick = 0;
        std::uint64_t appliedSetupVersion = 0, appliedPoseVersion = 0;
        bool tracked = false;
    };
    ZombieReplica& zombie(std::uint32_t id);
    void trackZombies(const Snapshot& snapshot, double receivedSeconds);
    void applyZombies(Scene& scene, double now, double estimatedServerTick);
    void forgetZombie(std::uint32_t id);
    std::deque<Snapshot> frames_;
    std::vector<PlayerState> players_;
    std::vector<viewer::GrenadePose> grenades_;
    double received_ = 0, renderTick_ = 0;
    // Direct stable-ID lookup lives on the heap. Replica payloads are allocated
    // lazily; frame work visits only the sorted, bounded tracked-ID list.
    std::vector<std::unique_ptr<ZombieReplica>> zombies_;
    std::vector<std::uint32_t> trackedZombieIds_, hiddenZombieIds_, publishZombieIds_;
};
} // namespace multiplayer
