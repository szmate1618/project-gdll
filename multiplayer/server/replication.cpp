#include "replication.hpp"
#include "zombie_selection.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace multiplayer {
Replication::SetupState* Replication::findSetup(std::size_t id) {
    if (id >= setupSlots_.size() || !setupSlots_[id]) return nullptr;
    return &setups_[setupSlots_[id] - 1];
}

Snapshot Replication::snapshot(Scene& scene, glm::vec3 feet, Snapshot s,
        const std::optional<ReplicationView>& view) {
    poseTick_ = s.tick;
    const auto population = scene.zombies.instances.size();
    if (setupSlots_.size() < population) setupSlots_.resize(population);
    if (interested_.size() < population) interested_.resize(population);
    vehicles_.append(*scene.vehicles, s.tick, s.vehicles);
    for (const auto door : scene.doors->activeDoors()) {
        const auto offset = scene.doors->panel(door).center - feet;
        if (glm::dot(offset, offset) < 240.0f * 240 && s.doors.size() < maxDoors)
            s.doors.push_back({static_cast<std::uint32_t>(door), scene.doors->angle(door)});
    }
    for (std::size_t i = 0; i < scene.boxes->count(); ++i) {
        const auto p = scene.boxes->pose(i);
        s.boxes.push_back({static_cast<std::uint32_t>(i), p.center, p.halfExtents, p.rotation, !scene.boxes->active(i)});
    }
    const auto lighting = viewer::dayNightLighting(scene.dayNight.hourAt(double(s.tick) / 60));
    const std::optional<glm::vec3> sun = glm::any(glm::greaterThan(lighting.sun.color, glm::vec3(0))) ?
        std::optional<glm::vec3>(lighting.sun.direction) : std::nullopt;
    selectZombieInterest(scene.zombies, feet, view, interested_, selected_, sun);
    interestedCorpses_.clear();
    s.zombies.clear();
    s.woundContents.clear();
    // Reserve for the largest (float-position escape) ordinary record. The
    // complete player/door/box lists and pending vehicle updates share the budget.
    const auto baseBytes = encode(s).size();
    const auto available = maxMessageBytes - baseBytes;
    const auto sampleLimit = std::min(maxZombieUpdatesPerSnapshot, available / 26);
    const std::vector<std::uint8_t> emptyMask;
    auto* setupSlots = setupSlots_.data();
    auto* cachedSetups = setups_.data();
    for (std::size_t rank = 0; rank < selected_.size(); ++rank) {
        const auto id = selected_[rank];
        const auto& instance = scene.zombies.instances[id];
        if (instance.ragdoll) interestedCorpses_.push_back(id);
        auto& slot = setupSlots[id];
        const bool created = slot == 0;
        if (created) {
            setups_.emplace_back();
            slot = static_cast<std::uint32_t>(setups_.size());
            // Payload growth can relocate records; the separate ID table stays
            // fixed during selection. Borrow the record only after refreshing.
            cachedSetups = setups_.data();
        }
        auto& state = cachedSetups[slot - 1];
        const auto* wound = scene.zombies.wounds().find(id);
        const auto& mask = wound ? wound->vertexMask : emptyMask;
        if (created) {
            state.current.id = static_cast<std::uint32_t>(id);
            state.current.asset = static_cast<std::uint32_t>(instance.asset);
            state.current.version = 1;
            state.current.normalization = glm::inverse(instance.restRoot) * instance.restTransform;
            // These are affine placement matrices. Generic inversion can
            // leave a homogeneous-row rounding residue; do not publish it as
            // perspective or let it fail the wire's exact affine contract.
            for (int column = 0; column < 4; ++column) {
                const float expected = column == 3 ? 1.f : 0.f;
                if (instance.restRoot[column][3] != expected || instance.restTransform[column][3] != expected)
                    throw std::runtime_error("Zombie placement must be affine");
                state.current.normalization[column][3] = expected;
            }
            state.current.phase = instance.phase;
            state.current.woundMask = mask;
        } else {
            if (state.current.asset != instance.asset)
                throw std::runtime_error("Zombie asset changed during replication");
            if (state.current.woundMask != mask) {
                ++state.current.version;
                state.current.woundMask = mask;
            }
        }
        if (!state.queued && state.acknowledged < state.current.version) {
            pendingSetups_.push_back(id);
            state.pending = std::prev(pendingSetups_.end());
            state.queued = true;
        }
        if (!interested_[id]) state.sentMotion = false;
        if (!state.acknowledged || s.zombies.size() == sampleLimit) continue;
        const auto period = static_cast<std::uint8_t>(rank < 256 ? 3 : rank < 1024 ? 12 : 60);
        const auto mode = static_cast<std::uint8_t>(instance.walking | (instance.running << 1) | (instance.ragdoll << 2) | (instance.attackClip << 3));
        const bool urgent = !state.sentMotion || state.lastMode != mode || period < state.lastPeriod;
        const auto passes = std::uint64_t(period / 3);
        // Rank balances every cadence bucket even for adversarial stable IDs
        // (for example every selected ID divisible by twenty). Deadlines keep
        // cell/rank churn from repeatedly moving an actor out of its next slot.
        const bool scheduled = (s.tick / 3) % passes == rank % passes;
        const bool overdue = state.sentMotion && s.tick >= state.lastMotionTick &&
            s.tick - state.lastMotionTick >= period;
        if (state.sentMotion && state.lastMotionTick == s.tick) continue;
        if (!urgent && !scheduled && !overdue) continue;
        ZombieState sample;
        sample.id = static_cast<std::uint32_t>(id); sample.asset = state.current.asset;
        sample.transform = instance.transform; sample.root = instance.restRoot;
        sample.phase = instance.phase; sample.walkSeconds = static_cast<float>(instance.walkSeconds);
        sample.walking = instance.walking; sample.running = instance.running; sample.dead = instance.ragdoll;
        sample.attackClip = instance.ragdoll ? 0 : instance.attackClip;
        sample.attackSeconds = static_cast<float>(instance.attackSeconds);
        sample.updatePeriodTicks = period;
        s.zombies.push_back(std::move(sample));
        state.sentMotion = true; state.lastMotionTick = s.tick; state.lastMode = mode; state.lastPeriod = period;
    }
    for (const auto id : interestedIds_) interested_[id] = 0;
    for (const auto id : selected_) interested_[id] = 1;
    interestedIds_ = selected_;
    // Expired interest does not need setup delivery. Already acknowledged
    // metadata remains cached for re-entry, and active reliable sends still ack.
    for (auto it = pendingSetups_.begin(); it != pendingSetups_.end();) {
        if (interested_[*it]) { ++it; continue; }
        cachedSetups[setupSlots[*it] - 1].queued = false;
        it = pendingSetups_.erase(it);
    }
    std::sort(interestedCorpses_.begin(), interestedCorpses_.end());
    const auto contentsLimit = std::min<std::size_t>(256, (available - s.zombies.size() * 26) / 64);
    s.woundContents = scene.zombies.woundContents(selected_, contentsLimit);
    return s;
}

