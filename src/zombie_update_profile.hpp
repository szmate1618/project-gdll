#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ostream>

#if defined(__x86_64__) && defined(__GNUC__)
#include <cpuid.h>
#include <x86intrin.h>
#endif

namespace viewer::zombie_profile {
// Opt-in, simulation-thread wall timing. Only a Capture enables clocks; cold
// loading and queries outside the captured zombie update are excluded.
enum class Stage : std::size_t {
    ground, contacts, coverage, steering, spatial, contactCandidates,
    contactTriangles, contactTrunks, contactDoors, contactProps,
    groundStatic, groundProps, count
};
inline constexpr std::array<const char*, std::size_t(Stage::count)> names{{
    "ground queries", "contact queries", "navmesh coverage", "Detour steering", "spatial updates",
    "contact candidates", "contact triangles", "contact trunks", "contact doors", "contact props",
    "ground static", "ground props"
}};
struct Measurement { double seconds = 0; std::uint64_t calls = 0, dropped = 0; };
struct Profile {
    std::array<Measurement, std::size_t(Stage::count)> stages{};
    double seconds = 0;
    std::uint64_t steps = 0;
    std::uint64_t doorCandidates = 0, doorOverlaps = 0;

    void print(std::ostream& out) const {
        if (!steps) return;
        const double scale = 1000.0 / steps;
        out << "Zombie update breakdown: " << seconds * scale << " ms/step (" << steps << " steps)\n";
        double measured = 0;
        for (std::size_t i = 0; i < stages.size(); ++i) {
            measured += stages[i].seconds;
            out << "  " << names[i] << ": " << stages[i].seconds * scale << " ms; "
                << double(stages[i].calls) / steps << " calls/step; "
                << stages[i].dropped << " migrated samples omitted\n";
        }
        out << "  door detail: " << double(doorCandidates)/steps << " collider candidates, "
            << double(doorOverlaps)/steps << " overlaps/step\n";
        out << "  other update work + timer overhead: " << (seconds - measured) * scale << " ms\n";
    }
};
using Clock = std::chrono::steady_clock;
namespace detail {
// Fine-grained wall-clock reads can dominate tiny queries. Use a calibrated
// invariant TSC when available. Calibration must stay on one CPU; query scopes
// that migrate are omitted and counted. Other platforms retain
// steady_clock. Calibration is cold and independent of CPU turbo frequency.
inline bool hardwareCounter = false;
inline double secondsPerTick = double(Clock::period::num) / Clock::period::den;
inline std::uint64_t ticks(unsigned& cpu) {
#if defined(__x86_64__) && defined(__GNUC__)
    if (hardwareCounter) {
        _mm_lfence();
        const auto value = __rdtscp(&cpu);
        _mm_lfence();
        return value;
    }
#endif
    cpu = 0;
    return std::uint64_t(Clock::now().time_since_epoch().count());
}
} // namespace detail
inline void initializeClock() {
    static const bool initialized = [] {
#if defined(__x86_64__) && defined(__GNUC__)
        unsigned a, b, c, d;
        const bool rdtscp = __get_cpuid(0x80000001, &a, &b, &c, &d) && (d & (1u << 27));
        const bool invariant = __get_cpuid(0x80000007, &a, &b, &c, &d) && (d & (1u << 8));
        if (rdtscp && invariant) {
            detail::hardwareCounter = true;
            bool calibrated = false;
            for (int attempt = 0; attempt < 5 && !calibrated; ++attempt) {
                unsigned firstCpu, lastCpu;
                const auto start = Clock::now();
                const auto first = detail::ticks(firstCpu);
                while (std::chrono::duration<double>(Clock::now() - start).count() < .025) {}
                const auto last = detail::ticks(lastCpu);
                const double elapsed = std::chrono::duration<double>(Clock::now() - start).count();
                if (firstCpu == lastCpu && last > first) {
                    detail::secondsPerTick = elapsed / double(last - first);
                    calibrated = true;
                }
            }
            if (!calibrated) detail::hardwareCounter = false;
        }
#endif
        return true;
    }();
    (void)initialized;
}
inline const char* timerName() {
    initializeClock();
    return detail::hardwareCounter ? "calibrated invariant TSC" : "steady_clock";
}
class Timer;
inline thread_local Profile* activeProfile = nullptr;
inline thread_local Timer* activeTimer = nullptr;

// Capture exactly one updateBehavior call. A null target performs no timing.
class Capture {
public:
    explicit Capture(Profile* profile) : profile_(profile), previous_(activeProfile), parent_(activeTimer) {
        activeProfile = profile;
        activeTimer = nullptr;
        if (profile_) { initializeClock(); start_ = Clock::now(); }
    }
    ~Capture() {
        if (profile_) {
            profile_->seconds += std::chrono::duration<double>(Clock::now() - start_).count();
            ++profile_->steps;
        }
        activeProfile = previous_;
        activeTimer = parent_;
    }
    Capture(const Capture&) = delete;
    Capture& operator=(const Capture&) = delete;
private:
    Profile* profile_;
    Profile* previous_;
    Timer* parent_;
    Clock::time_point start_{};
};

// Exclusive stage time: nested measured work belongs to its own stage, so the
// reported stages can be added without double counting.
class Timer {
public:
    explicit Timer(Stage stage) : profile_(activeProfile), stage_(stage) {
        if (!profile_) return;
        parent_ = activeTimer;
        activeTimer = this;
        start_ = detail::ticks(cpu_);
    }
    ~Timer() {
        if (!profile_) return;
        unsigned cpu;
        const auto end = detail::ticks(cpu);
        auto& measurement = profile_->stages[std::size_t(stage_)];
        ++measurement.calls;
        if (cpu == cpu_ && end >= start_) {
            const double elapsed = double(end - start_) * detail::secondsPerTick;
            measurement.seconds += elapsed - children_;
            if (parent_) parent_->children_ += elapsed;
        } else ++measurement.dropped;
        activeTimer = parent_;
    }
    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;
private:
    Profile* profile_;
    Stage stage_;
    Timer* parent_ = nullptr;
    std::uint64_t start_ = 0;
    unsigned cpu_ = 0;
    double children_ = 0;
};
} // namespace viewer::zombie_profile
