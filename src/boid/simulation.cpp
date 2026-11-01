#include "simulation.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace viewer::boid {
namespace {
Vec3 wander(std::uint32_t seed, std::size_t index, std::uint64_t tick) {
    // Smooth acceleration changes over two seconds, with a separate phase per
    // bird. It depends only on checkpoint fields, never client clocks or RNG.
    const auto identity=hash(seed ^ static_cast<std::uint32_t>(index));
    const auto time=tick+identity%120;
    const auto epoch=time/120;
    const auto sample=[&](std::uint64_t step) {
        const auto s=hash(identity ^ hash(static_cast<std::uint32_t>(step)) ^
                          hash(static_cast<std::uint32_t>(step>>32)));
        return Vec3{static_cast<std::int32_t>(hash(s+1)%4001)-2000,
                    static_cast<std::int32_t>(hash(s+2)%1201)-600,
                    static_cast<std::int32_t>(hash(s+3)%4001)-2000};
    };
    const auto from=sample(epoch), to=sample(epoch+1);
    return from+ratio(to-from,static_cast<std::int64_t>(time%120),120);
}
Vec3 cohesion(Vec3 towardCenter, bool gathering) {
    // A loose envelope, not a spring to an individual formation slot. Its
    // strongest pull remains comparable to the shared route steering.
    const auto radius=gathering?1200:3000;
    const auto distance=integerSqrt(lengthSquared(towardCenter));
    if (distance<=static_cast<std::uint64_t>(radius)) return {};
    return direction(towardCenter,static_cast<std::int32_t>(
        std::min<std::uint64_t>((distance-radius)*(gathering?3:2),7000)));
}
}
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
    accelerations_.resize(state_.birds.size());
    std::size_t largest=0;
    for (const auto& flock:state_.flocks) largest=std::max(largest,std::size_t(flock.birdCount));
    neighbors_.reserve(largest);
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
        neighbors_.rebuild(state_.birds,flock.firstBird,count,state_.tick);
        for (std::size_t i=flock.firstBird;i<end;++i) {
            auto& bird=state_.birds[i];
            Vec3 acceleration;
            if (attack) {
                if (dot(bird.position-c.aim,c.attackDirection)>=0) bird.passedPlayer=true;
                const auto target=bird.passedPlayer?c.exit:c.aim;
                acceleration=ratio(direction(target-bird.position,speed,c.attackDirection)-bird.velocity,5,1);
            } else {
                acceleration=ratio(desired-bird.velocity,3,2)+
                    cohesion(flock.center-bird.position,gathering)+wander(state_.seed,i,state_.tick);
            }
            accelerations_[i]=limited(acceleration+neighbors_.steering(state_.birds,i),attack?60000:18000);
        }
        // Every bird reacts to the same pre-tick positions and velocities.
        for (std::size_t i=flock.firstBird;i<end;++i) {
            auto& bird=state_.birds[i];
            bird.velocity+=integrate(accelerations_[i],bird.velocityRemainder);
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
