#pragma once

#include "fixed.hpp"
#include <cstddef>
#include <vector>

namespace viewer::boid {
constexpr std::uint32_t simulationVersion = 1;
constexpr std::size_t maxFlocks = 64, maxBirds = 65536;
enum class Mode : std::uint8_t { wandering, gathering, swooping, recovering };
struct Control {
    Vec3 target{}, aim{}, exit{}, attackDirection{0,0,1000};
    Mode mode = Mode::wandering;
    std::uint32_t remainingTicks = 0, pass = 0;
};
struct Flock {
    std::uint32_t firstBird = 0, birdCount = 0;
    Vec3 center{}, velocity{};
    Control control;
};
struct Bird {
    Vec3 position{}, velocity{}, positionRemainder{}, velocityRemainder{};
    bool passedPlayer = false;
};
struct Checkpoint {
    std::uint32_t version = simulationVersion, seed = 0;
    std::uint64_t tick = 0;
    std::vector<Bird> birds;
    std::vector<Flock> flocks;
};
struct Command {
    std::uint64_t tick = 0;
    std::uint32_t flock = 0;
    Control control;
};
// A reliable ordered stream publishes commands and the tick through which
// they are complete. Checkpoints are only for joining/recovering clients.
struct Frame {
    std::uint64_t tick = 0, checksum = 0;
    bool hasCheckpoint = false;
    Checkpoint checkpoint;
    std::vector<Command> commands;
};
void validate(const Control& control);
void validate(const Checkpoint& checkpoint);
class Simulation {
public:
    explicit Simulation(Checkpoint checkpoint = {});
    void restore(Checkpoint checkpoint);
    void step(const std::vector<Command>& commands = {});
    const Checkpoint& state() const { return state_; }
    std::uint64_t checksum() const;
private:
    Checkpoint state_;
    std::vector<Vec3> offsets_;
};
} // namespace viewer::boid
