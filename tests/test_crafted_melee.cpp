#include "crafted_combat.hpp"
#include "crafted_melee_geometry.hpp"
#include "collision_world.hpp"
#include "dog_layer.hpp"
#include "melee_sweep.hpp"
#include "weapons.hpp"
#include "zombie_layer.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
viewer::Item prototype() {
    viewer::ItemInventoryConfig config;
    config.name="Test crafted tool"; config.type="equipment"; config.width=config.height=1;
    return viewer::Item("custom_tool",config,{}, {"wood","crafting"});
}
viewer::Item tool(bool tiny=false) {
    viewer::ItemModelConfig model;
    viewer::ItemModelPart part;
    part.name="handle"; part.material="wood"; part.center={0,.35f,0}; part.size={.05f,.8f,.05f};
    if (tiny) { part.center={0,0,0}; part.size=glm::vec3(.001f); }
    model.parts.push_back(part);
    if (!tiny) {
        part.name="head"; part.center={-.08f,.86f,0}; part.size={.44f,.2f,.1f}; model.parts.push_back(part);
    }
    return prototype().withCrafting(model,{{}});
}
viewer::Camera eye() {
    viewer::Camera camera; camera.setPosition({0,1.5f,0}); camera.setOrientation(-90,0); return camera;
}
viewer::ZombieLayer bodies() {
    viewer::ZombieLayer zombies;
    viewer::AnimatedModel asset; asset.model.boundsMin={-.12f,0,-.12f}; asset.model.boundsMax={.12f,1.8f,.12f};
    zombies.assets.push_back(std::move(asset));
    for (const float x:{.25f,-.45f}) {
        viewer::ZombieInstance zombie;
        zombie.restRoot=zombie.restTransform=zombie.transform=glm::translate(glm::mat4(1),{x,0,-1.45f});
        zombies.instances.push_back(zombie);
    }
    zombies.rebuildSpatialIndex(); return zombies;
}
viewer::Model wall() {
    viewer::Model scene; auto& mesh=scene.primitives.emplace_back();
    for (const glm::vec3 point:{glm::vec3(-4,-1,-.95f),{4,-1,-.95f},{4,4,-.95f},{-4,4,-.95f}}) {
        viewer::Vertex vertex; vertex.position=point; mesh.vertices.push_back(vertex);
    }
    mesh.indices={0,1,2,0,2,3}; scene.draws.emplace_back(); return scene;
}
void geometry() {
    auto model=tool().inventory().model;
    model.parts.resize(1);
    auto& part=model.parts.front();
    part.shape=viewer::ItemModelShape::Frustum; part.center={.2f,.1f,-.2f};
    part.size={.4f,.8f,.6f}; part.topSize={.1f,.2f};
    part.rotation=glm::angleAxis(.7f,glm::normalize(glm::vec3(1,2,0)));
    const viewer::ItemGrip grip{{-.3f,.2f,.1f},glm::angleAxis(-.4f,glm::vec3(0,1,0))};
    const auto item=prototype().withCrafting(model,{grip});
    const auto transform=glm::translate(glm::mat4(1),grip.position)*glm::mat4_cast(grip.rotation);
    const auto toGrip=viewer::craftedItemToGrip(item);
    require(glm::distance(glm::vec3(toGrip*glm::vec4(grip.position,1)),glm::vec3(0))<1e-6f,
            "The saved palm contact must become the held origin");
    const auto parts=viewer::craftedMeleeParts(item);
    require(parts.size()==1,"A frustum must produce one exact convex volume");
    const auto partFrame=glm::translate(glm::mat4(1),part.center)*glm::mat4_cast(part.rotation);
    const auto bottom=glm::vec3(partFrame*glm::vec4(-.2f,-.4f,-.3f,1));
    const auto top=glm::vec3(partFrame*glm::vec4(-.05f,.4f,-.1f,1));
    require(glm::distance(glm::vec3(transform*glm::vec4(parts[0][0],1)),bottom)<1e-6f &&
            glm::distance(glm::vec3(transform*glm::vec4(parts[0][4],1)),top)<1e-6f,
            "Sweeps must retain each part rotation and both frustum footprints");
    model=tool().inventory().model;
    for (std::size_t index=0;index<model.parts.size();++index) {
        model.parts[index].size=glm::vec3(.1f);
        model.parts[index].center={index ? 1.f : -1.f,0,0};
    }
    const auto separated=viewer::craftedMeleeParts(prototype().withCrafting(model,{{}}));
    const glm::vec3 center(0);
    const auto target=viewer::meleeCollider(&center,1,.1f,glm::dmat4(1));
    for (const auto& shape:separated)
        require(!viewer::castMeleeShape(viewer::meleeSweep(shape.data(),8,glm::mat4(1),glm::mat4(1)),target),
                "Separate model parts must preserve empty space rather than fill one bounding box");
}
void equipment() {
    const std::filesystem::path root(VIEWER_ITEM_ROOT);
    viewer::Weapons weapons(root/"storage/weapons",root/"inventory.toml",false);
    require(!weapons.equip(viewer::craftedSlot) && !weapons.equipCrafted(prototype()),
            "An empty custom slot or an item without a saved grip cannot equip");
    auto item=tool();
    require(weapons.equipCrafted(item) && weapons.slot()==viewer::craftedSlot && weapons.craftedItem() &&
            weapons.craftedItem()->id()==item.id() && viewer::directionalMeleeSlot(weapons.slot()),
            "A crafted item must equip even without the prototype melee skills");
    weapons.trigger(true); weapons.directionalMelee().drag({-40,0});
    require(weapons.directionalMelee().update(1),"Custom equipment must accept the ordinary directional swing input");
    weapons.synchronizeCrafted(&item);
    require(weapons.craftedMelee().startedAt()==1 && weapons.status().find(item.inventory().name)!=std::string::npos,
            "Replicating an equipped definition must retain the active swing and item name");
    require(weapons.equip(viewer::handlingSlot) && weapons.craftedItem() && weapons.equip(viewer::craftedSlot),
            "Changing equipment must retain the custom held slot");
    weapons.clearCrafted();
    require(!weapons.craftedItem() && weapons.slot()==viewer::handlingSlot && !weapons.equip(viewer::craftedSlot),
            "Dropping a custom item must clear ownership and return to object handling");
}
void stoppedCuts() {
    float reference=-1;
    const auto item=tool(); const auto camera=eye();
    const viewer::CollisionWorld world(viewer::Model{});
    for (const bool coarse:{true,false}) {
        auto zombies=bodies(); viewer::SwordMelee swing; viewer::CraftedCombat combat;
        swing.trigger(true); swing.drag({-40,0}); require(swing.update(1),"Custom cut did not start");
        if (coarse) combat.update(swing,item,camera,2,2,world,&zombies,nullptr);
        else for (int step=1;step<=60;++step)
            combat.update(swing,item,camera,1+step/120.,1+step/120.,world,&zombies,nullptr);
        const auto pose=swing.pose(1.3);
        require(pose.stoppedElapsed>=viewer::swordWindupSeconds && pose.stoppedElapsed<.3f,
                "The actual crafted volume must stop at its first contact even through a frame stall");
        require(zombies.damage().injury(0) && std::abs(zombies.damage().injury(0)->health-.8f)<1e-6f &&
                !zombies.damage().injury(1),"One crafted swing must damage the first body once and stop");
        combat.update(swing,item,camera,3,3,world,&zombies,nullptr);
        require(std::abs(zombies.damage().injury(0)->health-.8f)<1e-6f && !zombies.damage().injury(1),
                "A stopped crafted cut must not continue damaging during recovery");
        if (reference<0) reference=pose.stoppedElapsed;
        else require(std::abs(reference-pose.stoppedElapsed)<.003f,"Crafted contacts must be independent of update cadence");
    }
    auto zombies=bodies(); viewer::SwordMelee swing; viewer::CraftedCombat combat;
    swing.trigger(true); swing.drag({-40,0}); swing.update(1);
    combat.update(swing,tool(true),camera,2,2,world,&zombies,nullptr);
    require(!zombies.damage().injury(0) && !zombies.damage().injury(1) && swing.pose(1.3).stoppedElapsed<0,
            "A tiny crafted object must not inherit the axe collider or reach");
}
void worldAndDogs() {
    const auto item=tool(); const auto camera=eye();
    for (const bool blocked:{false,true}) {
        viewer::DogLayer dogs;
        auto& asset=dogs.assets.emplace_back(); auto& mesh=asset.model.primitives.emplace_back();
        for (const glm::vec3 point:{glm::vec3(-.5f,0,0),{.5f,0,0},{.5f,2,0},{-.5f,2,0}}) {
            viewer::Vertex vertex; vertex.position=point; mesh.vertices.push_back(vertex);
        }
        mesh.indices={0,1,2,0,2,3}; asset.animation.resize(1);
        dogs.assetBounds.push_back({{-.5f,0,-.01f},{.5f,2,.01f}}); dogs.instances.resize(1);
        dogs.instances[0].root=dogs.instances[0].transform=glm::translate(glm::mat4(1),{0,0,-1});
        dogs.rebuildSpatialIndex();
        const viewer::CollisionWorld world(blocked ? wall() : viewer::Model{});
        viewer::SwordMelee swing; viewer::CraftedCombat combat;
        swing.trigger(true); swing.drag({-40,0}); swing.update(1);
        combat.update(swing,item,camera,2,2,world,nullptr,&dogs);
        require(std::abs(dogs.instances[0].health-(blocked ? 1.f : .8f))<1e-6f,
                "Crafted parts must hit a dog once and respect world occlusion");
        if (blocked) require(swing.pose(1.3).stoppedElapsed>=viewer::swordWindupSeconds,
                "A solid wall must stop the crafted volume even without an actor contact");
    }
    const viewer::CollisionWorld world(viewer::Model{}); auto zombies=bodies();
    viewer::SwordMelee swing; viewer::CraftedCombat combat;
    swing.trigger(true); swing.drag({-40,0}); swing.update(1); swing.cancel();
    combat.update(swing,item,camera,2,2,world,&zombies,nullptr);
    require(!zombies.damage().injury(0) && swing.pose(1.3).stoppedElapsed<0,
            "Cancelling a custom swing must prevent later damage");
}
}
int main() {
    try {
        geometry(); equipment(); stoppedCuts(); worldAndDogs();
        std::cout<<"Crafted grip geometry, equipment and actual-volume combat checks passed\n";
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
