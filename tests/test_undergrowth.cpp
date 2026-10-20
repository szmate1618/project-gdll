#include "undergrowth_fixture.hpp"
#include <iostream>
#include <stdexcept>
#include <cmath>

namespace {
using namespace undergrowth_fixture;
void require(bool condition,const char* message) { if (!condition) throw std::runtime_error(message); }
void contacts() {
    auto source=trees();
    // A bare tree must never become rustling foliage merely because it shares a mesh.
    source.instances.push_back(source.instances[0]); source.instances.back().undergrowth=viewer::UndergrowthKind::none;
    viewer::UndergrowthIndex index(source);
    require(index.size()==1,"Contact index includes non-shrub trees");
    std::vector<std::size_t> scratch;
    require(index.contact(motion({-3,0,0}),motion({3,0,0}),scratch).touched,"Swept path missed a crossed shrub");
    require(!index.contact(motion({-1,4,0}),motion({1,4,0}),scratch).touched,"Bridge above shrub created contact");
    require(!index.contact(motion({-1,-3,0}),motion({1,-3,0}),scratch).touched,"Shrub above player created contact");
    require(!index.contact(motion({1.7f,0,1.7f}),motion({1.9f,0,1.9f}),scratch).touched,
            "Empty card corner created foliage contact");
    source.instances[0].transform=glm::scale(glm::rotate(glm::translate(glm::mat4(1),{20,2,-10}),
        glm::radians(90.f),{0,1,0}),{-2,2,1});
    viewer::UndergrowthIndex transformed(source);
    require(transformed.contact(motion({20,2,-13}),motion({20,2,-7}),scratch).touched,
            "Reflected, rotated and scaled shrub lost contact");
    require(!transformed.contact(motion({24,2,-10}),motion({25,2,-10}),scratch).touched,
            "Rotated narrow axis used the broad axis size");
    auto sparse=trees(false,10000); viewer::UndergrowthIndex large(sparse);
    const auto hit=large.contact(motion({0,0,0}),motion({.2f,0,0}),scratch);
    require(hit.touched&&hit.stats.testedInstances<32,"Local query scanned the shrub population");
}

std::vector<viewer::UndergrowthEvent> walk(const viewer::UndergrowthIndex& index,bool running,int refresh=60) {
    viewer::UndergrowthEmitter emitter;
    std::vector<viewer::UndergrowthEvent> events;
    glm::vec3 feet(-1.8f,0,0);
    // Simulate 120 Hz physics and independent render refresh, including frames
    // without movement. Stay within the same large shrub for two seconds.
    double physics=0;
    for (int i=0;i<refresh*2;++i) {
        const auto before=motion(feet,running);
        physics+=120.0/refresh;
        while (physics>=1-1e-8) { feet.x+=(running?6.f:3.f)/120.f; physics-=1; }
        if (auto event=emitter.update(1.f/refresh,before,motion(feet,running),index)) events.push_back(*event);
    }
    return events;
}
void cadence() {
    auto source=trees(true);
    source.instances[0].transform=glm::scale(glm::mat4(1),glm::vec3(20,2,20));
    viewer::UndergrowthIndex index(source);
    for (const int refresh:{30,60,120,240}) {
        const auto running=walk(index,true,refresh),walking=walk(index,false,refresh);
        require(running.size()>=6&&running.size()<=8&&walking.size()>=6&&walking.size()<=8,
                "Moving shrubs must rustle with bounded cadence at every refresh rate");
        bool crack=false;
        for (const auto& event:running) {
            require(event.noise.decibels>50,"Running dry undergrowth must exceed normal running steps");
            crack|=event.noise.kind==viewer::NoiseEvent::Kind::twigSnap;
        }
        require(crack,"Running through dry shrubs never snapped a twig");
        for (const auto& event:walking)
            require(event.noise.kind==viewer::NoiseEvent::Kind::dryRustle,"Walking produced a running-only twig snap");
    }
    auto overlap=source;
    for (int i=0;i<20;++i) overlap.instances.push_back(source.instances[0]);
    require(walk(viewer::UndergrowthIndex(overlap),true).size()==walk(index,true).size(),
            "Overlapping bushes multiplied sound events");
    viewer::UndergrowthEmitter emitter;
    auto before=motion({0,0,0}),after=motion({0,0,0});
    for (int i=0;i<30;++i) require(!emitter.update(.05f,before,after,index),"Standing inside foliage made noise");
    after.feet.x=100;
    require(!emitter.update(.05f,before,after,index),"Teleport swept across foliage");
    before=motion({0,0,0});after=motion({.3f,0,0}); before.grounded=after.grounded=false;
    require(!emitter.update(.05f,before,after,index),"Airborne player emitted walking rustles");
    emitter.reset();before=motion({0,0,0});after=motion({.2f,0,0});
    require(emitter.update(.05f,before,after,index).has_value(),"Entering foliage did not rustle promptly");
    for (int i=0;i<20;++i) require(!emitter.update(.05f,after,after,index),"Stopping kept emitting rustles");
    emitter.reset(); before.height=after.height=.6f;
    const auto prone=emitter.update(.05f,before,after,index);
    require(prone&&prone->noise.decibels<40&&prone->gainScale<.4f,"Prone contact failed to reduce noise and playback gain");
}
}
int main() {
    try { contacts(); cadence(); }
    catch (const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
    std::cout<<"Shrub geometry, indexed sweeps, cadence, stances and discontinuities passed\n";
}
