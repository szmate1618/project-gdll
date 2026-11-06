#pragma once

#include "day_night_clock.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace viewer {
constexpr std::uint16_t minimumSleepMinutes = 30, maximumSleepMinutes = 720;
constexpr float sleepWakeDistanceMeters = 60;
bool validSleepDuration(std::uint16_t minutes);
std::string sleepDurationLabel(std::uint16_t minutes);

// The world has no authored calendar epoch. Day 1 starts at the session's
// configured start hour; subsequent days begin at midnight, not session age.
struct GameCalendarTime {
    std::uint64_t day = 1;
    std::string clock;
};
GameCalendarTime gameCalendarTime(const DayNightCycle& cycle, double seconds, int addMinutes = 0);

enum class SleepResponse : std::uint8_t { Pending, Accept, Guard };
enum class SleepActionKind : std::uint8_t { Suggest, Accept, Guard, Cancel };
struct SleepAction {
    SleepActionKind kind = SleepActionKind::Suggest;
    std::uint64_t revision = 0; // Respond only to the proposal actually seen.
    std::uint16_t minutes = minimumSleepMinutes;
};
struct SleepParticipant {
    std::uint32_t player = 0;
    SleepResponse response = SleepResponse::Pending;
};
struct SleepPlanState {
    std::uint64_t revision = 0;
    std::uint32_t proposer = 0; // Zero means no active proposal.
    std::uint16_t minutes = minimumSleepMinutes;
    std::vector<SleepParticipant> participants;
    SleepResponse response(std::uint32_t player) const;
    bool ready() const; // Everyone answered, with at least one accepting sleeper.
};

// Shared planning only. The authority owns membership and applies deduplicated
// actions; this does not advance time or put players to sleep.
class SleepPlan {
public:
    const SleepPlanState& state() const { return state_; }
    void join(std::uint32_t player);
    void leave(std::uint32_t player);
    bool apply(std::uint32_t player, const SleepAction& action);
private:
    void cancel();
    SleepPlanState state_;
};
} // namespace viewer
