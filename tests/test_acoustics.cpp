#include "acoustics.hpp"
#include "physics_impacts.hpp"
#include "physics_scene.hpp"
#include "physics_world.hpp"
#include "box_layer.hpp"
#include "collision_world.hpp"
#include "inventory/inventory_config.hpp"
#include <box3d/box3d.h>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
viewer::Model ground(bool terrain) {
    viewer::Model model;
    viewer::Primitive p;
    for (const auto point : {glm::vec3(-5,0,-5),glm::vec3(-5,0,5),glm::vec3(5,0,5),glm::vec3(5,0,-5)}) {
        viewer::Vertex vertex; vertex.position=point; p.vertices.push_back(vertex);
    }
    p.indices={0,1,2,0,2,3};
    model.primitives.push_back(p);
    model.draws.push_back({0,glm::mat4(1),terrain ? "terrain" : "floor"});
    model.boundsMin={-5,-1,-5}; model.boundsMax={5,6,5};
    return model;
}
void drops(bool terrain, bool tree, bool unclassified = false) {
    auto physics=std::make_shared<viewer::PhysicsWorld>(std::filesystem::path(VIEWER_ITEM_ROOT)/"storage");
    auto model=ground(terrain);
    if (unclassified) model.draws[0].name="bench";
    std::vector<viewer::TrunkCollider> trunks;
    if (tree) trunks.push_back({{0,0,0},.35f,3});
    viewer::CollisionWorld collision(model,trunks);
    viewer::PhysicsScene scene(physics,collision,model.boundsMin,model.boundsMax);
    scene.ensureNear(model.boundsMin,model.boundsMax);
    viewer::BoxLayer boxes(physics);
    viewer::BoxSpawnMotion motion;
    motion.acoustics="beer_bottle"; motion.mass=.35f;
    motion.velocity={0,-3,0};
    boxes.add({0,tree ? 4.5f : 1.5f,0},{.15f,.3f,.15f},motion);
    std::vector<viewer::PhysicsImpact> events;
    for (int i=0; i<180; ++i) {
        physics->step(1.f/60);
        const auto current=physics->impacts().take();
        if (i<90) events.insert(events.end(),current.begin(),current.end());
        else require(current.empty(),"Settled props must not produce contact chatter");
    }
    require(!events.empty(),"A real physics impact must produce an event");
    require(events.front().sound.hardSurface==((!terrain && !unclassified) || tree),"Wrong sample surface for terrain, floor, tree or unknown geometry");
    require(events.front().sound.material==viewer::AcousticMaterial::glass,"Named doodad acoustic profile was lost");
    require(events.front().noiseDb>0 && events.front().sound.gain>0,"Impact must carry independent hearing and playback levels");
    require(events.size()<8,"A single drop generated repeated triangle/contact noise");
    boxes.remove(0);
    motion.acoustics="stone";
    boxes.add({2,1.5f,0},{.2f,.2f,.2f},motion);
    bool stone=false;
    for (int i=0; i<60; ++i) {
        physics->step(1.f/60);
        for (const auto& event : physics->impacts().take()) stone |= event.sound.material==viewer::AcousticMaterial::rock;
    }
    require(stone,"A recycled body must use its new acoustic profile");
}
void gameplayBypassesPlaybackBudget() {
    auto world = std::make_shared<viewer::PhysicsWorld>();
    const auto definition = b3DefaultBodyDef();
    const auto floor = b3CreateBody(world->id(),&definition);
    const auto hull = b3MakeBoxHull(100,.1f,2);
    const auto shape = b3DefaultShapeDef(); b3CreateHullShape(floor,&shape,&hull.base);
    viewer::BoxLayer boxes(world);
    viewer::BoxSpawnMotion motion; motion.velocity = {0,-3,0};
    for (int i = 0; i < 80; ++i) boxes.add({float(i-40),1,0},{.1f,.1f,.1f},motion);
    bool overflow = false;
    for (int i = 0; i < 45; ++i) {
        world->step(1.f/60);
        const auto playback = world->impacts().take();
        const auto gameplay = world->impacts().takeNoises();
        if (gameplay.size() == 80) {
            require(playback.size() < gameplay.size(),"Fixture did not exceed the bounded playback budget");
            overflow = true;
        }
    }
    require(overflow,"Simultaneous physical sounds must reach gameplay even when playback drops events");
    b3DestroyBody(floor);
}
void category(std::uint64_t category) {
    auto world=std::make_shared<viewer::PhysicsWorld>();
    const auto def=b3DefaultBodyDef();
    const auto floor=b3CreateBody(world->id(),&def);
    const auto hull=b3MakeBoxHull(5,.1f,5);
    auto shape=b3DefaultShapeDef(); shape.filter.categoryBits=category;
    b3CreateHullShape(floor,&shape,&hull.base);
    viewer::BoxLayer boxes(world);
    viewer::BoxSpawnMotion motion; motion.velocity={0,-3,0};
    boxes.add({0,1,0},{.2f,.2f,.2f},motion);
    bool found=false;
    for (int i=0;i<45;++i) {
        world->step(1.f/60);
        for (const auto& noise : world->impacts().takeNoises())
            require((noise.audience == viewer::NoiseAudience::playersOnly) == (category == viewer::zombieAttackBodyCategory),
                "Gameplay impact stream lost its player-only audience");
        for(const auto& e:world->impacts().take()) {
            found=true;
            require(e.sound.hardSurface==((category & (viewer::hardImpactCategory | viewer::doorBodyCategory | viewer::vehicleBodyCategory)) != 0),
                "Car/door/unknown surface classification is wrong");
            require((e.audience == viewer::NoiseAudience::playersOnly) == (category == viewer::zombieAttackBodyCategory),
                "An item struck by a zombie must not feed its impact noise back into AI hearing");
        }
    }
    require(found,"Synthetic collision did not generate a hit");
    b3DestroyBody(floor);
}
}
int main() {
    try {
        const auto storage=std::filesystem::path(VIEWER_ITEM_ROOT)/"storage";
        for(const auto& file:viewer::acousticConfigFiles(storage)) viewer::loadAcousticConfig(file);
        for(const auto& directory:{"weapons","ammo"})
            for(const auto& entry:std::filesystem::directory_iterator(storage/directory)) {
                const auto name=entry.path().filename().string();
                const std::string suffix=".inventory.toml";
                if(name.size()>suffix.size() && name.compare(name.size()-suffix.size(),suffix.size(),suffix)==0)
                    require(std::filesystem::exists(entry.path().parent_path()/(name.substr(0,name.size()-suffix.size())+".acoustics.toml")),
                            "An equippable definition is missing its acoustic sidecar");
            }
        const auto inventory=viewer::loadInventoryContents(VIEWER_ITEM_ROOT);
        require(inventory.owned.at("pistol_rounds_1").inventory().acoustics=="pistol_rounds",
                "An ammo instance must retain its definition's acoustic ID");
        const auto renamed=inventory.owned.at("pistol_rounds_1").renamed("ground_123");
        require(renamed.inventory().acoustics=="pistol_rounds","Transferred stacks lost their acoustic ID");
        viewer::AcousticConfig c;
        const auto low=viewer::makeImpactSound(c,1,1,1,false,{});
        const auto fast=viewer::makeImpactSound(c,1,5,1,false,{});
        const auto heavy=viewer::makeImpactSound(c,4,1,1,false,{});
        require(fast.gain>low.gain && fast.sharpness>low.sharpness && heavy.gain>low.gain,
                "Speed and mass must affect the impact sound");
        require(viewer::impactNoiseDb(c,1,5)>viewer::impactNoiseDb(c,1,1),"Stronger impacts must be heard farther away");
        auto bad=low; bad.size=std::numeric_limits<float>::quiet_NaN();
        require(!viewer::validImpactSound(bad),"Nonfinite acoustic parameters must be rejected");
        drops(true,false); drops(false,false); drops(true,true); drops(false,false,true);
        category(viewer::ordinaryBodyCategory); category(viewer::doorBodyCategory); category(viewer::vehicleBodyCategory);
        category(viewer::zombieAttackBodyCategory);
        gameplayBypassesPlaybackBudget();
        std::cout << "Acoustic profiles, physical hit events, surface selection and stack identity passed\n";
    } catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