std::optional<ZombieSetups> Replication::nextSetups(const Scene&) const {
    constexpr std::size_t maxSetupTransfers = 128, maxSetupBytes = 16 * 1024;
    if (setupTransfers_ >= maxSetupTransfers) return std::nullopt;
    ZombieSetups batch;
    std::size_t bytes = 13;
    for (const auto id : pendingSetups_) {
        const auto& state = setups_[setupSlots_[id] - 1];
        if (state.inFlight || state.acknowledged >= state.current.version) continue;
        const auto cost = 88 + state.current.woundMask.size();
        if (bytes + cost > maxSetupBytes) break;
        batch.zombies.push_back(state.current); bytes += cost;
        if (batch.zombies.size() == maxZombieSetupsPerBatch ||
            batch.zombies.size() + setupTransfers_ == maxSetupTransfers) break;
    }
    if (batch.zombies.empty()) return std::nullopt;
    return batch;
}

void Replication::setupsSent(const ZombieSetups& batch) {
    for (const auto& setup : batch.zombies) {
        auto* cached = findSetup(setup.id);
        if (!cached) throw std::logic_error("Unknown zombie setup transfer");
        auto& state = *cached;
        if (state.inFlight || state.current.version != setup.version || setupTransfers_ >= 128)
            throw std::logic_error("Zombie setup transfer already active or full");
        state.inFlight = setup.version;
        ++setupTransfers_;
    }
}

