#include "client/dog_playback.hpp"
#include "server/dog_replication.hpp"
#include "client/firearm_hits.hpp"
#include "session_settings.hpp"
#include "server/world.hpp"
#include "simulation_clock.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>
namespace {
void require(bool v,const char* m) { if(!v) throw std::runtime_error(m); }
template<class F> void rejects(F f) { bool rejected=false; try { f(); } catch(const std::exception&) { rejected=true; } require(rejected,"Malformed dog data accepted"); }
void network() {
    using namespace multiplayer;
    viewer::DogLayer dogs; dogs.instances.resize(1000); dogs.behaviors.resize(1000);
    for (std::size_t id=0;id<1000;++id) dogs.instances[id].asset=id%3;
    DogReplication replication;
    Snapshot snapshot; snapshot.tick=60;
    std::vector<bool> seen(1000);
    for (std::uint64_t tick=60;tick<=69;tick+=3) {
        snapshot.tick=tick;
        replication.append(dogs,{0,0,0},tick,snapshot.dogs);
        require(snapshot.dogs.size()<=256,"Dog packet exceeded its bounded batch");
        snapshot.zombies.resize(1); // ID zero also exists in the humanoid namespace.
        const auto bytes=encode(snapshot);
        require(bytes.size()<12000,"Dog packet fragmentation budget exceeded");
        const auto decoded=std::get<Snapshot>(decode(bytes.data(),bytes.size()));
        require(decoded.zombies.size()==1,"Dog updates replaced humanoid state");
        for(const auto& dog:decoded.dogs) {seen[dog.id]=true;require(dog.asset==dog.id%3,"Dog variant lost on wire");}
    }
    for(bool present:seen) require(present,"Initial dog batches starved a late stable ID");
    std::fill(seen.begin(),seen.end(),false);
    for(int i=0;i<4;++i) {
        const auto frame=replication.nextFrame(dogs,69);
        require(frame.has_value(),"Initial reliable dog delivery ended too early");
        const auto bytes=encode(*frame);
        const auto copy=std::get<DogFrame>(decode(bytes.data(),bytes.size()));
        for(const auto& dog:copy.dogs) seen[dog.id]=true;
        replication.frameSent(*frame);
    }
    for(bool present:seen) require(present,"Reliable dog bootstrap lost an identity");
    require(!replication.nextFrame(dogs,69),"Reliable dog bootstrap repeated after successful send");
    auto invalid=snapshot; invalid.dogs.push_back(invalid.dogs.front()); rejects([&]{encode(invalid);});
    invalid=snapshot; invalid.dogs[1].id=invalid.dogs[0].id; rejects([&]{encode(invalid);});
    invalid=snapshot; invalid.dogs[0].asset=3; rejects([&]{encode(invalid);});
    invalid=snapshot; invalid.dogs[0].health=-1; rejects([&]{encode(invalid);});
    invalid=snapshot; invalid.dogs[0].running=true; rejects([&]{encode(invalid);});
    SessionSettings settings; settings.dogCount=1000;
    auto encoded=encode(settings); auto roundtrip=std::get<SessionSettings>(decode(encoded.data(),encoded.size()));
    require(applySessionSettings(Options{},roundtrip).dogCount==1000,"Joining must adopt host dog count");
    settings.dogCount=1001; rejects([&]{encode(settings);});
    dogs.instances[7].dead=true; dogs.instances[7].health=0;
    replication.append(dogs,{0,0,80},72,snapshot.dogs);
    bool death=false; for(const auto& d:snapshot.dogs) if(d.id==7) death=d.dead;
    require(death,"Death must bypass distant cadence");
    replication.append(dogs,{0,0,500},75,snapshot.dogs); require(snapshot.dogs.empty(),"Dog interest range ignored");
    std::fill(seen.begin(),seen.end(),false);
    for(std::uint64_t tick=78;tick<=87;tick+=3) {
        replication.append(dogs,{0,0,0},tick,snapshot.dogs);
        for(const auto& dog:snapshot.dogs) seen[dog.id]=true;
    }
    for(bool present:seen) require(present,"Reentry starved a dog identity");

}
void playback() {
    using namespace multiplayer;
    DogPlayback playback; viewer::DogLayer dogs; dogs.instances.resize(1);
    DogState state; state.walking=state.running=true;
    playback.push(60,{state},1); playback.apply(dogs,60,1);
    require(!dogs.instances[0].removed,"First dog sample must reveal hidden replica");
    viewer::AnimatedModel model;
    model.walkClip = viewer::BakedAnimationClip{"walk",1,30,30,{}};
    model.runClip = viewer::BakedAnimationClip{"run",.6f,18,60,{}};
    require(viewer::dogAnimationClip(model,dogs.instances[0]) == &*model.runClip,
            "Replicated running state must select the run clip");
    state.feet.z=.3f; state.walkSeconds=.1f; playback.push(66,{state},1.1);
    playback.apply(dogs,69,1.15); require(std::abs(dogs.instances[0].root[3].z-.15f)<.0001f,"Dog interpolation failed");
    playback.push(63,{DogState{}},1.2); playback.apply(dogs,75,1.2);
    require(std::abs(dogs.instances[0].root[3].z-.45f)<.0001f,"Reordered state overwrote dog motion");
    playback.apply(dogs,600,2); require(dogs.instances[0].root[3].z<=1.051f,"Dog extrapolation must be bounded");
    state.dead=true; state.walking=state.running=false; state.health=0; playback.push(72,{state},1.2);
    playback.apply(dogs,73,1.21); require(dogs.instances[0].dead && !dogs.instances[0].walking,"Dog death must publish immediately");
    playback.push(69,{DogState{}},1.3); playback.apply(dogs,75,1.3); require(dogs.instances[0].dead,"Late living sample resurrected dog");
    playback.apply(dogs,500,7); require(dogs.instances[0].removed,"Stale dog replica must hide");
}
void shooting() {
    using namespace multiplayer;
    viewer::WeaponConfig weapon; weapon.pellets=1;
    const auto zombie=[](glm::vec3,glm::vec3) -> std::optional<viewer::ZombieShotTrace> {
        viewer::ZombieShotTrace trace; trace.distance=10; trace.claim.zombie=0; return trace;
    };
    auto shot=reportFirearmHits(0,weapon,{0,0,0},{0,0,1},zombie,nullptr,
        [](glm::vec3,glm::vec3){return std::optional<viewer::DogShotTrace>{{0,5}};});
    require(shot.hits.empty() && shot.dogHits.size()==1,"Pellet must select nearest species");
    Command command; command.sequence=1; command.left=true; command.firearmShot=shot;
    auto bytes=encode(Commands{{command}}); auto decoded=std::get<Commands>(decode(bytes.data(),bytes.size()));
    require(decoded.commands[0].firearmShot->dogHits.size()==1,"Dog hit report lost on wire");
    shot.hits.emplace_back(); command.firearmShot=shot; rejects([&]{encode(Commands{{command}});});
}
// Exercise actual asset intersections, command serialization, authoritative
// damage/ammunition, and replicated health/death rather than injected damage.
void authoritativeShooting(const std::filesystem::path& map, const std::filesystem::path& assets) {
    using namespace multiplayer;
    for (const std::uint8_t slot : {0,3}) for (std::size_t variant=0;variant<3;++variant) {
        Options options; options.map=map; options.zombieCount=0;
        options.trees=options.forests=options.cars=false;
        Scene scene(options);
        scene.floorplans.reset();
        viewer::Model ground; viewer::Primitive floor;
        for (const auto point : {glm::vec3(-100,0,-100),{-100,0,100},{100,0,100},{100,0,-100}}) {
            viewer::Vertex vertex; vertex.position=point; floor.vertices.push_back(vertex);
        }
        floor.indices={0,1,2,0,2,3}; ground.primitives.push_back(floor);
        ground.draws.push_back({0,glm::mat4(1),"terrain"});
        ground.boundsMin={-100,0,-100}; ground.boundsMax={100,10,100};
        scene.model=std::move(ground);
        scene.collision=std::make_unique<viewer::CollisionWorld>(scene.model);
        scene.floorplans=std::make_unique<viewer::FloorplanLayer>(scene.model,*scene.doors,*scene.collision);
        scene.spawnEye={0,1.7f,0};
        const std::vector<glm::mat4> placements{glm::translate(glm::mat4(1),{0,0,-50})};
        scene.dogs=viewer::loadDogLayer(assets,placements);
        scene.dogs.instances[0].asset=variant;
        scene.dogs.releaseAnimationVertices();
        World world(scene); const auto player=world.join();
        std::uint64_t sequence=0;
        const auto send=[&](Command command) {
            command.sequence=++sequence; command.slot=slot;
            const auto bytes=encode(Commands{{command}});
            world.submit(player,std::get<Commands>(decode(bytes.data(),bytes.size())));
            world.step();
        };
        send(Command{});
        for (int tick=0;tick<600 && world.players().at(player).weapons.slot()!=slot;++tick) world.step();
        require(world.players().at(player).weapons.slot()==slot,"Shooting fixture could not equip firearm");
        const auto& weapon=world.players().at(player).weapons.equipped().config;
        const int loaded=world.players().at(player).weapons.equipped().loaded;
        const int lethalHits=slot==0 ? 10 : 5;
        DogPlayback playback; viewer::DogLayer replica;
        replica.instances.resize(1); replica.instances[0].asset=variant;
        for (int hit=1;hit<=lethalHits;++hit) {
            send(Command{});
            for (int tick=0;tick<int(std::ceil(weapon.shotIntervalSeconds*60))+1;++tick) world.step();
            const auto origin=world.players().at(player).camera.position();
            Command command; command.left=true;
            // Follow the running torso's vertical bob rather than assuming a
            // fixed height still intersects the skin at every animation phase.
            for (const float height : {.4f,.5f,.6f,.7f}) {
                const auto target=glm::vec3(scene.dogs.instances[0].root[3])+glm::vec3(0,height,0);
                const auto direction=glm::normalize(target-origin);
                command.movement.yawDegrees=glm::degrees(std::atan2(direction.z,direction.x));
                command.pitch=glm::degrees(std::asin(direction.y));
                command.firearmShot=reportFirearmHits(slot,weapon,origin,direction,
                    [](glm::vec3,glm::vec3)->std::optional<viewer::ZombieShotTrace>{return {};},nullptr,
                    [&](glm::vec3 start,glm::vec3 ray) {return scene.dogs.traceShot(start,ray,*scene.collision,viewer::SimulationClock::instance().seconds());});
                if (!command.firearmShot->dogHits.empty()) break;
            }
            require(command.firearmShot->dogHits.size()==1,"Aimed shot missed the actual dog torso");
            send(command);
            const float expected=100.f*(1.f-float(hit)/lethalHits);
            require(std::abs(scene.dogs.instances[0].health-expected)<.001f,"Accepted firearm hit did not reduce dog health");
            require(world.players().at(player).shots==std::uint64_t(hit) &&
                world.players().at(player).weapons.equipped().loaded==loaded-hit,"Dog shot was rejected or consumed incorrect ammunition");
            const auto snapshot=world.snapshot(player); const auto bytes=encode(snapshot);
            const auto copy=std::get<Snapshot>(decode(bytes.data(),bytes.size()));
            require(copy.dogs.size()==1,"Changed dog health was not replicated immediately");
            playback.push(copy.tick,copy.dogs,double(copy.tick)/60);
            playback.apply(replica,double(copy.tick),double(copy.tick)/60);
            require(std::abs(replica.instances[0].health-expected)<.001f &&
                replica.instances[0].dead==(hit==lethalHits),"Client did not receive dog damage/death");
        }
        require(scene.dogs.instances[0].dead && !scene.dogs.instances[0].walking &&
            !scene.dogs.instances[0].attacking,"Lethal gunfire did not stop dog behavior");
        std::cout<<"Dog variant "<<variant+1<<": killed by "<<lethalHits<<" hits with "<<weapon.name<<"; client death received\n";
    }
}
}
int main(int argc,char** argv) {
    try { network(); playback(); shooting(); if(argc==3) authoritativeShooting(argv[1],argv[2]); std::cout<<"Dog replication/playback passed\n"; }
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
