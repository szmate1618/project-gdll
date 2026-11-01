#include "client/zombie_presentation_clock.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using multiplayer::ZombiePresentationClock;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void near(double actual, double expected, const char* message) {
    require(std::abs(actual - expected) < 1e-8, message);
}

void steadyTimeAndTransit() {
    ZombiePresentationClock clock;
    clock.observe(6000, 10, .025);
    near(clock.tick(10), 6001.5, "First snapshot did not initialize the clock with incoming transit");
    for (int i = 1; i <= 40; ++i) {
        const double now = 10 + i * .05;
        clock.observe(6000 + i * 3, now, .025);
        near(clock.tick(now), 6001.5 + i * 3, "Regular packets introduced clock drift or a playback delay");
        near(clock.tick(now + .01), 6002.1 + i * 3, "Clock failed to advance between packets");
    }
}

void correctionsAreContinuousAndSettle(double transitChange) {
    ZombiePresentationClock clock;
    clock.observe(6000, 10, .1);
    const auto before = clock.tick(10.05);
    clock.observe(6003, 10.05, .1 + transitChange);
    near(clock.tick(10.05), before, "New timing observation jumped the displayed clock");
    double previous = before / 60;
    for (int frame = 1; frame <= 360; ++frame) {
        const double now = 10.05 + frame / 120.0;
        if (frame % 6 == 0)
            clock.observe(6003 + frame / 2, now, .1 + transitChange);
        const double current = clock.tick(now) / 60;
        const double step = current - previous;
        require(step >= .9 / 120 - 1e-10 && step <= 1.1 / 120 + 1e-10,
            "Timing correction rewound or jumped instead of gently adjusting clock speed");
        near(clock.tick(now) / 60, current, "Sampling twice advanced the clock at the same local time");
        previous = current;
    }
    require(std::abs(previous - (103.15 + transitChange)) < .0001,
        "Clock correction retained a permanent offset after stable updates resumed");
}

void burstArrivalsAndRenderCadence() {
    const auto run = [](int fps) {
        ZombiePresentationClock clock;
        clock.observe(60, 0, .04);
        const auto before = clock.tick(.1);
        clock.observe(63, .1, .04); // Delayed update, then newer updates in one receive batch.
        clock.observe(66, .1, .03);
        clock.observe(69, .1, .02);
        near(clock.tick(.1), before, "Coalesced observations advanced time within one frame");
        for (int frame = 1; frame <= fps; ++frame) clock.tick(.1 + double(frame) / fps);
        return clock.tick(1.1);
    };
    require(std::abs(run(30) - run(144)) < .001,
        "Clock smoothing materially depended on render frame rate");
}
} // namespace

int main() {
    try {
        steadyTimeAndTransit();
        correctionsAreContinuousAndSettle(-.08);
        correctionsAreContinuousAndSettle(.08);
        burstArrivalsAndRenderCadence();
        std::cout << "Continuous zombie clock, bounded corrections, transit and burst arrivals passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
