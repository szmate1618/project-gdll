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
        if (pose.severedJoint != latest.pose.severedJoint || pose.sleeping ||
            pose.tick == latest.pose.tick || pose.tick - latest.pose.tick > 36) samples_.clear();
    }
    if (samples_.size() == 2) samples_.erase(samples_.begin());
    samples_.push_back({pose, receivedSeconds, std::min(transitSeconds, .5)});
}

const CorpsePose* RagdollPlayback::sample(const viewer::AnimatedModel& asset, double now) {
    if (samples_.empty()) return nullptr;
    if (!std::isfinite(now)) throw std::invalid_argument("Invalid ragdoll sample time");
    const auto& latest = samples_.back();
    const double age = samples_.size() == 2 && !latest.pose.sleeping ?
        std::clamp(now - latest.seconds + latest.transit, 0.0, .4) : 0;
    if (age == renderSeconds_ && sampledVersion_ == latest.pose.version)
        return rendered_ ? &*rendered_ : &latest.pose;
    auto next = samples_.size() == 2 ?
        predictRagdollPose(samples_.front().pose, latest.pose, asset, age) : latest.pose;
    const auto& previous = rendered_ ? *rendered_ : latest.pose;
    if (sampledVersion_ != latest.pose.version || next.transform != previous.transform ||
        next.root != previous.root || next.bones != previous.bones) ++revision_;
    if (next.transform == latest.pose.transform && next.root == latest.pose.root && next.bones == latest.pose.bones)
        rendered_.reset();
    else rendered_ = std::move(next);
    renderSeconds_ = age;
    sampledVersion_ = latest.pose.version;
    return rendered_ ? &*rendered_ : &latest.pose;
}

void RagdollPlayback::resetHistory() {
    if (samples_.size() > 1) samples_.erase(samples_.begin(), samples_.end() - 1);
    rendered_.reset();
    renderSeconds_ = -1;
}
} // namespace multiplayer
