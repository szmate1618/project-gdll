#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ostream>

namespace viewer::frame_stage_profile {
// Coarse, opt-in inclusive CPU wall timings. Nested stages overlap: do not
// add them together. Captures never change scheduling or wait for the GPU.
enum class Stage : std::size_t {
    weapons, physics, wounds, synchronization, blood, audio,
    zombiePreparation, zombieQuery, zombieRefit, zombieTraversal, zombieSort,
    zombieSelection, zombiePalettes, zombieErrorCheck,
    shadows, town, townSort, vegetation, zombieDraws, props, bloodDraws, overlay, hud, count
};
inline constexpr std::array<const char*, std::size_t(Stage::count)> names{{
    "weapons", "rigid-body physics", "wound preparation", "zombie physics sync", "blood effects", "audio",
    "zombie visibility/pose preparation", "zombie visibility query", "zombie visibility bounds refitting",
    "zombie visibility traversal/tests", "zombie visibility sorting",
    "zombie LOD selection", "zombie palette uploads", "zombie GL error check",
    "shadow rendering", "town camera pass", "town opaque source-ID sorting", "vegetation camera pass",
    "zombie draws (all passes)", "props/vehicles camera pass", "blood rendering", "scene/weapon overlays", "HUD"
}};
struct Measurement { double seconds = 0; std::uint64_t calls = 0; };
struct Profile {
    std::array<Measurement, std::size_t(Stage::count)> stages{};
    void print(std::ostream& out, std::size_t frames) const {
        if (!frames) return;
        out << "CPU frame details (inclusive ms/frame; calls/frame):\n";
        for (std::size_t i = 0; i < stages.size(); ++i)
            out << "  " << names[i] << ": " << stages[i].seconds * 1000 / frames
                << "; " << double(stages[i].calls) / frames << '\n';
    }
};
inline thread_local Profile* activeProfile = nullptr;
class Capture {
public:
    explicit Capture(Profile* profile) : previous_(activeProfile) { activeProfile = profile; }
    ~Capture() { activeProfile = previous_; }
    Capture(const Capture&) = delete;
    Capture& operator=(const Capture&) = delete;
private:
    Profile* previous_;
};
class Timer {
public:
    explicit Timer(Stage stage) : profile_(activeProfile), stage_(stage) {
        if (profile_) start_ = Clock::now();
    }
    ~Timer() {
        if (!profile_) return;
        auto& sample = profile_->stages[std::size_t(stage_)];
        sample.seconds += std::chrono::duration<double>(Clock::now() - start_).count();
        ++sample.calls;
    }
    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;
private:
    using Clock = std::chrono::steady_clock;
    Profile* profile_;
    Stage stage_;
    Clock::time_point start_{};
};
} // namespace viewer::frame_stage_profile
