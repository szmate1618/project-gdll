#include "server/world.hpp"
#include "inventory_state.hpp"
#include <box3d/box3d.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace multiplayer;
using Action=viewer::InventoryDragActionKind;
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
Snapshot wire(const Snapshot& snapshot) {
    const auto bytes=encode(snapshot);
    return std::get<Snapshot>(decode(bytes.data(),bytes.size()));
}
void openScene(Scene& scene) {
    viewer::Model model; auto& mesh=model.primitives.emplace_back();
    for (const glm::vec3 point:{glm::vec3(-100,0,-100),{-100,0,100},{100,0,100},{100,0,-100}}) {
        viewer::Vertex vertex; vertex.position=point; mesh.vertices.push_back(vertex);
    }
    mesh.indices={0,1,2,0,2,3}; model.draws.emplace_back(); model.draws.back().name="terrain";
    model.boundsMin={-100,0,-100}; model.boundsMax={100,8,100}; scene.model=std::move(model);
    scene.doors=std::make_unique<viewer::DoorLayer>(std::vector<viewer::DoorPanel>{},scene.physics);
    scene.collision=std::make_unique<viewer::CollisionWorld>(scene.model);
    scene.collision->setBoxes(scene.boxes.get()); scene.collision->setVehicles(scene.vehicles.get());
    scene.spawnEye={0,1.7f,0}; scene.spawnOrientation={-90,0};
}
b3BodyId bodyOf(const std::shared_ptr<viewer::PhysicsWorld>& world,const viewer::PhysicsProp& prop) {
    struct Query { const viewer::PhysicsProp& prop; std::shared_ptr<viewer::PhysicsWorld> world; b3BodyId body{}; } query{prop,world,{}};
    const auto center=prop.center();
    b3World_OverlapAABB(world->id(),{{center.x-2,center.y-2,center.z-2},{center.x+2,center.y+2,center.z+2}},b3DefaultQueryFilter(),
        [](b3ShapeId shape,void* context) {
            auto& current=*static_cast<Query*>(context); const auto body=b3Shape_GetBody(shape);
            if (viewer::PhysicsProp(current.world,body)==current.prop) { current.body=body; return false; }
            return true;
        },&query);
    require(b3Body_IsValid(query.body),"Could not resolve the ordinary prop body"); return query.body;
}
std::vector<std::string> customItems(const viewer::PlayerInventory& inventory) {
    std::vector<std::string> result;
    for (const auto& [id,item]:inventory.items()) if (item.custom()) result.push_back(id);
    return result;
}
void authoritativeAcquisition(const std::filesystem::path& asset) {
    Options options; options.map=asset; options.zombieCount=0; options.trees=options.forests=options.cars=false;
    Scene scene(options); openScene(scene); World world(scene); const auto id=world.join();
    for (int tick=0;tick<5;++tick) world.step();
    const auto& player=world.players().at(id); const auto initialCount=player.inventory.items().size();
    const auto selected=player.weapons.slot(); std::uint64_t sequence=0;
    glm::vec3 cursor=player.camera.position()+glm::vec3(0,0,-.9f);
    const auto send=[&](std::vector<viewer::InventoryDragAction> actions={},bool crafting=false,bool left=false) {
        Command command; command.sequence=++sequence; command.slot=static_cast<std::uint8_t>(player.weapons.slot());
        command.movement.yawDegrees=-90; command.crafting=command.objectHandling=crafting; command.left=left;
        command.inventoryActions=std::move(actions);
        if (crafting) command.interactionDirection=glm::normalize(cursor-player.camera.position());
        const auto bytes=encode(Commands{{command}}); const auto decoded=std::get<Commands>(decode(bytes.data(),bytes.size()));
        world.submit(id,decoded); world.submit(id,decoded); world.step();
    };
    const auto spawn=[&](float mass) {
        cursor=player.camera.position()+glm::vec3(0,0,-.9f);
        viewer::BoxSpawnMotion motion; motion.mass=mass; motion.materialId=viewer::propMaterialId("wood");
        const auto index=scene.boxes->add(cursor,{.12f,.15f,.1f},motion);
        scene.physics->parkCraftingProp(bodyOf(scene.physics,scene.boxes->prop(index)));
        return index;
    };
    const auto g=spawn(1.3f); const auto gSource=scene.boxes->prop(g);
    send({{Action::PickUpBackpack,"",""}});
    require(!gSource.valid() && !scene.boxes->active(g) && player.inventory.items().size()==initialCount+1 && customItems(player.inventory).size()==1,
        "Authoritative G must acquire an ordinary crate once despite duplicate commands");
    const auto h=spawn(1.7f); const auto hSource=scene.boxes->prop(h);
    send({{Action::EquipGround,"",""}});
    require(!hSource.valid() && !scene.boxes->active(h) && player.inventory.items().size()==initialCount+2 &&
            player.weapons.slot()==selected && !player.weapons.craftedItem(),
        "Authoritative H must store an ungripped ordinary object without inventing a combat weapon");
    const auto held=spawn(1.1f); const auto heldSource=scene.boxes->prop(held);
    send({{Action::HandGrabWorld,"",""}},true,true);
    require(player.interaction.heldProp() && *player.interaction.heldProp()==heldSource && !player.handInventory.active(),
        "The Crafting world action must grab an ordinary source before inventory acquisition");
    send({{Action::HandEndDrag,"",""}},true);
    send({{Action::HandStore,"",""}},true);
    require(!heldSource.valid() && !scene.boxes->active(held) && !player.interaction.grabbing() &&
            player.inventory.items().size()==initialCount+3 && customItems(player.inventory).size()==3,
        "Returning a freehand world prop through the authoritative drawer must convert and acquire it exactly once");
    const auto snapshot=wire(world.snapshot(id)); const auto& self=snapshot.players.front();
    require(self.inventoryState && !self.equippedCrafted && self.slot!=viewer::craftedSlot,
        "Ungipped imported objects must replicate as inventory items rather than equipped melee definitions");
    for (const auto index:{g,h,held}) {
        const auto removed=std::find_if(snapshot.boxes.begin(),snapshot.boxes.end(),[&](const auto& state) { return state.id==index; });
        require(removed!=snapshot.boxes.end() && removed->removed,"Acquiring an ordinary box must publish its stable-slot removal to replicas");
    }
    auto replica=scene.initialInventory;
    require(applyInventory(replica,scene.initialInventory,*self.inventoryState),"A replica rejected the authoritative imported inventory");
    for (const auto& itemId:customItems(player.inventory)) {
        const auto* item=replica.find(itemId);
        require(item && item->custom() && !item->crafted() && item->hasTag("wood") && item->grips().empty() &&
                item->inventory().model.parts.size()==1 && item->inventory().width==player.inventory.find(itemId)->inventory().width &&
                std::abs(item->inventory().model.massKg-player.inventory.find(itemId)->inventory().model.massKg)<1e-6f,
            "Encoded inventory replication must retain each imported object's footprint, model, mass and material without a grip");
    }
    const auto itemId=customItems(player.inventory).front(); const auto original=*player.inventory.find(itemId);
    send({{Action::PickUp,itemId,""},{Action::DropGround,itemId,""}});
    require(!player.inventory.owns(itemId) && scene.groundItems->count()==1,"A replicated imported item must drop through the existing inventory action");
    const auto dropped=wire(world.snapshot(id));
    viewer::GroundItems replicaGround(std::make_shared<viewer::PhysicsWorld>(),true);
    applyGround(replicaGround,scene.initialInventory,dropped.groundItems);
    const auto pose=replicaGround.poses().front();
    require(pose.entry.item.id()==itemId && pose.entry.item.custom() && !pose.entry.item.crafted() &&
            pose.entry.item.inventory().model.parts[0].material=="wood" &&
            std::abs(replicaGround.prop(pose.entry.id)->mass()-original.inventory().model.massKg)<1e-5f,
        "Ground replication must preserve the imported item definition and physical mass");
    cursor=scene.groundItems->poses().front().body.center;
    send({{Action::PickUpBackpack,"",""}},true);
    require(player.inventory.owns(itemId) && player.inventory.find(itemId)->custom() && !player.inventory.find(itemId)->crafted() &&
            scene.groundItems->count()==0 && player.inventory.items().size()==initialCount+3,
        "Reacquiring the dropped imported object must preserve its identity without reviving any original box");
}
}

int main(int argc,char** argv) {
    try {
        require(argc==2,"Expected the synthetic test map"); authoritativeAcquisition(argv[1]);
        std::cout<<"Authoritative world item acquisition and replication checks passed\n";
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
