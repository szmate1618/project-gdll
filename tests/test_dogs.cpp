#include "dog_layer.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>
namespace {
void require(bool condition,const char* message) { if (!condition) throw std::runtime_error(message); }
viewer::DogLayer pack(std::size_t count = 1) {
    viewer::DogLayer dogs;
    auto& asset = dogs.assets.emplace_back();
    asset.frameCount = 30; asset.duration = 1;
    asset.walkClip = viewer::BakedAnimationClip{"walk",1,30,30,{}};
    asset.attackLeftClip = viewer::BakedAnimationClip{"attack",1,31,60,{},false};
    dogs.lodAssets.resize(1); dogs.assetBounds.push_back({{-.2f,0,-.6f},{.2f,.9f,.6f}});
    dogs.instances.resize(count);
    for (std::size_t id = 0; id < count; ++id)
        dogs.instances[id].transform = dogs.instances[id].root = glm::translate(glm::mat4(1),{float(id)*2,0,0});
    dogs.rebuildSpatialIndex(); return dogs;
}
viewer::Model wall() {
    viewer::Model model; viewer::Primitive primitive;
    for (const auto point : {glm::vec3(-10,0,.5f),{10,0,.5f},{10,3,.5f},{-10,3,.5f}}) {
        viewer::Vertex v; v.position = point; primitive.vertices.push_back(v);
    }
    primitive.indices = {0,1,2,0,2,3}; model.primitives.push_back(primitive);
    model.draws.push_back({0,glm::mat4(1),"building_test_walls"}); return model;
}
void behavior() {
    viewer::CollisionWorld empty(viewer::Model{});
    auto dogs = pack(); auto independent = pack();
    dogs.updateBehavior(.1f,{{{0,0,20}}},empty,0);
    require(dogs.behaviors[0].state == viewer::DogState::chasing,"Full sight must start pursuit");
    require(dogs.instances[0].running && dogs.instances[0].walking,"Pursuit must run");
    require(std::abs(dogs.instances[0].root[3].z-.3f)<.0001f,"Dog chase speed must mirror 3 m/s");
    require(!independent.behaviors[0].active && independent.instances[0].root[3].z == 0,"Dog populations share state");
    require(viewer::dogAnimationClip(dogs.assets[0],dogs.instances[0]) == &*dogs.assets[0].walkClip,"Running dogs must use walk");
    const auto walkClock = dogs.instances[0].walkSeconds;
    dogs.updateBehavior(.1f,{},empty,.1);
    require(!dogs.instances[0].walking && dogs.instances[0].walkSeconds == walkClock,"No players must stop movement");
    auto hearing = pack();
    hearing.updateBehavior(.1f,{{{0,0,-10}}},empty,0);
    hearing.makeNoise({10,0,0},40);
    require(hearing.behaviors[0].state == viewer::DogState::looking,"Idle dogs must hear player noise");
    auto outside = pack(); outside.updateBehavior(.1f,{{{0,0,200}}},empty,0);
    require(!outside.behaviors[0].active,"Distant idle dogs must not activate");
    auto bite = pack(); const std::vector<viewer::ZombiePlayer> player{{{0,0,.8f}}};
    bite.updateBehavior(.1f,player,empty,0);
    require(bite.instances[0].attacking,"Close dog must start authored attack");
    bite.updateBehavior(.1f,player,empty,.5);
    require(bite.attackHits().size()==1 && bite.attackHits()[0].damage==10,"Bite must damage at contact time");
    bite.updateBehavior(.1f,player,empty,.6);
    require(bite.attackHits().empty(),"One bite must not hit twice");
    auto blocked = pack(); viewer::CollisionWorld closed(wall());
    blocked.updateBehavior(.1f,player,closed,0);
    blocked.updateBehavior(.1f,player,closed,.5);
    require(blocked.attackHits().empty() && !blocked.instances[0].attacking,"Dogs must not attack through walls");
    auto dodged = pack(); dodged.updateBehavior(.1f,player,empty,0);
    dodged.updateBehavior(.1f,{{{0,0,10}}},empty,.5);
    require(dodged.attackHits().empty(),"Bite must recheck target at impact");
    require(dogs.damage(0,100) && dogs.instances[0].dead,"Lethal damage must kill dog");
    dogs.updateBehavior(.1f,player,empty,1);
    require(!dogs.instances[0].walking && !dogs.instances[0].attacking,"Dead dogs must stop AI");
    require(!dogs.damage(0,1),"Dead dog must not take another living hit");
}
void assets(const std::filesystem::path& path) {
    std::vector<glm::mat4> placements(3,glm::mat4(1));
    auto dogs = viewer::loadDogLayer(path,placements);
    require(dogs.assets.size()==3 && dogs.instances.size()==3,"Load exactly three approved appearances");
    viewer::CollisionWorld empty(viewer::Model{});
    for (std::size_t i=0;i<3;++i) {
        const auto& asset = dogs.assets[i];
        require(asset.skeleton.size()==25 && asset.walkClip && asset.attackLeftClip,"Dog rig/clips were lost");
        require(!asset.runClip && !asset.attackLeftClip->loop,"Dog clips do not match authored source");
        require(dogs.instances[i].asset == i && dogs.instances[i].transform == placements[i],"Dog size/variant identity changed");
        require(dogs.lodAssets[i].size()==3,"Missing dog LoDs");
        std::size_t triangles = 496;
        for (const auto& lod : dogs.lodAssets[i]) {
            std::size_t count=0; for (const auto& p:lod.model.primitives) count+=p.indices.size()/3;
            require(count < triangles && lod.skeleton.size()==25,"LoDs must reduce triangles and retain rig");
            triangles = count;
        }
    }
    // Brute force a few horizontal rays through the actual torso, independent
    // of mesh winding and without relying on a bounds-only hit.
    bool hit=false; float hitHeight=0, hitDistance=0;
    for (float y : {.2f,.4f,.6f})
        if (const auto contact=dogs.traceShot({3,y,0},{-1,0,0},empty,0)) { hit=true; hitHeight=y; hitDistance=contact->distance; }
    require(hit,"Actual dog torso cannot be shot");
    dogs.releaseAnimationVertices();
    const auto after=dogs.traceShot({3,hitHeight,0},{-1,0,0},empty,0);
    require(after && std::abs(after->distance-hitDistance)<.0001f,"Releasing baked vertices changed dog hit geometry");
    auto replica = viewer::loadDogLayer(path,placements,true);
    require(std::all_of(replica.instances.begin(),replica.instances.end(),[](const auto& dog){return dog.removed;}),"Replica slots must start hidden");
}
}
int main(int argc,char** argv) {
    try { behavior(); if (argc>1) assets(argv[1]); std::cout << "Dog behavior/assets passed\n"; }
    catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