void Replication::acknowledgeSetups(const ZombieSetupAck& ack) {
    for (const auto& receipt : ack.zombies) {
        auto* cached = findSetup(receipt.id);
        if (!cached) throw std::runtime_error("Unknown zombie setup acknowledgement");
        auto& state = *cached;
        if (receipt.version <= state.acknowledged) continue;
        if (!state.inFlight || receipt.version != state.inFlight)
            throw std::runtime_error("Client acknowledged an unsent zombie setup");
        state.acknowledged = receipt.version;
        state.inFlight = 0; --setupTransfers_;
        if (state.queued && state.acknowledged == state.current.version) {
            pendingSetups_.erase(state.pending);
            state.queued = false;
        }
    }
}

std::optional<CorpsePose> Replication::nextPose(const Scene& scene) const {
    if (inFlight_.size() >= maxPoseTransfers) return std::nullopt;
    // Round-robin prevents continuously moving nearby corpses starving late joins.
    const auto start = std::lower_bound(interestedCorpses_.begin(), interestedCorpses_.end(), nextPoseId_);
    const auto startOffset = static_cast<std::size_t>(start - interestedCorpses_.begin());
    for (std::size_t offset = 0; offset < interestedCorpses_.size(); ++offset) {
        const auto id = interestedCorpses_[(startOffset + offset) % interestedCorpses_.size()];
        if (inFlight_.count(id) || !setups_[setupSlots_[id] - 1].acknowledged) continue;
        const auto last = lastPoseTicks_.find(id);
        if (last != lastPoseTicks_.end() && poseTick_ - last->second < ragdollUpdateTicks) continue;
        const auto& instance = scene.zombies.instances.at(id);
        if (!instance.ragdoll || instance.removed) continue;
        const auto ack = acknowledged_.find(id);
        if (ack != acknowledged_.end() && ack->second >= instance.ragdollPoseVersion) continue;
        return CorpsePose{static_cast<std::uint32_t>(id), static_cast<std::uint32_t>(instance.asset),
            instance.ragdollPoseVersion, instance.transform, instance.restRoot, instance.ragdollBones,
            static_cast<std::int16_t>(instance.severedJoint), poseTick_, instance.ragdollSleeping};
    }
    return std::nullopt;
}
void Replication::sent(const CorpsePose& pose) {
    if (inFlight_.size() >= maxPoseTransfers || inFlight_.count(pose.id))
        throw std::logic_error("Corpse transfer already active or full");
    inFlight_.emplace(pose.id, pose.version);
    lastPoseTicks_[pose.id] = pose.tick;
    nextPoseId_ = std::size_t(pose.id) + 1;
}
void Replication::acknowledge(const PoseAck& ack) {
    const auto sent = inFlight_.find(ack.id);
    const auto previous = acknowledged_.find(ack.id);
    if (previous != acknowledged_.end() && ack.version <= previous->second) return;
    if (sent == inFlight_.end() || ack.version != sent->second)
        throw std::runtime_error("Client acknowledged an unsent corpse pose");
    acknowledged_[ack.id] = ack.version;
    inFlight_.erase(sent);
}
} // namespace multiplayer
