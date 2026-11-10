#include "server/world.hpp"
#include "inventory/crafted_item.hpp"
#include "grip_placement.hpp"
#include "simulation_clock.hpp"
#include <box3d/box3d.h>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
using namespace multiplayer;
using Action=viewer::InventoryDragActionKind;
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
struct Scratch {
    std::filesystem::path directory=std::filesystem::temp_directory_path()/
        ("gd-crafting-grip-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Scratch() { if (!std::filesystem::create_directory(directory)) throw std::runtime_error("Could not create isolated grip save directory"); }
    ~Scratch() { std::error_code ignored; std::filesystem::remove_all(directory,ignored); }
};
Snapshot wire(const Snapshot& snapshot) {
    const auto bytes=encode(snapshot);
    return std::get<Snapshot>(decode(bytes.data(),bytes.size()));
}
b3BodyId bodyOf(const std::shared_ptr<viewer::PhysicsWorld>& world,const viewer::PhysicsProp& prop) {
    struct Query { const viewer::PhysicsProp& prop; std::shared_ptr<viewer::PhysicsWorld> world; b3BodyId body{}; } query{prop,world,{}};
    const auto center=prop.center();
    b3World_OverlapAABB(world->id(),{{center.x-3,center.y-3,center.z-3},{center.x+3,center.y+3,center.z+3}},
        b3DefaultQueryFilter(),[](b3ShapeId shape,void* context) {
            auto& current=*static_cast<Query*>(context);
            const auto body=b3Shape_GetBody(shape);
            if (viewer::PhysicsProp(current.world,body)==current.prop) { current.body=body; return false; }
            return true;
        },&query);
    require(b3Body_IsValid(query.body),"Could not resolve the fixture body for suspension");
    return query.body;
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
Command craft(std::vector<viewer::InventoryDragAction> actions={},bool left=false,bool right=false,glm::vec2 drag={}) {
    Command command; command.crafting=command.objectHandling=true;
    command.left=left; command.right=right; command.drag=drag; command.inventoryActions=std::move(actions);
    return command;
}
void authoritativeGrip(const std::filesystem::path& map,const std::filesystem::path& directory) {
    Options options; options.map=map; options.zombieCount=0;
    options.trees=options.forests=options.cars=false;
    Scene scene(options); openScene(scene);
    scene.groundItems->grips(scene.boxes.get(),directory);
    World world(scene); const auto id=world.join();
    for (int tick=0;tick<5;++tick) world.step();
    const auto& player=world.players().at(id);
    std::uint64_t sequence=0;
    glm::vec3 cursor=player.camera.position()+glm::vec3(0,0,-1.2f);
    const auto send=[&](Command command={},std::optional<std::size_t> requestedSlot={}) {
        command.sequence=++sequence;
        command.slot=static_cast<std::uint8_t>(requestedSlot.value_or(player.weapons.slot()));
        command.movement.yawDegrees=-90;
        if (command.crafting) command.interactionDirection=glm::normalize(cursor-player.camera.position());
        const auto bytes=encode(Commands{{command}});
        const auto decoded=std::get<Commands>(decode(bytes.data(),bytes.size()));
        world.submit(id,decoded); world.submit(id,decoded); world.step();
    };
    const auto& broom=*scene.initialInventory.find("broom");
    const auto original=scene.groundItems->add(broom,0,{},cursor);
    scene.physics->parkCraftingProp(bodyOf(scene.physics,*scene.groundItems->prop(original)));
    send(craft({{Action::SetGripBegin,"",""}},true));
    require(player.interaction.settingGrip() && !player.handInventory.active() && scene.groundItems->gripPreviews().size()==1,
            "Set grip must start from its permanent crafting action without retrieving an inventory item");
    const auto initial=scene.groundItems->gripPreviews().front();
    cursor.y+=.12f;
    send(craft({},true));
    const auto moved=scene.groundItems->gripPreviews().front();
    require(glm::distance(initial.position,moved.position)>.08f &&
            std::abs(moved.position.z-cursor.z)<.03f,
            "Dragging Set grip must place the palm contact on the aimed physical surface");
    send(craft({},true,true,{80,35}));
    const auto rotated=scene.groundItems->gripPreviews().front();
    require(glm::distance(moved.position,rotated.position)<.001f && std::abs(glm::dot(moved.rotation,rotated.rotation))<.99f,
            "Right dragging Set grip must rotate the hand around its fixed palm contact");
    send(craft({{Action::HandEndDrag,"",""}},false,true));
    send(craft());
    const auto parked=scene.groundItems->gripPreviews().front();
    cursor.y+=.2f;
    send(craft());
    require(glm::distance(parked.position,scene.groundItems->gripPreviews().front().position)<.001f,
            "Releasing the grip tool must keep its authored contact until another drag");
    const auto previewSnapshot=wire(world.snapshot(id));
    require(previewSnapshot.players.front().holdingProp && previewSnapshot.gripPreviews.size()==1 &&
            glm::distance(previewSnapshot.gripPreviews[0].position,parked.position)<1e-5f,
            "The authoritative hand preview and held-tool rotation state must survive snapshot encoding");
    send(); // Closing F5 publishes the complete authored item.
    require(!player.interaction.settingGrip() && scene.groundItems->gripPreviews().empty() &&
            scene.groundItems->count()==1 && !scene.groundItems->prop(original),
            "Closing Crafting must replace the edited world item with one saved custom assembly");
    const auto savedPose=scene.groundItems->poses().front();
    const auto& saved=savedPose.entry.item;
    require(saved.crafted() && saved.grips().size()==1 && !savedPose.entry.weapon &&
            saved.inventory().model.parts.size()==broom.inventory().model.parts.size(),
            "Completing the grip must retain every broom primitive and its palm frame");
    require(glm::distance(savedPose.body.center+savedPose.body.rotation*saved.grips()[0].position,parked.position)<.001f &&
            std::abs(glm::dot(savedPose.body.rotation*saved.grips()[0].rotation,parked.rotation))>.99999f,
            "The saved model-local grip must reproduce the exact authored world contact and rotation");
    const auto path=directory/(saved.id()+".toml");
    const auto loaded=viewer::loadCraftedItem(path);
    require(loaded.id()==saved.id() && loaded.grips().size()==1 &&
            glm::distance(loaded.grips()[0].position,saved.grips()[0].position)<1e-6f,
            "Crafting completion must durably save the resulting item with its grip");
    const auto groundSnapshot=wire(world.snapshot(id));
    require(groundSnapshot.groundItems.size()==1 && groundSnapshot.groundItems[0].crafted &&
            groundSnapshot.groundItems[0].crafted->id==saved.id(),
            "A completed ground assembly must replicate as a self-contained custom definition");
    Command pickup; pickup.inventoryActions={{Action::PickUpBackpack,"",""}};
    pickup.preserveEquippedSlot=true;
    send(pickup);
    require(player.weapons.slot()==viewer::craftedSlot && player.weapons.craftedItem() &&
            player.weapons.craftedItem()->id()==saved.id() && scene.groundItems->count()==0,
            "G must acquire and equip the saved gripped item through the authoritative shortcut");
    send(); // A physical release arms the next real click after leaving Crafting.
    Command swing; swing.left=true; swing.drag={-40,0};
    send(swing);
    require(std::isfinite(player.weapons.craftedMelee().startedAt()) &&
            player.weapons.craftedMelee().pose(viewer::SimulationClock::instance().seconds()).elapsed>=0,
            "The equipped crafted object must start the axe-style directional swing");
    const auto equipped=wire(world.snapshot(id));
    require(equipped.players.front().slot==viewer::craftedSlot && equipped.players.front().equippedCrafted &&
            equipped.players.front().equippedCrafted->id==saved.id() && equipped.players.front().sword.elapsed>=0,
            "The custom equipped definition and directional swing must survive snapshot encoding");
    send({},viewer::handlingSlot);
    for (int tick=0;tick<60 && player.weapons.slot()!=viewer::handlingSlot;++tick) send({},viewer::handlingSlot);
    const auto switched=wire(world.snapshot(id));
    require(player.weapons.slot()==viewer::handlingSlot && switched.players.front().equippedCrafted &&
            switched.players.front().equippedCrafted->id==saved.id(),
            "Switching away from a crafted item must retain its replicated held definition");
    send({},viewer::craftedSlot);
    for (int tick=0;tick<60 && player.weapons.slot()!=viewer::craftedSlot;++tick) send({},viewer::craftedSlot);
    require(player.weapons.slot()==viewer::craftedSlot && wire(world.snapshot(id)).players.front().equippedCrafted->grips.size()==1,
            "A retained custom item must switch back with its saved grip");

    // Starting the permanent tool during timed retrieval must cancel that
    // reservation, so a later timer cannot replace the authored hand ghost.
    cursor=player.camera.position()+glm::vec3(0,0,-1.2f);
    const auto second=scene.groundItems->add(broom,0,{},cursor);
    scene.physics->parkCraftingProp(bodyOf(scene.physics,*scene.groundItems->prop(second)));
    const auto pistolPlacement=player.inventory.placements().at("pistol");
    send(craft({{Action::HandPickUp,"pistol",""}},true));
    require(player.handInventory.active() && player.handInventory.remaining()>0 && scene.groundItems->count()==1,
            "The pending-retrieval fixture must still be waiting for authoritative inventory access");
    send(craft({{Action::SetGripBegin,"",""}},true));
    require(player.interaction.settingGrip() && !player.handInventory.active() && !player.inventory.held() &&
            player.inventory.placements().at("pistol").storage==pistolPlacement.storage && scene.groundItems->count()==1,
            "Starting Set grip must cancel an unfinished retrieval and restore its reserved source");
    send(craft({{Action::HandEndDrag,"",""}}));
    send();
    require(scene.groundItems->count()==1 && scene.groundItems->poses().front().entry.item.crafted(),
            "A tool started during retrieval must still finish the actual edited object");
    const auto secondCustom=scene.groundItems->poses().front().entry.item.id();
    for (int tick=0;tick<120;++tick) send();
    require(!player.handInventory.active() && !player.interaction.settingGrip() && player.inventory.owns("pistol") &&
            scene.groundItems->count()==1 && scene.groundItems->poses().front().entry.item.id()==secondCustom,
            "The cancelled retrieval timer must not create a ghost prop or replace the saved assembly later");
    require(std::filesystem::exists(directory/(secondCustom+".toml")),"The second authored grip was not durably saved");
    const auto beforeReturn=scene.groundItems->poses().front();
    send(craft({{Action::SetGripBegin,"",""}},true));
    send(craft({{Action::HandEndDrag,"",""}}));
    require(player.interaction.settingGrip(),"Could not park the hand ghost for a drawer return");
    cursor.y+=.15f;
    send(craft({{Action::HandGrabWorld,"",""}},true));
    send(craft({{Action::HandStore,"",""}}));
    require(!player.interaction.settingGrip() && scene.groundItems->gripPreviews().empty() &&
            scene.groundItems->count()==1 && scene.groundItems->poses().front().entry.id==beforeReturn.entry.id &&
            scene.groundItems->poses().front().entry.item.grips()[0].position==beforeReturn.entry.item.grips()[0].position,
            "Returning a dragged grip ghost to the drawer must cancel it without replacing or saving the target");
}
}
int main(int argc,char** argv) {
    try {
        require(argc==2,"Expected the synthetic test map");
        Scratch scratch;
        authoritativeGrip(argv[1],scratch.directory/"crafted_items");
        std::cout<<"Authoritative crafting grip, pickup, swing and retrieval cancellation checks passed\n";
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
