#include "client/blood_playback.hpp"
#include "protocol.hpp"
#include "zombie_layer.hpp"
#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
template<class F> void rejects(F run) {
    bool rejected = false;
    try { run(); } catch (const std::exception&) { rejected = true; }
    require(rejected,"Malformed blood spurt was accepted");
}
viewer::Model floorModel() {
    viewer::Model model;
    viewer::Primitive floor;
    for (const auto p : {glm::vec3(-20,0,-20), {-20,0,20}, {20,0,20}, {20,0,-20}}) {
        viewer::Vertex v; v.position = p; floor.vertices.push_back(v);
    }
    floor.indices = {0,1,2,0,2,3};
    model.primitives.push_back(floor);
    model.draws.push_back({0,glm::mat4(1),"terrain"});
    return model;
}
auto particleCount(const viewer::ZombieLayer& layer) {
    const auto& particles = layer.bloodParticles().particles();
    return std::count_if(particles.begin(),particles.end(),[](const auto& p) { return p.lifetime>0; });
}
void protocol() {
    multiplayer::Snapshot snapshot;
    snapshot.bloodSpurts = {{1,{2,1,3},{0,1,0},.25f,{7,2,{.1f,.2f,.3f},{1,0,0}}},
                           {2,{1,2,3},{0,0,1},.1f,{}}};
    const auto bytes = multiplayer::encode(snapshot);
    const auto copy = std::get<multiplayer::Snapshot>(multiplayer::decode(bytes.data(),bytes.size()));
    const auto& start = copy.bloodSpurts.at(0);
    require(copy.bloodSpurts.size()==2 && start.id==1 && start.position==glm::vec3(2,1,3) &&
            start.direction==glm::vec3(0,1,0) && start.age==.25f && start.attachment.zombie==7 &&
            start.attachment.bone==2 && start.attachment.point==glm::vec3(.1f,.2f,.3f) &&
            start.attachment.direction==glm::vec3(1,0,0) && copy.bloodSpurts[1].attachment.bone==-1,
        "Spurt wire round trip lost world or bone-local start data");
    const auto bad = [&](auto change) { auto invalid = snapshot; change(invalid.bloodSpurts[0]);
        rejects([&] { multiplayer::encode(invalid); }); };
    bad([](auto& s) { s.id=0; });
    bad([](auto& s) { s.id=2; });
    bad([](auto& s) { s.age=-1; });
    bad([](auto& s) { s.age=viewer::BloodSpurts::historySeconds+1; });
    bad([](auto& s) { s.direction.x=std::numeric_limits<float>::quiet_NaN(); });
    bad([](auto& s) { s.attachment.bone=int(multiplayer::maxBones); });
    bad([](auto& s) { s.attachment.zombie=multiplayer::maxZombies; });
    rejects([&] { multiplayer::decode(bytes.data(),bytes.size()-1); });
    snapshot.bloodSpurts.resize(viewer::BloodSpurts::capacity+1);
    rejects([&] { multiplayer::encode(snapshot); });
}
void localPlayback() {
    const auto model = floorModel();
    viewer::CollisionWorld world(model);
    viewer::ZombieLayer zombies;
    multiplayer::BloodPlayback playback;
    const std::vector<viewer::BloodSpurtStart> starts{{1,{0,1,0},{0,0,1},.1f,{}}};
    // Defer starts ahead of the buffered server timeline.
    playback.apply(starts,10,9.8,100,zombies,world);
    require(particleCount(zombies)==0,"A future server hit played early");
    playback.apply(starts,10,9.9,100.1,zombies,world);
    require(particleCount(zombies)==10,"The confirmed start did not emit client droplets");
    auto repeated = starts; repeated[0].age=.2f;
    playback.apply(repeated,10.1,10,100.2,zombies,world);
    require(particleCount(zombies)==10,"Repeated event history replayed a spurt");
    // Freeze the server timeline: particles must still land and fade locally.
    playback.apply(repeated,10.1,10,102,zombies,world);
    const auto& stains = zombies.bloodParticles().stains();
    require(particleCount(zombies)==0 && zombies.bloodParticles().hasEffects(102) &&
            std::any_of(stains.begin(),stains.end(),[](const auto& s) {
                return s.size>0 && std::abs(s.point.y-.008f)<.0001f && s.normal==glm::vec3(0,1,0);
            }),"Client droplets did not leave stains on the local static floor during a server stall");
    playback.apply(repeated,10.1,10,125,zombies,world);
    require(!zombies.bloodParticles().hasEffects(125),"Client stains failed to expire during a server stall");
    // The first packet was lost; a later history still starts this event at its age.
    auto delayed = starts; delayed[0].id=2; delayed[0].age=1.6f;
    playback.apply(delayed,20,20,130,zombies,world);
    require(particleCount(zombies)==0 && zombies.bloodParticles().hasEffects(130),
        "A delayed start restarted fresh droplets or lost their ground stains");
    // Missing/streamed-out actors still get the impact without unsafe bone access.
    auto missing = starts; missing[0].id=3; missing[0].attachment={999,9,{0,0,0},{0,1,0}};
    playback.apply(missing,21,21,131,zombies,world);
    require(particleCount(zombies)==10,"Unavailable wound attachment lost the impact");
}
void attachments() {
    const auto model = floorModel(); viewer::CollisionWorld world(model);
    viewer::ZombieLayer zombies;
    zombies.assets.emplace_back().skeleton.resize(1);
    auto& actor = zombies.instances.emplace_back();
    actor.removed = true; // Cosmetic start beats the separate corpse-pose lane.
    multiplayer::BloodPlayback playback;
    const std::vector<viewer::BloodSpurtStart> starts{{1,{0,1,0},{1,0,0},0,{0,0,{0,1,0},{1,0,0}}}};
    playback.apply(starts,0,0,100,zombies,world);
    actor.removed = false; actor.ragdoll = true; actor.ragdollBones = {glm::mat4(1)};
    actor.transform[3].x = 3;
    playback.apply(starts,0,0,100.1,zombies,world);
    const auto& emitters = zombies.bloodParticles().emitters();
    require(emitters[0].point==glm::vec3(3,1,0) && particleCount(zombies)>10,
        "Client wound emitter did not follow the replicated ragdoll");
    actor.removed = true;
    playback.apply(starts,0,0,100.2,zombies,world);
    require(emitters[0].end==0,"Removed actor retained an attached emitter");
}
void rotatedHistory() {
    const auto model = floorModel(); viewer::CollisionWorld world(model);
    viewer::ZombieLayer zombies; multiplayer::BloodPlayback playback;
    viewer::BloodSpurts history;
    for (int i=0; i<260; ++i) history.emit({0,{0,1,0},{0,0,1},0,{}},0);
    playback.apply(history.recent(1.6),1.6,1.6,100,zombies,world);
    history.emit({0,{0,1,0},{0,0,1},0,{}},0);
    playback.apply(history.recent(1.6),1.6,1.6,100,zombies,world);
    const auto stains = zombies.bloodParticles().stains();
    playback.apply(history.recent(1.6),1.6,1.6,100,zombies,world);
    const auto& repeated = zombies.bloodParticles().stains();
    require(std::equal(stains.begin(),stains.end(),repeated.begin(),[](const auto& a,const auto& b) {
        return a.point==b.point && a.born==b.born && a.size==b.size && a.angle==b.angle;
    }),"Wrapping the server history replayed a retained event after a dropped batch");
}
void history() {
    viewer::BloodSpurts history;
    for (std::size_t i=0; i<viewer::BloodSpurts::capacity+3; ++i) history.emit({},10);
    const auto recent = history.recent(10.5);
    require(recent.size()==viewer::BloodSpurts::capacity &&
            std::all_of(recent.begin(),recent.end(),[](const auto& s) { return s.id>=4 && s.age==.5f; }) &&
            history.recent(9).empty() && history.recent(12).empty(),
        "Spurt starts did not retain a bounded, expiring history");
}
}
int main() {
    try { protocol(); localPlayback(); attachments(); history(); rotatedHistory();
        std::cout << "Blood start replication, deduplication, local stains and attachments passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
