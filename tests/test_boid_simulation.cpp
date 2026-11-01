#include "boid/replica.hpp"
#include <cfenv>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace viewer::boid;
void require(bool b,const char* message) { if (!b) throw std::runtime_error(message); }
template<class F> void rejects(F f,const char* message) {
    bool rejected=false; try { f(); } catch (const std::exception&) { rejected=true; }
    require(rejected,message);
}
Checkpoint initial() {
    Checkpoint c; c.seed=0x62697264u;
    for (std::uint32_t f=0;f<2;++f) {
        Flock flock; flock.firstBird=static_cast<std::uint32_t>(c.birds.size()); flock.birdCount=32;
        flock.center={6000000+static_cast<std::int32_t>(f)*20000,25000,-6000000};
        flock.velocity={4000,0,2000}; flock.control.target=flock.center+Vec3{40000,2000,-50000};
        c.flocks.push_back(flock);
        for (std::uint32_t i=0;i<flock.birdCount;++i) {
            Bird b; b.position=flock.center+birdOffset(c.seed,static_cast<std::uint32_t>(c.birds.size()));
            b.velocity=flock.velocity; c.birds.push_back(b);
        }
    }
    return c;
}
std::vector<Command> commands(std::uint64_t tick,const Simulation& simulation) {
    if (tick%120) return {};
    const auto f=static_cast<std::uint32_t>((tick/120)%2);
    auto c=simulation.state().flocks[f].control;
    c.mode=static_cast<Mode>((tick/120)%4); c.remainingTicks=120;
    c.aim={6000000,1500,-6000000}; c.exit={6020000,11000,-6000000}; c.attackDirection={1000,0,0};
    c.target=c.mode==Mode::wandering?Vec3{6000000,25000,-6000000}:c.exit;
    if (c.mode==Mode::gathering) ++c.pass;
    return {{tick,f,c}};
}
void arithmetic() {
    require(ratio({-5,5,-8},1,2)==Vec3{-2,2,-4},"Signed division must truncate toward zero");
    for (std::uint64_t i=0;i<100000;i+=37) {
        const auto root=integerSqrt(i);
        require(root*root<=i && (root+1)*(root+1)>i,"Integer square root must round downward");
    }
    require(integerSqrt(UINT64_MAX)==UINT32_MAX,"Square root must handle full-width unsigned inputs");
    rejects([] { narrow(std::int64_t(INT32_MAX)+1); },"Overflow must not silently wrap signed coordinates");
    Vec3 remainder{},sum{};
    for (int i=0;i<60;++i) sum+=integrate({1,-1,61},remainder);
    require(sum==Vec3{1,-1,61} && remainder==Vec3{},"Sub-millimetre movement must survive through carried remainders");
}
void localNeighbors() {
    // Indices 0 and 1 are not the old fixed index samples. Their separation
    // straddles the origin and a grid cell boundary.
    std::vector<Bird> birds(80);
    for (std::size_t i=0;i<birds.size();++i)
        birds[i].position={10000+static_cast<std::int32_t>(i)*10000,0,0};
    birds[0].position={-100,0,0}; birds[1].position={100,0,0};
    FlockNeighbors neighbors; neighbors.reserve(birds.size());
    neighbors.rebuild(birds,0,birds.size(),0);
    require(neighbors.steering(birds,0).x<0 && neighbors.steering(birds,1).x>0,
            "Spatially close birds must separate across negative cell boundaries regardless of their indices");
    birds[1].position={1200,0,0}; birds[1].velocity={0,0,4000};
    neighbors.rebuild(birds,0,birds.size(),1);
    require(neighbors.steering(birds,0).z>0,
            "Birds must align with nearby moving birds outside the separation radius");
    neighbors.rebuild(birds,0,1,2);
    require(neighbors.steering(birds,0)==Vec3{},"Nearby birds from other flocks must not enter the neighbor calculation");
    birds[1]=birds[0];
    neighbors.rebuild(birds,0,2,3);
    const auto a=neighbors.steering(birds,0), b=neighbors.steering(birds,1);
    require(lengthSquared(a)>0 && a+b==Vec3{},
            "Coincident birds must receive opposite nonzero separation instead of remaining stacked");
}
void fluidFlock() {
    Checkpoint c; c.seed=0x62697264u;
    Flock flock; flock.birdCount=192; flock.center={0,25000,0};
    flock.velocity={0,0,8000}; flock.control.target={0,25000,2000000};
    c.flocks.push_back(flock);
    for (std::uint32_t i=0;i<flock.birdCount;++i) {
        Bird bird; bird.position=flock.center+birdOffset(c.seed,i);
        bird.velocity=flock.velocity; c.birds.push_back(bird);
    }
    Simulation simulation(c);
    for (int tick=0;tick<600;++tick) simulation.step();
    const auto settled=simulation.state();
    for (int tick=0;tick<600;++tick) simulation.step();
    const auto& after=simulation.state();
    std::size_t moved=0,reordered=0;
    for (std::size_t i=0;i<after.birds.size();++i) {
        const auto relative=after.birds[i].position-after.flocks[0].center;
        const auto before=settled.birds[i].position-settled.flocks[0].center;
        moved+=lengthSquared(relative-before)>600*600;
        const auto next=(i+1)%after.birds.size();
        reordered+=(after.birds[i].position.x<after.birds[next].position.x)!=
                   (settled.birds[i].position.x<settled.birds[next].position.x);
        require(lengthSquared(relative)<10000ull*10000,
                "Free-moving birds must stay within a cohesive flock during steady travel");
    }
    std::cout<<"Fluid flock: "<<moved<<" moved, "<<reordered<<" reordered\n";
    require(moved>after.birds.size()/2 && reordered>after.birds.size()/5,
            "Settled birds must keep exchanging positions instead of returning to permanent formation slots");
    require(after.flocks[0].center.z-settled.flocks[0].center.z>50000,
            "Internal flock movement must preserve progress along the shared route");
}
void deterministicMotion() {
    Simulation first(initial()),second(initial());
    const auto start=first.state().birds[0].position;
    const int rounding=std::fegetround();
    for (std::uint64_t tick=1;tick<=3600;++tick) {
        const auto control=commands(tick,first);
        std::fesetround(FE_UPWARD); first.step(control);
        std::fesetround(FE_DOWNWARD); second.step(control);
        require(first.checksum()==second.checksum(),"Integer movement must ignore CPU floating-point rounding mode");
    }
    std::fesetround(rounding);
    require(first.state().birds[0].position!=start,"Determinism test must exercise actual motion");
    Simulation restored(first.state());
    for (std::uint64_t tick=3601;tick<=3720;++tick) {
        const auto control=commands(tick,first); first.step(control); restored.step(control);
        require(first.checksum()==restored.checksum(),"Checkpoint must preserve velocity and integration remainders");
    }
    std::cout<<"Boid deterministic checksum after 3720 ticks: "<<first.checksum()<<'\n';
    require(first.checksum()==16094241270410465360ull,"Cross-platform boid golden state changed after checkpoint continuation");
    const auto before=first.checksum(); Command wrong; wrong.tick=first.state().tick+2;
    rejects([&] { first.step({wrong}); },"Wrong-tick controls must be rejected");
    require(before==first.checksum(),"Rejected command must not mutate simulation");
    auto invalid=first.state(); invalid.version++;
    rejects([&] { restored.restore(invalid); },"Unknown integer simulation versions must be rejected");
    invalid.version=1;
    rejects([&] { restored.restore(invalid); },"Old formation-slot simulation checkpoints must be rejected");
}
Frame checkpoint(const Simulation& s) { Frame f; f.tick=s.state().tick; f.checksum=s.checksum(); f.hasCheckpoint=true; f.checkpoint=s.state(); return f; }
void replay() {
    Simulation server(initial()); Replica client;
    require(client.receive(checkpoint(server)),"Joining replica must accept a checkpoint");
    Frame frame;
    for (std::uint64_t tick=1;tick<=600;++tick) {
        auto control=commands(tick,server); server.step(control);
        frame.commands.insert(frame.commands.end(),control.begin(),control.end());
        if (tick%3) continue;
        frame.tick=tick; frame.checksum=server.checksum();
        require(client.receive(frame) && client.simulation().checksum()==server.checksum(),"Delayed commands must replay at their original ticks");
        require(client.receive(frame),"Duplicate frame must be harmless");
        frame.commands.clear();
    }
    require(client.sample(9.95).size()==server.state().birds.size(),"Replica must present interpolated bird poses");
    // A missed state-changing command is detected even if the final tick is known.
    for (int i=0;i<120;++i) { const auto control=commands(server.state().tick+1,server); server.step(control); }
    Frame missing; missing.tick=server.state().tick; missing.checksum=server.checksum();
    require(!client.receive(missing) && !client.ready(),"Missed commands must request a checkpoint instead of silently diverging");
    require(client.receive(checkpoint(server)),"A fresh checkpoint must recover a desynchronized replica");
    Replica late; require(late.receive(checkpoint(server)),"Late join must not require replay from startup");
    require(late.simulation().checksum()==server.checksum(),"Late join must recover the exact current bird state");
}
}
int main() {
    try { arithmetic(); localNeighbors(); fluidFlock(); deterministicMotion(); replay(); std::cout<<"Fixed-point boid simulation and replay tests passed\n"; }
    catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
