#include "client/zombie_prediction.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace multiplayer;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void near(double value, double expected, const char* message) {
    require(std::abs(value - expected) < .0001, message);
}
ZombieState moving(float x, float walkSeconds, std::uint8_t period = 3, float degrees = 0) {
    ZombieState state;
    state.root = glm::rotate(glm::mat4(1), glm::radians(degrees), {0, 1, 0});
    state.root[3] = {x, 0, 0, 1};
    state.walking = true;
    state.walkSeconds = walkSeconds;
    state.updatePeriodTicks = period;
    return state;
}

void immediateSamplesAndSteadyMotion() {
    ZombiePrediction prediction;
    require(prediction.empty(), "New prediction retained motion history");
    prediction.push(60, moving(0, 1));
    near(prediction.sample(60, 0).root[3].x, 0, "Initial pose was not displayed immediately");
    prediction.push(63, moving(.15f, 1.025f));
    const auto latest = prediction.sample(63, .05);
    near(latest.root[3].x, .15, "Newest motion waited in an interpolation buffer");
    near(latest.walkSeconds, 1.025, "Newest clip time waited in an interpolation buffer");
    auto next = prediction.sample(64.5, .075);
    near(next.root[3].x, .225, "Constant speed did not advance between packets");
    near(next.walkSeconds, 1.0375, "Partial-speed travel did not advance its measured clip rate");
    prediction.push(66, moving(.30f, 1.05f));
    next = prediction.sample(66, .1);
    near(next.root[3].x, .30, "Regular motion started a permanent correction lag");
    near(next.walkSeconds, 1.05, "Regular clip updates started a permanent correction lag");
    const auto transit = prediction.sample(69, .15);
    near(transit.root[3].x, .45, "Estimated inbound transit was not covered by prediction");
    near(transit.idleSeconds, 1.15, "Idle clock used a buffered timeline");
}

void cadenceAndStarvation() {
    for (const auto period : {std::uint8_t(3), std::uint8_t(12), std::uint8_t(60)}) {
        ZombiePrediction prediction;
        prediction.push(60, moving(0, 1, period));
        prediction.push(60 + period, moving(float(period) / 30, 1 + float(period) / 60, period));
        const double latest = 60 + period;
        const auto half = prediction.sample(latest + double(period) / 2, 2);
        near(half.root[3].x, double(period) / 20, "Slow cadence did not predict between updates");
        const auto held = prediction.sample(latest + 600, 20);
        const double cap = std::max(.1, double(period) / 60);
        near(held.root[3].x, double(period) / 30 + 2 * cap, "Starved motion exceeded its bounded horizon");
        near(held.walkSeconds, 1 + double(period) / 60 + cap, "Starved clip clock drifted indefinitely");
        const auto later = prediction.sample(latest + 1200, 30);
        require(held.root == later.root && held.walkSeconds == later.walkSeconds && held.idleSeconds == later.idleSeconds,
            "Held prediction changed after its horizon expired");
    }
}

void smallCorrectionsSettle() {
    ZombiePrediction prediction;
    prediction.push(60, moving(0, 0));
    prediction.push(63, moving(.15f, .05f));
    prediction.sample(66, .1);
    prediction.push(66, moving(.28f, .1f));
    const auto corrected = prediction.sample(66, .1);
    near(corrected.root[3].x, .30, "Small correction snapped the displayed trajectory");
    require(prediction.sample(66, .1).root == corrected.root,
        "Sampling twice in one frame restarted correction");
    const auto settled = prediction.sample(69, .15);
    near(settled.root[3].x, .41, "Correction did not settle to the new trajectory within fifty milliseconds");
}

