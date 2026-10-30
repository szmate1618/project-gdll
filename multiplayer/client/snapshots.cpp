#include "snapshots.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <unordered_map>
#include <stdexcept>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace multiplayer {
Snapshots::ZombieReplica& Snapshots::zombie(std::uint32_t id) {
    if (id >= maxZombies) throw std::runtime_error("Zombie ID exceeds session capacity");
    if (zombies_.size() <= id) zombies_.resize(static_cast<std::size_t>(id) + 1);
    auto& replica = zombies_[id];
    if (!replica) replica = std::make_unique<ZombieReplica>();
    return *replica;
}
void Snapshots::forgetZombie(std::uint32_t id) {
    auto& replica = *zombies_[id];
    replica.tracked = false;
    replica.motion.reset();
    replica.poses.resetHistory();
    // Setup and complete physics poses survive display-buffer retirement.
    hiddenZombieIds_.push_back(id);
}
void Snapshots::trackZombies(const Snapshot& snapshot, double receivedSeconds) {
    std::vector<std::uint32_t> added;
    added.reserve(snapshot.zombies.size());
    for (const auto& state : snapshot.zombies) {
        auto& replica = zombie(state.id);
        if (!replica.motion.empty() && replica.motion.latestTick() >= snapshot.tick) continue;
        if (!replica.tracked) {
            replica.tracked = true;
            replica.poses.resetHistory();
            added.push_back(state.id);
        }
        replica.received = receivedSeconds;
        replica.motion.push(snapshot.tick, state);
    }
    if (added.empty()) return;
    std::sort(added.begin(), added.end());
    added.erase(std::unique(added.begin(), added.end()), added.end());
    std::vector<std::uint32_t> merged;
    merged.reserve(trackedZombieIds_.size() + added.size());
    std::set_union(trackedZombieIds_.begin(), trackedZombieIds_.end(),
        added.begin(), added.end(), std::back_inserter(merged));
    if (merged.size() > maxVisibleZombies) {
        // Only membership changes require this selection. Prefer the newest
        // presence samples; ticks and stable IDs make equal-time ties explicit.
        const auto newer = [&](std::uint32_t left, std::uint32_t right) {
            const auto& a = *zombies_[left]; const auto& b = *zombies_[right];
            if (a.received != b.received) return a.received > b.received;
            if (a.motion.latestTick() != b.motion.latestTick())
                return a.motion.latestTick() > b.motion.latestTick();
            return left < right;
        };
        auto keptEnd = merged.begin() + static_cast<std::ptrdiff_t>(maxVisibleZombies);
        std::nth_element(merged.begin(), keptEnd, merged.end(), newer);
        for (auto it = keptEnd; it != merged.end(); ++it) forgetZombie(*it);
        merged.erase(keptEnd, merged.end());
        std::sort(merged.begin(), merged.end());
    }
    trackedZombieIds_ = std::move(merged);
}
bool Snapshots::push(Snapshot snapshot, double receivedSeconds, double transitSeconds) {
    if (!frames_.empty() && snapshot.tick <= frames_.back().tick) return false;
    if (!std::isfinite(receivedSeconds) || !std::isfinite(transitSeconds) || transitSeconds < 0 ||
        snapshot.zombies.size() > maxZombieUpdatesPerSnapshot)
        throw std::runtime_error("Invalid zombie update batch");
    for (const auto& state : snapshot.zombies)
        if (state.id >= maxZombies || (state.updatePeriodTicks != 3 && state.updatePeriodTicks != 12 && state.updatePeriodTicks != 60))
            throw std::runtime_error("Invalid zombie motion sample");
    vehicles_.push(snapshot.tick, snapshot.vehicles);
    trackZombies(snapshot, receivedSeconds);
    frames_.push_back(std::move(snapshot));
    if (frames_.size() > 32) frames_.pop_front();
    received_ = receivedSeconds;
    transitSeconds_ = std::min(transitSeconds, .5);
    return true;
}
void Snapshots::pushSetups(Scene& scene, const ZombieSetups& setups) {
    if (setups.zombies.size() > maxZombieSetupsPerBatch) throw std::runtime_error("Zombie setup batch exceeds capacity");
    // Validate before changing caches, including calls from CPU-only fixtures.
    for (const auto& setup : setups.zombies) {
        if (setup.id >= scene.zombies.instances.size() || setup.asset >= scene.zombies.assets.size() ||
            setup.asset != scene.zombies.instances[setup.id].asset || !setup.version ||
            !std::isfinite(setup.phase) || setup.phase < 0 || setup.phase >= 1 || setup.woundMask.size() > 1024)
            throw std::runtime_error("Zombie setup does not match session assets");
        for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r)
            if (!std::isfinite(setup.normalization[c][r])) throw std::runtime_error("Nonfinite zombie normalization");
        if (setup.normalization[0][3] != 0 || setup.normalization[1][3] != 0 ||
            setup.normalization[2][3] != 0 || setup.normalization[3][3] != 1 ||
            glm::determinant(glm::dmat3(setup.normalization)) == 0)
            throw std::runtime_error("Zombie normalization is not an invertible affine map");
    }
    for (const auto& setup : setups.zombies) {
        auto& cached = zombie(setup.id).setup;
        if (!cached || cached->version < setup.version) cached = setup;
    }
}
void Snapshots::pushPose(const Scene& scene, const CorpsePose& pose, double receivedSeconds) {
    if (pose.id >= scene.zombies.instances.size() || pose.asset >= scene.zombies.assets.size() ||
        pose.asset != scene.zombies.instances[pose.id].asset || !pose.version ||
        pose.bones.size() != scene.zombies.assets[pose.asset].skeleton.size())
        throw std::runtime_error("Corpse pose does not match session assets");
    if (pose.severedJoint >= 0 && (pose.asset >= scene.zombies.dismembermentAssets.size() ||
        std::size_t(pose.severedJoint) >= scene.zombies.dismembermentAssets[pose.asset].meshes.size()))
        throw std::runtime_error("Corpse separation does not match session assets");
    auto& replica = zombie(pose.id);
    replica.poses.push(pose, receivedSeconds);
    if (!replica.tracked) replica.poses.resetHistory();
}
std::vector<viewer::GrenadeExplosion> Snapshots::explosions(double now) const {
    std::vector<viewer::GrenadeExplosion> result;
    if (frames_.empty()) return result;
    for (auto explosion : frames_.back().explosions) {
        explosion.age += static_cast<float>(std::max(0.0, now - received_));
        result.push_back(explosion);
    }
    return result;
}
std::vector<viewer::MuzzleFlashLight> Snapshots::explosionLights(double now) const {
    std::vector<viewer::MuzzleFlashLight> result;
    for (const auto& explosion : explosions(now)) {
        const auto light = viewer::grenadeExplosionLight(explosion);
        if (light.color.r > 0) result.push_back(light);
    }
    return result;
}
void Snapshots::apply(Scene& scene, double now) {
    if (frames_.empty()) return;
    const double latestTick = static_cast<double>(frames_.back().tick);
    // RTT/2 estimates incoming transit for unbuffered presentation.
    const double estimatedServerTick = latestTick + (std::max(0.0, now - received_) + transitSeconds_) * 60;
    // Render six ticks behind incoming state; freeze at the latest state on starvation.
    renderTick_ = std::max(renderTick_, std::clamp(latestTick + (now - received_) * 60 - 6,
        static_cast<double>(frames_.front().tick), latestTick));
    while (frames_.size() > 2 && frames_[1].tick <= renderTick_) frames_.pop_front();
    const auto& a = frames_.front();
    const auto& b = frames_.size() > 1 ? frames_[1] : a;
    const float t = b.tick == a.tick ? 1.0f : static_cast<float>(std::clamp(
        (renderTick_ - static_cast<double>(a.tick)) / static_cast<double>(b.tick - a.tick), 0.0, 1.0));
    grenades_ = b.grenades;
    for (auto& grenade : grenades_) for (const auto& previous : a.grenades) if (previous.id == grenade.id) {
        grenade.center = glm::mix(previous.center, grenade.center, t);
        grenade.rotation = glm::slerp(previous.rotation, grenade.rotation, t);
        break;
    }
    // Explosion lights use the latest confirmed event. Remove its body on the
    // same frame even if pose interpolation is still behind the detonation.
    const auto& explosions = frames_.back().explosions;
    grenades_.erase(std::remove_if(grenades_.begin(), grenades_.end(), [&](const auto& grenade) {
        return std::any_of(explosions.begin(), explosions.end(), [&](const auto& e) { return e.id == grenade.id; });
    }), grenades_.end());
    players_ = b.players;
    for (auto& p : players_) for (const auto& before : a.players) if (before.id == p.id) {
        if (before.vehicle == p.vehicle && before.seat == p.seat) {
            p.movement.feet = glm::mix(before.movement.feet, p.movement.feet, t);
            p.melee = viewer::interpolatePlayerBodyPose(before.melee,p.melee,t);
        }
        break;
    }
    // All chassis, wheels and seat cameras share unbuffered predicted poses.
    vehicles_.apply(*scene.vehicles, estimatedServerTick);
    for (const auto& player : frames_.back().players) if (player.vehicle != noVehicle)
        (void)scene.vehicles->config(player.vehicle).seats.at(player.seat);
    std::unordered_map<std::uint32_t, float> oldAngles;
    for (const auto& d : a.doors) oldAngles.emplace(d.id, d.angle);
    for (const auto& d : b.doors) {
        if (d.id >= scene.doors->count()) throw std::runtime_error("Server supplied unknown door ID");
        float angle = d.angle;
        const auto before = oldAngles.find(d.id);
        if (before != oldAngles.end()) angle = glm::mix(before->second, angle, t);
        scene.doors->setAngle(d.id, angle);
    }
    // Box IDs are dense and never recycled during a session, including late joins.
    std::array<const BoxState*, maxBoxes> oldBoxes{};
    for (const auto& box : a.boxes) oldBoxes.at(box.id) = &box;
    for (const auto& box : b.boxes) {
        if (box.id > scene.boxes->count()) throw std::runtime_error("Server supplied non-dense box IDs");
        if (box.id == scene.boxes->count()) scene.boxes->add(box.center, box.halfExtents * 2.0f);
        if (box.removed) { scene.boxes->retire(box.id); continue; }
        auto center = box.center; auto rotation = box.rotation;
        if (const auto* before = oldBoxes.at(box.id)) {
            center = glm::mix(before->center, center, t); rotation = glm::slerp(before->rotation, rotation, t);
        }
        scene.boxes->setPose(box.id, center, rotation);
    }
    scene.zombies.setReplicatedWoundContents(b.woundContents);
    scene.zombies.setReplicatedBloodMist(b.bloodMist,double(b.tick)/60);
    applyZombies(scene, now, estimatedServerTick);
}
void Snapshots::applyZombies(Scene& scene, double now, double estimatedServerTick) {
    auto kept = trackedZombieIds_.begin();
    for (const auto id : trackedZombieIds_) {
        if (now - zombies_[id]->received >= zombieTimeoutSeconds) forgetZombie(id);
        else *kept++ = id;
    }
    trackedZombieIds_.erase(kept, trackedZombieIds_.end());
    std::sort(hiddenZombieIds_.begin(), hiddenZombieIds_.end());
    hiddenZombieIds_.erase(std::unique(hiddenZombieIds_.begin(), hiddenZombieIds_.end()), hiddenZombieIds_.end());
    auto& changed = publishZombieIds_;
    changed.clear();
    changed.reserve(trackedZombieIds_.size() + hiddenZombieIds_.size());
    std::set_union(trackedZombieIds_.begin(), trackedZombieIds_.end(),
        hiddenZombieIds_.begin(), hiddenZombieIds_.end(), std::back_inserter(changed));
    hiddenZombieIds_.clear();
    for (const auto id : changed) {
        if (id >= scene.zombies.instances.size()) throw std::runtime_error("Unknown zombie ID");
        auto& replica = *zombies_[id];
        auto& instance = scene.zombies.instances[id];
        if (!replica.tracked || !replica.setup) {
            if (!instance.removed) { instance.removed = true; scene.zombies.markRenderChanged(id); }
            continue;
        }
        const auto& setup = *replica.setup;
        if (setup.asset != instance.asset || setup.asset >= scene.zombies.assets.size()) throw std::runtime_error("Zombie asset mismatch");
        if (replica.appliedSetupVersion != setup.version) {
            if (!setup.woundMask.empty() || scene.zombies.wounds().find(id))
                scene.zombies.setWoundMask(id, setup.woundMask);
            replica.appliedSetupVersion = setup.version;
        }
        const auto& z = replica.motion.latest();
        instance.removed = false;
        instance.restRoot = z.root;
        instance.phase = setup.phase; instance.walkSeconds = z.walkSeconds;
        instance.walking = z.walking; instance.running = z.running; instance.ragdoll = z.dead;
        instance.idleSeconds = -1;
        instance.displayedAnimationFrame = -1;
        if (!z.dead) {
            const auto predicted = replica.motion.sample(estimatedServerTick, now);
            instance.restRoot = predicted.root;
            instance.walkSeconds = predicted.walkSeconds;
            instance.idleSeconds = predicted.idleSeconds;
        }
        instance.transform = instance.restRoot * setup.normalization;
        instance.restTransform = instance.transform;
        if (z.dead) {
            const auto* pose = replica.poses.sample(scene.zombies.assets[setup.asset], now);
            // Roots and palettes share one playback time. Joint-world rigid
            // motion is interpolated while fixed affine bind geometry survives.
            if (!pose) instance.removed = true;
            else {
                instance.transform = pose->transform; instance.restRoot = pose->root;
                if (replica.appliedPoseRevision != replica.poses.revision()) {
                    instance.severedJoint = pose->severedJoint;
                    scene.zombies.setRagdollPalette(id, pose->bones);
                    replica.appliedPoseRevision = replica.poses.revision();
                }
            }
        }
        scene.zombies.markRenderChanged(id);
    }
}
} // namespace multiplayer
