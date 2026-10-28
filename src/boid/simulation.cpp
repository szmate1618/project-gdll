#include "simulation.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace viewer::boid {
void validate(const Control& c) {
    if (static_cast<unsigned>(c.mode)>static_cast<unsigned>(Mode::recovering) ||
        !bounded(c.target,coordinateLimit) || !bounded(c.aim,coordinateLimit) ||
        !bounded(c.exit,coordinateLimit) || !bounded(c.attackDirection,1001) ||
        c.remainingTicks > 36000)
        throw std::invalid_argument("Invalid boid control");
}
void validate(const Checkpoint& c) {
    if (c.version!=simulationVersion || c.birds.size()>maxBirds || c.flocks.size()>maxFlocks || c.tick>UINT64_MAX-36000)
        throw std::invalid_argument("Invalid boid checkpoint");
    std::size_t first=0;
    for (const auto& f:c.flocks) {
        validate(f.control);
        if (f.firstBird!=first || !f.birdCount || f.birdCount>4096 || f.birdCount>c.birds.size()-first ||
            !bounded(f.center,coordinateLimit) || !bounded(f.velocity,100000))
            throw std::invalid_argument("Invalid boid flock range");
        first+=f.birdCount;
    }
    if (first!=c.birds.size()) throw std::invalid_argument("Unassigned boid checkpoint birds");
    for (const auto& b:c.birds)
        if (!bounded(b.position,coordinateLimit) || !bounded(b.velocity,100000) ||
            !bounded(b.positionRemainder,59) || !bounded(b.velocityRemainder,59))
            throw std::invalid_argument("Invalid boid checkpoint bird");
}
Simulation::Simulation(Checkpoint checkpoint) { restore(std::move(checkpoint)); }
void Simulation::restore(Checkpoint checkpoint) {
    validate(checkpoint); state_=std::move(checkpoint);
    offsets_.resize(state_.birds.size());
    for (std::size_t i=0;i<offsets_.size();++i) offsets_[i]=birdOffset(state_.seed,static_cast<std::uint32_t>(i));
}
void Simulation::step(const std::vector<Command>& commands) {
    if (state_.tick>=UINT64_MAX-36000) throw std::overflow_error("Boid clock exhausted");
    for (const auto& command:commands) {
        validate(command.control);
        if (command.tick!=state_.tick+1 || command.flock>=state_.flocks.size())
            throw std::invalid_argument("Boid command has wrong tick or flock");
    }
    for (const auto& command:commands) {
        auto& flock=state_.flocks[command.flock];
        if (flock.control.pass!=command.control.pass)
            for (std::size_t i=flock.firstBird;i<flock.firstBird+flock.birdCount;++i) state_.birds[i].passedPlayer=false;
        flock.control=command.control;
    }
    for (auto& flock:state_.flocks) {
        auto& c=flock.control;
        if (c.remainingTicks) --c.remainingTicks;
        const std::size_t end=flock.firstBird+flock.birdCount;
        // Wide sums preserve ordering independence of the flock average.
        std::int64_t px=0,py=0,pz=0,vx=0,vy=0,vz=0;
        for (std::size_t i=flock.firstBird;i<end;++i) {
            const auto& b=state_.birds[i];
            px+=b.position.x; py+=b.position.y; pz+=b.position.z;
            vx+=b.velocity.x; vy+=b.velocity.y; vz+=b.velocity.z;
        }
        const auto count=flock.birdCount;
        flock.center={narrow(px/count),narrow(py/count),narrow(pz/count)};
        flock.velocity={narrow(vx/count),narrow(vy/count),narrow(vz/count)};
        const bool attack=c.mode==Mode::swooping, gathering=c.mode==Mode::gathering;
        const auto speed=attack?17000:(gathering?2500:8000);
        auto desired=direction(c.target-flock.center,speed,direction(flock.velocity,1000));
        if (gathering) desired=direction(flock.velocity,speed);
        if (!attack && !gathering && lengthSquared(c.target-flock.center)<9000000) desired={};
        for (std::size_t i=flock.firstBird;i<end;++i) {
            auto& bird=state_.birds[i];
            auto slot=attack || gathering?ratio(offsets_[i],48,100):offsets_[i];
            Vec3 acceleration;
            if (attack) {
                if (dot(bird.position-c.aim,c.attackDirection)>=0) bird.passedPlayer=true;
                slot.y/=5;
                const auto target=(bird.passedPlayer?c.exit:c.aim)+slot;
                acceleration=ratio(direction(target-bird.position,speed,c.attackDirection)-bird.velocity,5,1);
            } else {
                acceleration=ratio(desired-bird.velocity,2,1)+ratio(flock.velocity-bird.velocity,9,20)+
                    ratio(flock.center+slot-bird.position,gathering?5:7,gathering?1:5);
            }
            // Stable index order and four samples are part of the simulation
            // contract. Later birds see earlier birds' just-integrated states.
            const auto local=i-flock.firstBird;
            for (std::size_t sample=1;sample<=4 && sample<count;++sample) {
                const auto neighbor=flock.firstBird+(local+sample*37)%count;
                if (neighbor==i) continue;
                const auto away=bird.position-state_.birds[neighbor].position;
                const auto distance=lengthSquared(away);
                if (distance>10 && distance<1000000) acceleration+=ratio(away,1800000,static_cast<std::int64_t>(distance)+100000);
            }
            bird.velocity+=integrate(limited(acceleration,attack?60000:18000),bird.velocityRemainder);
            bird.velocity=limited(bird.velocity,speed*6/5);
            bird.position+=integrate(bird.velocity,bird.positionRemainder);
            if (!bounded(bird.position,coordinateLimit)) throw std::overflow_error("Boid left supported local frame");
        }
    }
    ++state_.tick;
}
std::uint64_t Simulation::checksum() const {
    // Canonical fields, explicit byte order, no struct padding or float bits.
    std::uint64_t result=14695981039346656037ull;
    const auto add=[&](std::uint64_t v) { for (int i=0;i<8;++i) { result^=(v>>(i*8))&255; result*=1099511628211ull; } };
    const auto vec=[&](Vec3 v) { add(static_cast<std::uint32_t>(v.x)); add(static_cast<std::uint32_t>(v.y)); add(static_cast<std::uint32_t>(v.z)); };
    add(state_.version); add(state_.seed); add(state_.tick); add(state_.birds.size()); add(state_.flocks.size());
    for (const auto& f:state_.flocks) {
        add(f.firstBird); add(f.birdCount); vec(f.center); vec(f.velocity);
        vec(f.control.target); vec(f.control.aim); vec(f.control.exit); vec(f.control.attackDirection);
        add(static_cast<std::uint8_t>(f.control.mode)); add(f.control.remainingTicks); add(f.control.pass);
    }
    for (const auto& b:state_.birds) { vec(b.position); vec(b.velocity); vec(b.positionRemainder); vec(b.velocityRemainder); add(b.passedPlayer); }
    return result;
}
} // namespace viewer::boid
