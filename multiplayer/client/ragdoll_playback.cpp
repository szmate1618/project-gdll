#include "ragdoll_playback.hpp"
#include "ragdoll_interpolation.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace multiplayer {
namespace {
constexpr double minimumDelay = .1;
constexpr double maximumDelay = .5;
constexpr std::size_t historySize = 4;
}

void RagdollPlayback::push(const CorpsePose& pose, double receivedSeconds) {
    if (!std::isfinite(receivedSeconds) || receivedSeconds < 0)
        throw std::invalid_argument("Invalid ragdoll receive time");
    if (!samples_.empty()) {
        const auto& latest = samples_.back();
        if (pose.version <= latest.pose.version) return;
        if (receivedSeconds < latest.seconds)
            throw std::invalid_argument("Ragdoll receive time moved backwards");
        const double interval = receivedSeconds - latest.seconds;
        if (pose.severedJoint != latest.pose.severedJoint) {
            // Mesh separation and its complete palette become visible together.
            samples_.clear();
            rendered_.reset();
            renderSeconds_ = -1;
        } else if (interval > maximumDelay) {
            // A settled body may wake seconds later. Ease into the new pose over
            // a short window instead of treating that idle time as moving time.
            auto previous = latest.pose;
            samples_.clear();
            samples_.push_back({std::move(previous), receivedSeconds - minimumDelay});
            renderSeconds_ = receivedSeconds - minimumDelay;
            delay_ = minimumDelay;
        } else {
            delay_ = std::clamp(interval, minimumDelay, maximumDelay);
        }
        if (!samples_.empty() && receivedSeconds == samples_.back().seconds)
            samples_.pop_back(); // Coalesce poses received in the same poll.
    }
    if (samples_.size() == historySize) samples_.erase(samples_.begin());
    samples_.push_back({pose, receivedSeconds});
}

const CorpsePose* RagdollPlayback::sample(const viewer::AnimatedModel& asset, double now) {
    if (samples_.empty()) return nullptr;
    const double seconds = std::max(renderSeconds_, std::clamp(now - delay_,
        samples_.front().seconds, samples_.back().seconds));
    if (seconds == renderSeconds_ && sampledVersion_ == samples_.back().pose.version)
        return rendered_ ? &*rendered_ : &samples_.back().pose;
    if (seconds == samples_.back().seconds) {
        // Settled corpses retain one complete pose, without four old palettes
        // and an additional copy of the displayed endpoint in persistent caches.
        if (samples_.size() > 1) samples_.erase(samples_.begin(), samples_.end() - 1);
        rendered_.reset();
        renderSeconds_ = seconds;
        sampledVersion_ = samples_.back().pose.version;
        ++revision_;
        return &samples_.back().pose;
    }
    const Sample* before = &samples_.front();
    const Sample* after = before;
    for (std::size_t i = 1; i < samples_.size(); ++i) {
        after = &samples_[i];
        if (after->seconds >= seconds) break;
        before = after;
    }
    const float alpha = after->seconds == before->seconds ? 1.f : static_cast<float>(std::clamp(
        (seconds - before->seconds) / (after->seconds - before->seconds), 0.0, 1.0));
    rendered_ = interpolateRagdollPose(before->pose, after->pose, asset, alpha);
    renderSeconds_ = seconds;
    sampledVersion_ = samples_.back().pose.version;
    ++revision_;
    return &*rendered_;
}

void RagdollPlayback::resetHistory() {
    if (samples_.size() > 1) samples_.erase(samples_.begin(), samples_.end() - 1);
    rendered_.reset();
    renderSeconds_ = -1;
    delay_ = minimumDelay;
}
} // namespace multiplayer