void coalescedUpdatesAndCadencePromotion() {
    ZombiePrediction prediction;
    prediction.push(60, moving(0, 0));
    prediction.push(63, moving(.15f, .05f));
    prediction.sample(66, .1);
    prediction.push(66, moving(.28f, .1f));
    prediction.sample(66, .1);
    near(prediction.sample(67.2, .12).root[3].x, .344,
        "Correction did not continue the displayed trajectory between updates");
    prediction.push(69, moving(.40f, .15f));
    prediction.push(72, moving(.50f, .20f));
    const auto coalesced = prediction.sample(72, .2);
    near(coalesced.root[3].x, .54,
        "Coalesced updates corrected from an intermediate undisplayed trajectory");
    require(prediction.sample(72, .2).root == coalesced.root,
        "Duplicate frame sampling restarted a coalesced correction");
    near(prediction.sample(75, .25).root[3].x, .60,
        "Coalesced correction failed to settle to the newest sample");

    prediction.reset();
    prediction.push(60, moving(0, 0, 60));
    prediction.push(120, moving(3, 1, 60));
    near(prediction.sample(150, 1.5).root[3].x, 4.5,
        "Slow actor did not cover its advertised update interval");
    prediction.push(153, moving(4.65f, 1.55f, 3));
    const auto promoted = prediction.sample(213, 2.55);
    near(promoted.root[3].x, 4.95,
        "Promoted actor retained its previous one-second prediction horizon");
    near(promoted.walkSeconds, 1.65,
        "Promoted actor's animation retained its old prediction horizon");
}

void turningAndDiscontinuities() {
    ZombiePrediction prediction;
    prediction.push(60, moving(0, 1, 3, 170));
    prediction.push(63, moving(.1f, 1.05f, 3, -181));
    const auto turn = prediction.sample(66, .1);
    near(glm::length(glm::vec3(turn.root[0])), 1, "Predicted turn changed root scale");
    near(glm::determinant(glm::mat3(turn.root)), 1, "Predicted turn collapsed root volume");
    const auto forward = glm::vec3(turn.root[2]);
    near(forward.x, std::sin(glm::radians(-172.f)), "Predicted turn did not cross the yaw wrap by the shortest arc");
    auto stopped = moving(.2f, 1.1f);
    stopped.walking = false;
    prediction.push(66, stopped);
    const auto stop = prediction.sample(69, .15);
    near(stop.root[3].x, .2, "Stop retained the previous travel velocity");
    near(stop.walkSeconds, 1.1, "Stop retained the previous locomotion clock rate");
    auto run = moving(.4f, 2);
    run.running = true;
    prediction.push(69, run);
    near(prediction.sample(72, .2).root[3].x, .4, "Clip change extrapolated the preceding mode's velocity");
    prediction.push(72, moving(100, 2.05f));
    near(prediction.sample(75, .25).root[3].x, 100, "Teleport produced an unbounded inferred velocity");
    prediction.push(132, moving(101, 3));
    near(prediction.sample(135, 1.25).root[3].x, 101, "Long gap inferred motion across missing history");
    auto dead = moving(102, 4);
    dead.dead = true;
    prediction.push(135, dead);
    const auto corpse = prediction.sample(140, 1.4);
    near(corpse.root[3].x, 102, "Death retained living motion prediction");
    near(corpse.walkSeconds, 4, "Death retained living animation prediction");
}

void staleTicksAndReset() {
    ZombiePrediction prediction;
    prediction.push(63, moving(1, 1));
    prediction.push(60, moving(100, 10));
    prediction.push(63, moving(100, 10));
    require(prediction.latestTick() == 63 && prediction.latest().root[3].x == 1,
        "Stale motion replaced a newer authoritative sample");
    prediction.reset();
    require(prediction.empty(), "Reset retained motion or correction state");
    prediction.push(3, moving(7, 0));
    near(prediction.sample(6, 2).root[3].x, 7, "Re-entry extrapolated motion from a retired actor");
}
} // namespace

int main() {
    try {
        immediateSamplesAndSteadyMotion(); cadenceAndStarvation(); smallCorrectionsSettle();
        coalescedUpdatesAndCadencePromotion();
        turningAndDiscontinuities(); staleTicksAndReset();
        std::cout << "Bufferless zombie motion, animation prediction, corrections and starvation passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
