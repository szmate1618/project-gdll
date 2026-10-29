#include "network.hpp"
#include "boid/replica.hpp"
#include "server/replication.hpp"
#include "player_body.hpp"
#include "physics_world.hpp"
#include <chrono>
#include <iostream>
#include <thread>

int main(int argc, char** argv) {
    try {
        if (argc != 5) throw std::runtime_error("Expected role, port, large-state flag and loss percentage");
        const bool server = std::string(argv[1]) == "server", large = std::stoi(argv[3]) != 0;
        const auto port = static_cast<std::uint16_t>(std::stoi(argv[2]));
        multiplayer::Network network(0, std::stof(argv[4]));
        auto peer = k_HSteamNetConnection_Invalid;
        if (server) { network.listen(port); std::cout << "READY\n" << std::flush; }
        else peer = network.connect("127.0.0.1:" + std::to_string(port));
        auto physics=std::make_shared<viewer::PhysicsWorld>(); viewer::PlayerBody body(physics,1.7f);
        body.follow({0,1.7f,0},{0,0,-1},true); body.punch({0,0,-1});
        multiplayer::Snapshot state; state.players.resize(multiplayer::maxPlayers);
        for (std::size_t p=0;p<state.players.size();++p) {
            state.players[p].id=static_cast<std::uint32_t>(p+1);
            state.players[p].slot=static_cast<std::uint8_t>(viewer::punchSlot);
            state.players[p].melee=body.pose();
        }
        state.zombies.resize(large ? multiplayer::maxZombieUpdatesPerSnapshot : 40);
        for (std::size_t i = 0; i < state.zombies.size(); ++i) {
            state.zombies[i].id = static_cast<std::uint32_t>(i); state.zombies[i].dead = !large;
        }
        if (large) {
            state.doors.resize(multiplayer::maxDoors); state.boxes.resize(multiplayer::maxBoxes);
            for (std::size_t i = 0; i < state.doors.size(); ++i) state.doors[i].id = static_cast<std::uint32_t>(i);
            for (std::size_t i = 0; i < state.boxes.size(); ++i) state.boxes[i].id = static_cast<std::uint32_t>(i);
        }
        viewer::boid::Checkpoint checkpoint; checkpoint.seed=123;
        for (std::uint32_t flock=0;flock<6;++flock) {
            viewer::boid::Flock f; f.firstBird=static_cast<std::uint32_t>(checkpoint.birds.size()); f.birdCount=192;
            f.center={static_cast<std::int32_t>(flock)*10000,25000,0}; f.velocity={3000,0,0}; f.control.target={50000,25000,0};
            checkpoint.flocks.push_back(f);
            for (int i=0;i<192;++i) {
                viewer::boid::Bird b; b.position=f.center+viewer::boid::birdOffset(checkpoint.seed,static_cast<std::uint32_t>(checkpoint.birds.size()));
                b.velocity=f.velocity; checkpoint.birds.push_back(b);
            }
        }
        viewer::boid::Simulation birdSimulation(checkpoint); viewer::boid::Replica birds;
        bool sentBirdCheckpoint=false; std::size_t birdFrames=0;
        using Clock = std::chrono::steady_clock;
        const auto start = Clock::now(); auto next = start;
        std::size_t snapshots = 0, poses = 0;
        std::uint64_t latest = 0;
        std::uint32_t nextPose = 0;
        std::map<std::uint32_t, std::uint64_t> inFlight;
        while (Clock::now() - start < std::chrono::milliseconds(server ? 6000 : 4000)) {
            const auto messages = network.receive();
            for (const auto& event : network.events)
                if (!server && event.state == k_ESteamNetworkingConnectionState_Connected)
                    network.send(peer, multiplayer::Hello{std::string(64, 'a')}, true);
            for (const auto& [connection, message] : messages) {
                if (server && std::holds_alternative<multiplayer::Hello>(message)) peer = connection;
                if (const auto* frame=std::get_if<multiplayer::BirdFrame>(&message)) {
                    if (!birds.receive(*frame)) throw std::runtime_error("Reliable bird command stream diverged under packet loss");
                    ++birdFrames;
                }
                if (const auto* snapshot = std::get_if<multiplayer::Snapshot>(&message)) {
                    ++snapshots; latest = std::max(latest, snapshot->tick);
                    if (snapshot->signals.size() != 2 || snapshot->signals[0].kind != multiplayer::SignalKind::Danger ||
                        snapshot->signals[1].id != snapshot->signals[0].id)
                        throw std::runtime_error("Transport lost concurrent team signals");
                    if (snapshot->players.size()!=multiplayer::maxPlayers) throw std::runtime_error("Transport lost physical players");
                    for (const auto& p : snapshot->players)
                        if (p.slot!=viewer::punchSlot || !p.melee.enabled || p.melee.striking!=2 || !viewer::validPlayerBodyPose(p.melee))
                            throw std::runtime_error("Transport lost or corrupted a physical punch pose");
                }
                if (const auto* pose = std::get_if<multiplayer::CorpsePose>(&message)) {
                    ++poses;
                    if (!network.send(peer, multiplayer::PoseAck{pose->id, pose->version}, true))
                        throw std::runtime_error("Cannot acknowledge bulk transfer");
                }
                if (const auto* ack = std::get_if<multiplayer::PoseAck>(&message)) {
                    const auto pending = inFlight.find(ack->id);
                    if (pending == inFlight.end() || pending->second != ack->version)
                        throw std::runtime_error("Bulk acknowledgement mismatch");
                    inFlight.erase(pending);
                }
            }
            const auto now = Clock::now();
            if (server && peer != k_HSteamNetConnection_Invalid && now >= next) {
                ++state.tick;
                state.signals = {{1, multiplayer::SignalKind::Danger, {1,2,3}, state.tick + 360},
                                 {1, multiplayer::SignalKind::Backup, {3,2,1}, state.tick + 300}};
                network.send(peer, state, false);
                if (!sentBirdCheckpoint) {
                    multiplayer::BirdFrame initial; initial.hasCheckpoint=true; initial.checkpoint=birdSimulation.state();
                    initial.tick=initial.checkpoint.tick; initial.checksum=birdSimulation.checksum();
                    if (!network.send(peer,initial,true)) throw std::runtime_error("Cannot send initial bird checkpoint");
                    sentBirdCheckpoint=true;
                }
                multiplayer::BirdFrame birdFrame;
                for (int i=0;i<3;++i) {
                    std::vector<viewer::boid::Command> commands;
                    const auto tick=birdSimulation.state().tick+1;
                    if (tick%60==0) {
                        auto control=birdSimulation.state().flocks[0].control;
                        control.target.x=tick%120? -50000:50000;
                        commands.push_back({tick,0,control}); birdFrame.commands.push_back(commands.back());
                    }
                    birdSimulation.step(commands);
                }
                birdFrame.tick=birdSimulation.state().tick; birdFrame.checksum=birdSimulation.checksum();
                if (!network.send(peer,birdFrame,true)) throw std::runtime_error("Cannot send bird command watermark");
                next = now + std::chrono::milliseconds(50);
                if (!large) for (std::size_t i = inFlight.size(); i < multiplayer::Replication::maxPoseTransfers; ++i) {
                    while (inFlight.count(nextPose)) nextPose = (nextPose + 1) % 40;
                    multiplayer::CorpsePose pose;
                    pose.id = nextPose; pose.version = state.tick; pose.bones.resize(multiplayer::maxBones, glm::mat4(1));
                    if (!network.send(peer, pose, true)) break;
                    inFlight.emplace(pose.id, pose.version); nextPose = (nextPose + 1) % 40;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (!server && (snapshots < (large ? 4u : 30u) || latest < 40 || (!large && poses < 12)))
            throw std::runtime_error("Snapshot/bulk delivery stalled: snapshots=" + std::to_string(snapshots) +
                " latest=" + std::to_string(latest) + " poses=" + std::to_string(poses));
        if (!server && (birdFrames<20 || !birds.ready() || birds.simulation().state().tick<100))
            throw std::runtime_error("Reliable bird checkpoint/command playback stalled");
        std::cout<<"bird_frames="<<birdFrames<<' ';
        std::cout << "snapshots=" << snapshots << " latest=" << latest << " poses=" << poses << '\n';
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
