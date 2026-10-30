#pragma once
#include "scene.hpp"
#include "ragdoll_playback.hpp"
#include "vehicle_playback.hpp"
#include "zombie_prediction.hpp"
#include <deque>
#include <memory>

namespace multiplayer {
class Snapshots {
public:
    bool push(Snapshot snapshot, double receivedSeconds, double transitSeconds = 0);
    void pushSetups(Scene& scene, const ZombieSetups& setups);
    void pushPose(const Scene& scene, const CorpsePose& pose, double receivedSeconds, double transitSeconds = 0);
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
    struct ZombieReplica {
        std::optional<ZombieSetup> setup;
        RagdollPlayback poses;
        ZombiePrediction motion;
        double received = 0;
        std::uint64_t appliedSetupVersion = 0, appliedPoseRevision = 0;
        bool tracked = false;
    };
    ZombieReplica& zombie(std::uint32_t id);
    void trackZombies(const Snapshot& snapshot, double receivedSeconds);
    void applyZombies(Scene& scene, double now, double estimatedServerTick);
    void forgetZombie(std::uint32_t id);
    std::deque<Snapshot> frames_;
    std::vector<PlayerState> players_;
    std::vector<viewer::GrenadePose> grenades_;
    VehiclePlayback vehicles_;
    double received_ = 0, renderTick_ = 0, transitSeconds_ = 0;
    // Direct stable-ID lookup lives on the heap. Replica payloads are allocated
    // lazily; frame work visits only the sorted, bounded tracked-ID list.
    std::vector<std::unique_ptr<ZombieReplica>> zombies_;
    std::vector<std::uint32_t> trackedZombieIds_, hiddenZombieIds_, publishZombieIds_;
};
} // namespace multiplayer
