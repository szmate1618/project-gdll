#include "sleep_plan.hpp"
#include "noise.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <utility>

namespace viewer {
bool validSleepDuration(std::uint16_t minutes) {
    return minutes >= minimumSleepMinutes && minutes <= maximumSleepMinutes && minutes % 30 == 0;
}
std::string sleepDurationLabel(std::uint16_t minutes) {
    std::ostringstream text;
    text << std::setfill('0') << std::setw(2) << minutes / 60 << ':' << std::setw(2) << minutes % 60;
    return text.str();
}
GameCalendarTime gameCalendarTime(const DayNightCycle& cycle, double seconds, int addMinutes) {
    cycle.hourAt(seconds); // Validate using the same contract as the world clock.
    const double minutes = cycle.startHour * 60 + (cycle.dayLengthSeconds > 0 ? seconds / cycle.dayLengthSeconds * 1440 : 0)
        + addMinutes;
    const auto wholeMinutes = static_cast<std::uint64_t>(std::floor(std::max(0.0, minutes) + 1e-7));
    return {1 + wholeMinutes / 1440, sleepDurationLabel(static_cast<std::uint16_t>(wholeMinutes % 1440))};
}
SleepResponse SleepPlanState::response(std::uint32_t player) const {
    const auto found = std::find_if(participants.begin(), participants.end(),
        [player](const auto& member) { return member.player == player; });
    return found == participants.end() ? SleepResponse::Pending : found->response;
}
bool SleepPlanState::ready() const {
    return proposer && std::none_of(participants.begin(), participants.end(),
        [](const auto& member) { return member.response == SleepResponse::Pending; }) &&
        std::any_of(participants.begin(), participants.end(),
        [](const auto& member) { return member.response == SleepResponse::Accept; });
}
int SleepPlanState::rate() const {
    return sleeping ? sleepAcceleration : 1;
}
const char* sleepGuardDirection(std::uint8_t bearing) {
    constexpr std::array<const char*, 16> names{{"north", "north-northeast", "northeast", "east-northeast",
        "east", "east-southeast", "southeast", "south-southeast", "south", "south-southwest", "southwest",
        "west-southwest", "west", "west-northwest", "northwest", "north-northwest"}};
    return names.at(bearing);
}
bool SleepPlan::begin(double now, const DayNightCycle& cycle) {
    if (state_.sleeping || !state_.ready() || cycle.dayLengthSeconds <= 0) return false;
    cycle.hourAt(now);
    state_.sleeping = true;
    state_.startedAt = now;
    state_.endsAt = now + cycle.dayLengthSeconds * state_.minutes / 1440;
    state_.wakeReason = SleepWakeReason::None;
    state_.guardAlert = {};
    return true;
}
void SleepPlan::update(double now) {
    if (state_.sleeping && now >= state_.endsAt) wake(SleepWakeReason::Rested, now);
}
void SleepPlan::wake(SleepWakeReason reason, double now) {
    if (!state_.sleeping) return;
    state_.sleeping = false;
    state_.wakeReason = reason;
    state_.guardAlert = {};
    state_.endedAt = now;
    cancel();
}
void SleepPlan::guardSpotted(SleepGuardAlert alert, double now) {
    if (!state_.guarding(alert.guard)) return;
    wake(SleepWakeReason::GuardSpotted, now);
    state_.guardAlert = std::move(alert);
}
bool SleepPlan::hearNoise(const NoiseEvent& noise, glm::vec3 ears, double now) {
    if (!state_.sleeping || noiseLevelAt(noise, ears) <= sleepNoiseWakeThresholdDecibels) return false;
    wake(SleepWakeReason::Noise, now);
    return true;
}
void SleepPlan::join(std::uint32_t player, double now) {
    if (!player || std::any_of(state_.participants.begin(), state_.participants.end(),
        [player](const auto& member) { return member.player == player; })) return;
    if (state_.sleeping) wake(SleepWakeReason::PlayerJoined, now);
    state_.participants.push_back({player, SleepResponse::Pending});
}
void SleepPlan::cancel() {
    ++state_.revision;
    state_.proposer = 0;
    for (auto& member : state_.participants) member.response = SleepResponse::Pending;
}
void SleepPlan::leave(std::uint32_t player, double now) {
    if (state_.sleeping) wake(SleepWakeReason::PlayerLeft, now);
    state_.participants.erase(std::remove_if(state_.participants.begin(), state_.participants.end(),
        [player](const auto& member) { return member.player == player; }), state_.participants.end());
    if (state_.proposer == player) cancel();
}
bool SleepPlan::apply(std::uint32_t player, const SleepAction& action, double now) {
    const auto found = std::find_if(state_.participants.begin(), state_.participants.end(),
        [player](const auto& member) { return member.player == player; });
    if (found == state_.participants.end() || action.revision != state_.revision) return false;
    if (state_.sleeping) {
        if (action.kind == SleepActionKind::Accept) { found->response = SleepResponse::Accept; return true; }
        if (action.kind == SleepActionKind::Guard) {
            found->response = SleepResponse::Guard;
            if (!state_.ready()) wake(SleepWakeReason::Cancelled, now);
            return true;
        }
        if (action.kind == SleepActionKind::Cancel) { wake(SleepWakeReason::Cancelled, now); return true; }
        return false;
    }
    if (action.kind == SleepActionKind::Suggest) {
        if (!validSleepDuration(action.minutes)) return false;
        ++state_.revision;
        state_.proposer = player;
        state_.minutes = action.minutes;
        state_.wakeReason = SleepWakeReason::None;
        state_.guardAlert = {};
        for (auto& member : state_.participants) member.response = SleepResponse::Pending;
        found->response = SleepResponse::Accept;
        return true;
    }
    if (!state_.proposer) return false;
    switch (action.kind) {
    case SleepActionKind::Accept: found->response = SleepResponse::Accept; return true;
    case SleepActionKind::Guard: found->response = SleepResponse::Guard; return true;
    case SleepActionKind::Cancel:
        if (player == state_.proposer) cancel();
        else found->response = SleepResponse::Pending;
        return true;
    default: return false;
    }
}
} // namespace viewer
