#include "ragdoll_playback.hpp"
#include "ragdoll_interpolation.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace multiplayer {
void RagdollPlayback::push(const CorpsePose& pose, double receivedSeconds, double transitSeconds) {
    if (!std::isfinite(receivedSeconds) || receivedSeconds < 0 ||
        !std::isfinite(transitSeconds) || transitSeconds < 0)
        throw std::invalid_argument("Invalid ragdoll receive time");
    if (!samples_.empty()) {
        const auto& latest = samples_.back();
        if (pose.version <= latest.pose.version || pose.tick < latest.pose.tick) return;
        if (receivedSeconds < latest.seconds)
            throw std::invalid_argument("Ragdoll receive time moved backwards");
        if (pose.activeAttackRevision != latest.pose.activeAttackRevision ||
            pose.severedJoint != latest.pose.severedJoint || pose.sleeping ||
            pose.id != latest.pose.id || pose.asset != latest.pose.asset || latest.pose.sleeping ||
            pose.tick == latest.pose.tick || pose.tick - latest.pose.tick > 36 ||
            glm::length(glm::dvec3(pose.transform[3] - latest.pose.transform[3])) >
                20 * double(pose.tick - latest.pose.tick) / 60) {
            samples_.clear(); previousSamples_.clear();
            correctionFrom_.reset(); correctionTo_.reset(); correctionSeconds_ = -1;
        }
    }
    // Retain the last presented trajectory across a batch of incoming poses.
    if (!samples_.empty() && previousSamples_.empty() && sampledVersion_)
        previousSamples_ = samples_;
    if (samples_.size() == 2) samples_.erase(samples_.begin());
    samples_.push_back({pose, receivedSeconds, std::min(transitSeconds, .5)});
}

CorpsePose RagdollPlayback::predict(const std::vector<Sample>& samples, const viewer::AnimatedModel& asset, double now) {
    const auto& latest = samples.back();
    const double age = samples.size() == 2 && !latest.pose.sleeping ?
        std::clamp(now - latest.seconds + latest.transit, 0.0, latest.pose.activeAttackRevision ? .1 : .4) : 0;
    return samples.size() == 2 ? predictRagdollPose(samples.front().pose, latest.pose, asset, age) : latest.pose;
}

float RagdollPlayback::correctionWeight(double now) const {
    return correctionSeconds_ < 0 ? 0.f : float(std::clamp(1 - (now - correctionSeconds_) / .05, 0.0, 1.0));
}

const CorpsePose* RagdollPlayback::sample(const viewer::AnimatedModel& asset, double now) {
    if (samples_.empty()) return nullptr;
    if (!std::isfinite(now)) throw std::invalid_argument("Invalid ragdoll sample time");
    const auto& latest = samples_.back();
    if (!previousSamples_.empty()) {
        const double received = latest.seconds;
        auto previous = predict(previousSamples_, asset, received);
        if (correctionFrom_ && correctionTo_)
            previous = correctRagdollPose(*correctionFrom_, *correctionTo_, previous, asset, correctionWeight(received));
        correctionFrom_ = std::move(previous);
        correctionTo_ = predict(samples_, asset, received);
        correctionSeconds_ = received;
        previousSamples_.clear();
    }
    const double age = samples_.size() == 2 && !latest.pose.sleeping ?
        std::clamp(now - latest.seconds + latest.transit, 0.0, latest.pose.activeAttackRevision ? .1 : .4) : 0;
    const float weight = correctionWeight(now);
    if (age == renderSeconds_ && sampledVersion_ == latest.pose.version && weight == renderedCorrectionWeight_)
        return rendered_ ? &*rendered_ : &latest.pose;
    auto next = predict(samples_, asset, now);
    if (correctionFrom_ && correctionTo_)
        next = correctRagdollPose(*correctionFrom_, *correctionTo_, next, asset, weight);
    if (weight == 0) { correctionFrom_.reset(); correctionTo_.reset(); }
    const auto& previous = rendered_ ? *rendered_ : latest.pose;
    if (sampledVersion_ != latest.pose.version || next.transform != previous.transform ||
        next.root != previous.root || next.bones != previous.bones) ++revision_;
    if (next.transform == latest.pose.transform && next.root == latest.pose.root && next.bones == latest.pose.bones)
        rendered_.reset();
    else rendered_ = std::move(next);
    renderSeconds_ = age;
    renderedCorrectionWeight_ = weight;
    sampledVersion_ = latest.pose.version;
    return rendered_ ? &*rendered_ : &latest.pose;
}

void RagdollPlayback::resetHistory() {
    if (samples_.size() > 1) samples_.erase(samples_.begin(), samples_.end() - 1);
    rendered_.reset();
    previousSamples_.clear();
    correctionFrom_.reset(); correctionTo_.reset(); correctionSeconds_ = -1;
    renderSeconds_ = -1;
}
} // namespace multiplayer
