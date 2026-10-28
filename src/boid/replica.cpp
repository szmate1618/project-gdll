#include "replica.hpp"
#include <algorithm>
#include <cmath>

namespace viewer::boid {
bool Replica::receive(const Frame& frame) {
    // Duplicate/stale publication must not rewind a valid simulation.
    if (ready_ && frame.tick<=simulation_.state().tick) return true;
    if (frame.hasCheckpoint) {
        if (frame.checkpoint.tick!=frame.tick || !frame.commands.empty()) return false;
        simulation_.restore(frame.checkpoint); poses_.clear(); ready_=true;
    } else {
        if (!ready_ || frame.tick-simulation_.state().tick>600) { ready_=false; return false; }
        std::size_t next=0;
        while (simulation_.state().tick<frame.tick) {
            std::vector<Command> commands;
            while (next<frame.commands.size() && frame.commands[next].tick==simulation_.state().tick+1)
                commands.push_back(frame.commands[next++]);
            simulation_.step(commands);
        }
        if (next!=frame.commands.size()) { ready_=false; return false; }
    }
    if (simulation_.checksum()!=frame.checksum) { ready_=false; return false; }
    Pose pose; pose.tick=frame.tick; present(simulation_.state(),pose.birds);
    poses_.push_back(std::move(pose));
    // Two seconds of 20 Hz samples covers normal remote interpolation delay.
    while (poses_.size()>40) poses_.pop_front();
    return true;
}
const std::vector<FlockBird>& Replica::sample(double seconds) {
    if (!ready_ || poses_.empty() || !std::isfinite(seconds)) { displayed_.clear(); return displayed_; }
    const auto tick=seconds*ticksPerSecond;
    while (poses_.size()>2 && double(poses_[1].tick)<=tick) poses_.pop_front();
    const auto& a=poses_.front(); const auto& b=poses_.size()>1?poses_[1]:a;
    const auto alpha=a.tick==b.tick?0.f:float(std::clamp((tick-double(a.tick))/double(b.tick-a.tick),0.,1.));
    displayed_=a.birds;
    for (std::size_t i=0;i<displayed_.size();++i) {
        displayed_[i].position=a.birds[i].position+(b.birds[i].position-a.birds[i].position)*alpha;
        displayed_[i].velocity=a.birds[i].velocity+(b.birds[i].velocity-a.birds[i].velocity)*alpha;
    }
    return displayed_;
}
} // namespace viewer::boid
