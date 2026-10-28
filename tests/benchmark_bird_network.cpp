#include "boid/bird_flock.hpp"
#include "boid/replica.hpp"
#include "collision_world.hpp"
#include "protocol.hpp"
#include <chrono>
#include <iomanip>
#include <iostream>

namespace {
volatile std::uint64_t sink=0;
template<class F> double measure(int count,F operation) {
    for (int i=0;i<8;++i) operation();
    const auto start=std::chrono::steady_clock::now();
    for (int i=0;i<count;++i) operation();
    return std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count()/count;
}
}
int main() {
    const viewer::CollisionWorld world(viewer::Model{});
    viewer::BirdFlocks authority(world,nullptr,{-500,0,-500},{500,100,500},{0,25,0});
    auto checkpoint=authority.simulation().state();
    multiplayer::BirdFrame frame; frame.tick=checkpoint.tick; frame.hasCheckpoint=true;
    frame.checkpoint=checkpoint; frame.checksum=authority.simulation().checksum();
    const auto bytes=multiplayer::encode(frame);
    std::cout<<std::fixed<<std::setprecision(3);
    std::cout<<"birds="<<checkpoint.birds.size()<<" flocks="<<checkpoint.flocks.size()<<'\n';
    std::cout<<"checkpoint_cpu_payload_bytes="<<checkpoint.birds.size()*sizeof(viewer::boid::Bird)+checkpoint.flocks.size()*sizeof(viewer::boid::Flock)<<'\n';
    std::cout<<"checkpoint_wire_bytes="<<bytes.size()<<'\n';
    std::cout<<"command_cpu_bytes="<<sizeof(viewer::boid::Command)<<'\n';
    std::cout<<"checkpoint_copy_us="<<measure(2000,[&] { auto copy=checkpoint; sink+=copy.birds.size(); })<<'\n';
    std::cout<<"checksum_us="<<measure(400,[&] { sink^=authority.simulation().checksum(); })<<'\n';
    std::cout<<"checkpoint_encode_us="<<measure(400,[&] { sink+=multiplayer::encode(frame).size(); })<<'\n';
    std::cout<<"checkpoint_decode_us="<<measure(400,[&] { sink+=std::get<multiplayer::BirdFrame>(multiplayer::decode(bytes.data(),bytes.size())).checkpoint.birds.size(); })<<'\n';
    std::cout<<"checkpoint_restore_us="<<measure(400,[&] { viewer::boid::Simulation simulation(checkpoint); sink^=simulation.state().tick; })<<'\n';
    std::cout<<"replica_checkpoint_receive_us="<<measure(400,[&] { viewer::boid::Replica replica; sink+=replica.receive(frame); })<<'\n';
    std::cout<<"client_pose_history_payload_bytes="<<40*checkpoint.birds.size()*sizeof(viewer::FlockBird)<<'\n';
    std::uint64_t commands=0; std::size_t maximumHistory=0;
    std::deque<viewer::boid::Command> history;
    for (int tick=0;tick<3600;++tick) {
        viewer::BirdFlockPlayer player; player.feet={0,0,0}; authority.step({player});
        for (const auto& command:authority.commands()) { history.push_back(command); ++commands; }
        const auto now=authority.simulation().state().tick;
        while (!history.empty() && now-history.front().tick>=600) history.pop_front();
        maximumHistory=std::max(maximumHistory,history.size());
    }
    std::cout<<"commands_in_simulated_minute="<<commands<<" peak_history_commands="<<maximumHistory
             <<" peak_history_payload_bytes="<<maximumHistory*sizeof(viewer::boid::Command)<<'\n';
}
