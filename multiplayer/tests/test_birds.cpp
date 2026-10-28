#include "server/world.hpp"
#include "boid/replica.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool b,const char* message) { if (!b) throw std::runtime_error(message); }
template<class F> void rejects(F f,const char* message) {
    bool rejected=false; try { f(); } catch (const std::exception&) { rejected=true; }
    require(rejected,message);
}
multiplayer::BirdFrame roundTrip(const multiplayer::BirdFrame& frame) {
    const auto bytes=multiplayer::encode(frame);
    return std::get<multiplayer::BirdFrame>(multiplayer::decode(bytes.data(),bytes.size()));
}
void openScene(multiplayer::Scene& scene) {
    viewer::Model model; viewer::Primitive ground;
    for (const auto p:{glm::vec3(-200,0,-200),{-200,0,200},{200,0,200},{200,0,-200}}) {
        viewer::Vertex vertex; vertex.position=p; ground.vertices.push_back(vertex);
    }
    ground.indices={0,1,2,0,2,3}; model.primitives.push_back(ground);
    model.draws.push_back({0,glm::mat4(1),"terrain"}); model.boundsMin={-200,0,-200}; model.boundsMax={200,40,200};
    scene.model=std::move(model);
    scene.doors=std::make_unique<viewer::DoorLayer>(std::vector<viewer::DoorPanel>{},scene.physics);
    scene.collision=std::make_unique<viewer::CollisionWorld>(scene.model,std::vector<viewer::TrunkCollider>{},scene.doors.get());
    scene.collision->setBoxes(scene.boxes.get()); scene.collision->setVehicles(scene.vehicles.get());
    scene.vehicles->add({120,.68f,120}); scene.spawnEye={0,1.7f,0}; scene.birdsEnabled=true;
}
void protocol() {
    viewer::boid::Checkpoint c; c.seed=123;
    viewer::boid::Flock flock; flock.birdCount=1; flock.center={-6000000,20000,6000000};
    flock.control.target=flock.center; c.flocks.push_back(flock);
    viewer::boid::Bird bird; bird.position=flock.center;
    bird.velocity={-3000,7,8000}; bird.positionRemainder={-59,0,59}; bird.velocityRemainder={7,-31,4};
    c.birds.push_back(bird); viewer::boid::Simulation simulation(c);
    multiplayer::BirdFrame frame; frame.hasCheckpoint=true; frame.checkpoint=c; frame.checksum=simulation.checksum();
    viewer::boid::Replica replica;
    require(replica.receive(roundTrip(frame)),"Wire checkpoint must preserve negative coordinates and integration remainders");
    auto bytes=multiplayer::encode(frame);
    for (std::size_t i=0;i<bytes.size();++i)
        rejects([&] { multiplayer::decode(bytes.data(),i); },"Truncated bird checkpoint accepted");
    frame.checkpoint.version++;
    rejects([&] { multiplayer::encode(frame); },"Unknown boid arithmetic version accepted");
    frame.checkpoint=c; frame.checkpoint.birds[0].positionRemainder.x=60;
    rejects([&] { multiplayer::encode(frame); },"Out-of-range integration remainder accepted");
    frame.checkpoint=c; frame.checkpoint.flocks[0].firstBird=1;
    rejects([&] { multiplayer::encode(frame); },"Unassigned checkpoint bird accepted");
    frame.checkpoint=c; frame.checkpoint.birds.resize(multiplayer::maxNetworkBirds+1);
    rejects([&] { multiplayer::encode(frame); },"Oversized bird checkpoint accepted");
    frame.hasCheckpoint=false; frame.checkpoint={}; frame.tick=10;
    viewer::boid::Command command; command.tick=11; frame.commands={command};
    rejects([&] { multiplayer::encode(frame); },"Future command beyond completed watermark accepted");
    frame.commands[0].tick=8; command.tick=7; frame.commands.push_back(command);
    rejects([&] { multiplayer::encode(frame); },"Reordered bird commands accepted");
    const auto request=multiplayer::encode(multiplayer::BirdResync{});
    require(std::holds_alternative<multiplayer::BirdResync>(multiplayer::decode(request.data(),request.size())),"Checkpoint request round trip failed");
}
void authority(const char* asset) {
    multiplayer::Options options; options.map=asset; options.trees=options.forests=false; options.zombieCount=0;
    multiplayer::Scene scene(options); openScene(scene); multiplayer::World server(scene);
    const auto first=server.join(),second=server.join();
    viewer::boid::Replica client,other;
    auto checkpoint=server.birdFrame(first);
    require(checkpoint && checkpoint->hasCheckpoint && checkpoint->checkpoint.birds.size()==1152,"Join must receive the full default bird population");
    const auto wireSize=multiplayer::encode(*checkpoint).size();
    require(wireSize<multiplayer::maxMessageBytes && wireSize>50000,"Default checkpoint must fit the existing transport budget");
    require(client.receive(roundTrip(*checkpoint)),"Joining client failed to restore authoritative birds");
    server.birdFrameSent(first,checkpoint->tick);
    const auto secondCheckpoint=server.birdFrame(second);
    require(secondCheckpoint && other.receive(roundTrip(*secondCheckpoint)),"Second player must share the same global flock population");
    server.birdFrameSent(second,secondCheckpoint->tick);
    std::size_t payload=0,frames=0,commands=0;
    for (int tick=1;tick<=600;++tick) {
        server.step();
        if (tick%3) continue;
        const auto frame=server.birdFrame(first);
        require(frame && !frame->hasCheckpoint,"Normal playback must not repeatedly send full bird states");
        payload+=multiplayer::encode(*frame).size(); ++frames; commands+=frame->commands.size();
        require(client.receive(roundTrip(*frame)),"Server/client integer motion diverged through multiplayer commands");
        server.birdFrameSent(first,frame->tick);
        // Batch a second player's delivery by 250ms to exercise late commands.
        if (tick%15==0) {
            const auto late=server.birdFrame(second);
            require(late && other.receive(roundTrip(*late)),"Late reliable commands must apply at their effective ticks");
            server.birdFrameSent(second,late->tick);
            require(client.simulation().checksum()==other.simulation().checksum(),"Different delivery schedules must produce identical birds");
        }
    }
    server.requestBirdCheckpoint(first);
    const auto recovery=server.birdFrame(first);
    require(recovery && recovery->hasCheckpoint && client.receive(roundTrip(*recovery)),"Explicit recovery must provide a fresh checkpoint");
    // A silent peer cannot require unbounded command retention or catch-up.
    for (int i=0;i<601;++i) server.step();
    const auto expired=server.birdFrame(second);
    require(expired && expired->hasCheckpoint,"Peers outside replay history must receive a current checkpoint");
    std::cout<<"Bird stream: "<<frames<<" frames, "<<commands<<" commands, "<<payload
             <<" bytes over 10 simulated seconds; checkpoint "<<wireSize<<" bytes\n";
}
}
int main(int argc,char** argv) {
    try { if (argc!=2) throw std::runtime_error("Expected synthetic map asset"); protocol(); authority(argv[1]); std::cout<<"Multiplayer deterministic birds passed\n"; }
    catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
