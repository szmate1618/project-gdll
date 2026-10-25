#include "server/world.hpp"
#include "client/snapshots.hpp"
#include "player_body_pose.hpp"
#include "box_layer.hpp"
#include <box3d/box3d.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace multiplayer;
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F f,const char* message) {
    bool rejected=false;
    try { f(); } catch (const std::exception&) { rejected=true; }
    require(rejected,message);
}
Snapshot wire(const Snapshot& s) {
    const auto bytes=encode(s);
    return std::get<Snapshot>(decode(bytes.data(),bytes.size()));
}
void openScene(Scene& scene) {
    viewer::Model model; viewer::Primitive p;
    for (const glm::vec3 point : {glm::vec3(-100,0,-100),{-100,0,100},{100,0,100},{100,0,-100}}) {
        viewer::Vertex v; v.position=point; p.vertices.push_back(v);
    }
    p.indices={0,1,2,0,2,3}; model.primitives.push_back(p);
    viewer::DrawInstance draw; draw.name="terrain"; model.draws.push_back(draw);
    model.boundsMin={-100,0,-100}; model.boundsMax={100,8,100}; scene.model=std::move(model);
    scene.doors=std::make_unique<viewer::DoorLayer>(std::vector<viewer::DoorPanel>{},scene.physics);
    scene.collision=std::make_unique<viewer::CollisionWorld>(scene.model);
    scene.collision->setBoxes(scene.boxes.get()); scene.collision->setVehicles(scene.vehicles.get());
    scene.spawnEye={0,1.7f,0}; scene.spawnOrientation={-90,0};
    scene.vehicles->add({60,.68f,60}); // Keep the spawn clear and prevent the fallback car.
    viewer::AnimatedModel asset; asset.model=viewer::BoxLayer::unitCube(); asset.model.draws.emplace_back();
    for (auto& v : asset.model.primitives[0].vertices) v.position=v.position*glm::vec3(.45f,1.8f,.32f)+glm::vec3(0,.9f,0);
    asset.model.boundsMin={-.225f,0,-.16f}; asset.model.boundsMax={.225f,1.8f,.16f};
    scene.zombies.assets.push_back(std::move(asset));
    for (float x : {0.f,1.f}) {
        viewer::ZombieInstance zombie;
        zombie.restRoot=zombie.restTransform=zombie.transform=glm::translate(glm::mat4(1),{x,0,-.78f});
        scene.zombies.instances.push_back(zombie);
    }
    scene.zombies.setPerceptionEnabled(false); scene.zombies.rebuildSpatialIndex();
}
Command fists(std::uint64_t sequence,bool left=false) {
    Command c; c.sequence=sequence; c.slot=static_cast<std::uint8_t>(viewer::punchSlot);
    c.movement.yawDegrees=-90; c.left=left; return c;
}
void poseProtocol() {
    auto physics=std::make_shared<viewer::PhysicsWorld>();
    viewer::PlayerBody body(physics,1.7f); body.follow({0,1.7f,0},{0,0,-1},true);
    body.follow({.1f,1.7f,0},{1,0,0},true);
    require(glm::distance(body.eyePosition(),glm::vec3(0,1.7f,0))<.00001f,
            "Following a future translated/turned eye must retain the solved aim origin until physics advances");
    body.follow({0,1.7f,0},{0,0,-1},true);
    body.punch({0,0,-1});
    PlayerState player; player.id=1; player.slot=static_cast<std::uint8_t>(viewer::punchSlot); player.melee=body.pose();
    Snapshot s; s.players.push_back(player);
    const auto copy=wire(s).players[0];
    require(copy.melee.enabled && copy.melee.striking==2 && copy.melee.eye==player.melee.eye,
            "Melee wire format lost physical body parts or hand state");
    for (std::size_t p=0;p<copy.melee.parts.size();++p)
        for (int c=0;c<4;++c) require(glm::length(copy.melee.parts[p][c]-player.melee.parts[p][c])<1e-5f,
                "Physical box TRS changed the transmitted pose");
    auto bad=s; bad.players[0].melee.parts[0][0].x=0;
    rejects([&]{encode(bad);},"Degenerate body pose was accepted");
    bad=s; bad.players[0].melee.parts[0][0].w=1;
    rejects([&]{encode(bad);},"Projective body pose was accepted");
    bad=s; bad.players[0].melee.striking=4;
    rejects([&]{encode(bad);},"Invalid punching hand was accepted");
    bad=s; bad.players[0].melee.returning=2;
    rejects([&]{encode(bad);},"A hand cannot strike and retract simultaneously");
    bad=s; bad.players[0].slot=0;
    rejects([&]{encode(bad);},"A firearm-equipped player published a melee body");
    bad=s; bad.players[0].vehicle=0; bad.players[0].seat=0;
    rejects([&]{encode(bad);},"A seated player published a melee body");
    auto bytes=encode(s); bytes[4]=11;
    rejects([&]{decode(bytes.data(),bytes.size());},"Old protocol accepted melee state");
    const auto valid=encode(s);
    for (std::size_t n=0;n<bytes.size();++n) {
        rejects([&]{decode(valid.data(),n);},"Truncated melee pose was accepted");
    }
    Snapshot full; full.vehicles.resize(maxVehicles); full.zombies.resize(maxVisibleZombies);
    full.doors.resize(maxDoors); full.boxes.resize(maxBoxes); full.players.resize(maxPlayers,player);
    for (std::size_t i=0;i<full.vehicles.size();++i) full.vehicles[i].id=static_cast<std::uint32_t>(i);
    for (std::size_t i=0;i<full.zombies.size();++i) full.zombies[i].id=static_cast<std::uint32_t>(i);
    for (std::size_t i=0;i<full.doors.size();++i) full.doors[i].id=static_cast<std::uint32_t>(i);
    for (std::size_t i=0;i<full.boxes.size();++i) full.boxes[i].id=static_cast<std::uint32_t>(i);
    for (std::size_t i=0;i<full.players.size();++i) full.players[i].id=static_cast<std::uint32_t>(i+1);
    require(encode(full).size()<=maxMessageBytes && wire(full).players.size()==maxPlayers,
            "Eight physical players with the full fleet exceeded the bounded snapshot budget");
}
void authoritativeHits(const Options& options) {
    Scene scene(options); openScene(scene); World world(scene);
    const auto first=world.join(),second=world.join();
    for (auto id : {first,second}) world.submit(id,Commands{{fists(1)}});
    for (int tick=0;tick<600 && (world.players().at(first).weapons.slot()!=viewer::punchSlot ||
                               world.players().at(second).weapons.slot()!=viewer::punchSlot);++tick) world.step();
    require(world.players().at(first).weapons.slot()==viewer::punchSlot && world.players().at(second).weapons.slot()==viewer::punchSlot,
            "Both players must finish putting away their weapons before punching");
    for (std::uint64_t sequence=2;sequence<=91;++sequence) {
        for (auto id : {first,second}) {
            auto command=fists(sequence,true); command.reload=sequence==40;
            world.submit(id,Commands{{command}}); world.submit(id,Commands{{command}});
        }
        world.step();
    }
    require(world.players().at(first).shots==1 && world.players().at(second).shots==1,
            "Holding or resending a punch generated duplicate actions");
    require(scene.zombies.ragdollCount()==2 && scene.zombies.instances[0].springRagdoll && scene.zombies.instances[1].springRagdoll,
            "Two clients did not hit their zombies through shared physical contacts");
    auto snapshot=wire(world.snapshot(first));
    require(snapshot.players[0].melee.enabled && snapshot.players[1].melee.enabled && snapshot.zombies[0].dead && snapshot.zombies[1].dead,
            "Physical player poses or hit outcomes were absent from the observer snapshot");
    for (auto id : {first,second}) world.submit(id,Commands{{fists(92)}});
    world.step();
    for (auto id : {first,second}) world.submit(id,Commands{{fists(93,true)}});
    world.step();
    for (int n=0;n<8 && (world.players().at(first).resolved<93 || world.players().at(second).resolved<93);++n) world.step();
    snapshot=wire(world.snapshot(second));
    require(world.players().at(first).shots==2 && world.players().at(second).shots==2 && (snapshot.players[0].melee.striking|snapshot.players[0].melee.returning)==1 &&
            (snapshot.players[1].melee.striking|snapshot.players[1].melee.returning)==1 && scene.zombies.ragdollCount()==2,
            "Fresh punches must alternate hands and reuse the held ragdolls");
    const auto late=world.join();
    Scene client(options); openScene(client); Snapshots playback;
    playback.push(wire(world.snapshot(late)),0);
    for (int n=0;n<2;++n) {
        const auto pose=world.nextPose(late); require(pose.has_value(),"Late join lost a melee zombie pose");
        playback.pushPose(client,*pose); world.poseSent(late,*pose); world.acknowledgePose(late,{pose->id,pose->version});
    }
    playback.apply(client,0);
    require(playback.players()[0].melee.enabled && !client.zombies.instances[0].removed && client.zombies.instances[0].ragdoll,
            "Late client could not render a physical player and its struck zombie");
    const auto before=b3World_GetCounters(scene.physics->id()).bodyCount;
    world.leave(second);
    require(b3World_GetCounters(scene.physics->id()).bodyCount==before-int(viewer::playerBodyPartCount)-1,
            "Disconnect retained a player's physical bodies");
    auto interaction=fists(94,true); interaction.interaction=true;
    world.submit(first,Commands{{interaction}}); world.step();
    require(!wire(world.snapshot(first)).players[0].melee.enabled && world.players().at(first).shots==2,
            "Interaction mode left the physical fists enabled or punched");
    auto gun=fists(95); gun.slot=0; world.submit(first,Commands{{gun}}); world.step();
    require(!wire(world.snapshot(first)).players[0].melee.enabled,"Equipping a gun retained the fists body");
}
void rejectedPunches(const Options& options) {
    Scene scene(options); openScene(scene);
    scene.zombies.instances.clear(); scene.zombies.rebuildSpatialIndex();
    World world(scene); const auto id=world.join(); world.submit(id,Commands{{fists(1)}});
    for(int n=0;n<600 && world.players().at(id).weapons.slot()!=viewer::punchSlot;++n) world.step();
    const auto& player=world.players().at(id);
    require(player.body.enabled() && player.body.canPunch(),"Rejection fixture did not enable fists");
    // Put both real arms in flight without advancing the equipment timer. The
    // test owns this non-const world; its inspection API only exposes const players.
    auto& body=const_cast<viewer::PlayerBody&>(player.body);
    require(body.punch({0,0,-1}) && body.punch({0,0,-1}) && !body.canPunch(),
            "Rejection fixture must make the alternating hand unavailable");
    const auto readyAt=player.weapons.equipped().readyAt;
    auto reload=fists(3,true); reload.reload=true;
    world.submit(id,Commands{{fists(2,true),reload,fists(4,true),fists(5,true)}});
    world.step();
    require(player.resolved==3 && player.shots==0 && player.weapons.equipped().readyAt==readyAt,
            "Rejected physical punches followed by a batched reload must not count or spend cooldown");

    body.follow(player.camera.position(),player.camera.direction(),false);
    body.follow(player.camera.position(),player.camera.direction(),true);
    require(body.canPunch(),"Rejection fixture must restore an available hand");
    world.step(); world.step();
    require(player.shots==0 && player.weapons.equipped().readyAt==readyAt,
            "Holding after physical rejection must not queue a punch");
    world.submit(id,Commands{{fists(6)}}); world.step();
    world.submit(id,Commands{{fists(7,true)}}); world.step();
    require(player.shots==1 && player.weapons.equipped().readyAt>readyAt &&
            (player.body.pose().striking|player.body.pose().returning)!=0,
            "A fresh click after physical rejection must start and count one real punch immediately");
}
void movingTargetContactsMatchSnapshot(const Options& options) {
    Scene scene(options); openScene(scene); scene.zombies.instances.resize(1); scene.zombies.rebuildSpatialIndex();
    World world(scene); const auto id=world.join(); world.submit(id,Commands{{fists(1)}});
    for(int n=0;n<600 && world.players().at(id).weapons.slot()!=viewer::punchSlot;++n) world.step();
    require(world.players().at(id).body.enabled(),"Moving-target fixture did not enable fists");
    auto& zombie=scene.zombies.instances[0];
    zombie.restRoot=zombie.restTransform=zombie.transform=glm::translate(glm::mat4(1),{0,0,-2.5f});
    scene.zombies.rebuildSpatialIndex(); scene.zombies.setPerceptionEnabled(true);
    auto filter=b3DefaultQueryFilter();
    filter.categoryBits=viewer::playerBodyCategory; filter.maskBits=viewer::meleeZombieCategory;
    for(int tick=0;tick<5;++tick) {
        const float previous=zombie.restRoot[3].z;
        world.step();
        require(zombie.restRoot[3].z>previous+.02f && !zombie.ragdoll,
                "Moving-target fixture must keep approaching throughout the contact checks");
        const auto snapshot=wire(world.snapshot(id));
        require(snapshot.zombies.size()==1 && snapshot.zombies[0].root==zombie.restRoot,
                "Moving target snapshot lost its current root");
        const auto ray=b3World_CastRayClosest(scene.physics->id(),{0,1,0},{0,0,-3},filter);
        require(ray.hit && std::abs(ray.point.z-(snapshot.zombies[0].root[3].z+.16f))<.0001f,
                "Melee contact surfaces must match the moving target's published pose in the same tick");
    }
}
viewer::PlayerBodyPose runningPunchPose(const Options& options,bool batched,bool directStart) {
    Scene scene(options); openScene(scene); scene.zombies.instances.clear(); scene.zombies.rebuildSpatialIndex();
    World world(scene); const auto id=world.join(); world.submit(id,Commands{{fists(1)}});
    for(int n=0;n<600 && world.players().at(id).weapons.slot()!=viewer::punchSlot;++n) world.step();
    const auto& player=world.players().at(id);
    require(player.body.canPunch(),"Running aim fixture did not enable an available hand");
    const auto startEye=player.camera.position();
    if(directStart) {
        viewer::MeleeCombat melee(scene.physics);
        auto& body=const_cast<viewer::PlayerBody&>(player.body);
        require(melee.punch(body,startEye,player.camera.direction(),*scene.collision),
                "Running aim reference must launch from the current solved eye before movement");
    }
    auto first=fists(2,!batched && !directStart); first.movement.right=1; first.movement.run=true;
    if(batched) {
        auto second=first; second.sequence=3; second.left=!directStart;
        auto third=first; third.sequence=4;
        auto fourth=first; fourth.sequence=5;
        world.submit(id,Commands{{first,second,third,fourth}});
    } else world.submit(id,Commands{{first}});
    world.step();
    require(player.camera.position().x>startEye.x+.09f && player.resolved==(batched?3u:2u),
            "Running aim fixture must move one full tick and consume the intended punch command");
    require(player.shots==(directStart?0u:1u),"A running command must accept exactly one punch");
    return player.body.pose();
}
void runningPunchStartsAtSolvedEye(const Options& options) {
    for(const bool batched : {false,true}) {
        const auto reference=runningPunchPose(options,batched,true);
        const auto commanded=runningPunchPose(options,batched,false);
        require(commanded.striking==reference.striking && commanded.returning==reference.returning,
                "Commanded running punches must share the direct start-of-step strike state");
        for(std::size_t p=0;p<commanded.parts.size();++p)
            for(int c=0;c<4;++c)
                require(glm::distance(commanded.parts[p][c],reference.parts[p][c])<.00001f,
                        "Running punch aim must use the solved eye even after an earlier batched movement command");
    }
}
void simultaneousSharedImpact(const Options& options) {
    Scene scene(options); openScene(scene); scene.zombies.instances.resize(1); scene.zombies.rebuildSpatialIndex();
    scene.zombies.attachPhysics(scene.physics);
    viewer::PlayerBody first(scene.physics,1.7f),second(scene.physics,1.7f);
    first.follow({0,1.7f,0},{0,0,-1},true); second.follow({0,1.7f,0},{0,0,-1},true);
    first.punch({0,0,-1}); second.punch({0,0,-1});
    viewer::PlayerBody* bodies[]={&first,&second}; viewer::MeleeCombat melee(scene.physics);
    melee.prepare(bodies,2,&scene.zombies,0);
    require(melee.contactStats().deformingMeshes==2,"Two players duplicated one zombie's two-sided contact surface");
    for(int n=0;n<30 && !scene.zombies.instances[0].ragdoll;++n)
        scene.physics->step(tickSeconds,[&](float step) {
            first.beforeStep(step); second.beforeStep(step); melee.prepare(bodies,2,&scene.zombies,0);
        },[&](float) {
            melee.finish(bodies,2,&scene.zombies,0); first.afterStep(); second.afterStep();
        });
    require(scene.zombies.ragdollCount()==1 && first.returning(viewer::PlayerBodyPart::rightFist) &&
            second.returning(viewer::PlayerBodyPart::rightFist),
            "The first hit invalidated another player's solved contact before handoff");
}
void seatedFists(const Options& options) {
    Scene scene(options); openScene(scene);
    const auto box=scene.vehicles->pose(0).chassis;
    scene.spawnEye=box.center+scene.vehicles->config(0).seats[1].entryPosition+glm::vec3(.6f,0,0);
    World world(scene); const auto id=world.join(); world.submit(id,Commands{{fists(1)}});
    for(int n=0;n<600 && world.players().at(id).weapons.slot()!=viewer::punchSlot;++n) world.step();
    require(wire(world.snapshot(id)).players[0].melee.enabled,"Seat fixture did not enable fists");
    auto enter=fists(2,true); enter.enterVehicle=true; world.submit(id,Commands{{enter}}); world.step();
    for(std::uint64_t sequence=3;sequence<20;++sequence) {
        world.submit(id,Commands{{fists(sequence,true)}}); world.step();
    }
    const auto state=wire(world.snapshot(id)).players[0];
    require(state.vehicle==0 && state.seat==1 && !state.melee.enabled && state.shots==0 && !scene.vehicles->simulating(0),
            "A seated passenger punched, rendered hands, or activated parked physics");
}
void interpolation(const Options& options) {
    Scene scene(options);
    auto physics=std::make_shared<viewer::PhysicsWorld>(); viewer::PlayerBody body(physics,1.7f);
    body.follow({0,1.7f,0},{0,0,-1},true);
    PlayerState p; p.id=1; p.slot=static_cast<std::uint8_t>(viewer::punchSlot); p.melee=body.pose();
    Snapshot a; a.tick=10; a.players.push_back(p);
    auto b=a; b.tick=22; b.players[0].movement.feet.x=1; b.players[0].melee.eye.x=1;
    for (auto& part : b.players[0].melee.parts) {
        part[3].x+=1;
        const auto rotation=glm::rotate(glm::mat4(1),glm::radians(90.f),glm::vec3(0,1,0));
        for (int c=0;c<3;++c) part[c]=rotation*part[c];
    }
    Snapshots playback; playback.push(wire(a),0); playback.push(wire(b),.2); playback.apply(scene,.2);
    const auto& pose=playback.players()[0].melee;
    require(std::abs(pose.eye.x-.5f)<.001f && viewer::validPlayerBodyPose(pose),
            "Player body did not interpolate with rigid dimensions and movement");
    require(!playback.push(a,.3),"Reordered snapshots replaced a newer physical pose");
    auto unequipped=b; unequipped.tick=34; unequipped.players[0].slot=0; unequipped.players[0].melee={};
    playback.push(wire(unequipped),.4); playback.apply(scene,.4);
    require(!playback.players()[0].melee.enabled,"Unequipping replayed an older punch pose");
}
}
int main(int argc,char** argv) {
    try {
        require(argc==2,"Expected test map");
        Options options; options.map=argv[1]; options.zombieCount=0; options.trees=options.forests=false;
        poseProtocol(); authoritativeHits(options); rejectedPunches(options); movingTargetContactsMatchSnapshot(options);
        runningPunchStartsAtSolvedEye(options); simultaneousSharedImpact(options); seatedFists(options); interpolation(options);
        std::cout<<"Multiplayer melee authority, shared contacts, replication and cleanup passed\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
