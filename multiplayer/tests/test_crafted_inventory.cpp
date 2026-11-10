#include "inventory_state.hpp"
#include "inventory/crafted_item.hpp"
#include "inventory/inventory_config.hpp"
#include "collision_world.hpp"
#include "weapons.hpp"
#include "physics_world.hpp"
#include "prop_item_geometry.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
template<class Action> void rejects(Action action,const char* message) {
    try { action(); } catch (const std::exception&) { return; }
    throw std::runtime_error(message);
}
multiplayer::Snapshot roundTrip(const multiplayer::Snapshot& snapshot) {
    const auto bytes=multiplayer::encode(snapshot);
    return std::get<multiplayer::Snapshot>(multiplayer::decode(bytes.data(),bytes.size()));
}
void ungrippedTransfers(const viewer::PlayerInventory& catalog,const std::filesystem::path& root) {
    viewer::ItemInventoryConfig config; config.name="Imported crate"; config.type="equipment";
    config.width=2; config.height=3; config.acoustics="crate";
    viewer::ItemModelConfig model; model.massKg=2;
    model.parts.push_back({"body",viewer::ItemModelShape::Box,"wood",{0,0,0},{.2f,.3f,.4f},{.4f,.2f,.1f},{.2f,.4f}});
    model.mesh={{{-.1f,-.15f,.2f},{0,0,1},{.2f,.3f,.4f}},{{.1f,-.15f,.2f},{0,0,1},{.4f,.5f,.6f}},{{0,.15f,.2f},{0,0,1},{.6f,.7f,.8f}}};
    const auto item=viewer::Item("generated_wood_box",config,{}, {"wood","crafting"}).withModel(model).renamed("lifted_crate_1");
    viewer::InventoryContents empty; empty.skills=catalog.skills();
    viewer::PlayerInventory owner(catalog.storage(),empty), replica(catalog.storage(),empty);
    const auto reference=viewer::backpackStorageReference(catalog.storage().backpack->id);
    require(owner.insert(item,0,{reference,0,0,0}),"Could not insert an imported ungripped object");
    multiplayer::Snapshot snapshot; multiplayer::PlayerState player; player.id=1;
    player.inventoryState=multiplayer::captureInventory(owner); snapshot.players.push_back(player);
    const auto decoded=roundTrip(snapshot);
    require(decoded.players[0].inventoryState->items[0].crafted && decoded.players[0].inventoryState->items[0].crafted->grips.empty(),
        "An ungripped custom item must carry its full definition on the wire");
    bool changed=false;
    require(multiplayer::applyInventory(replica,catalog,*decoded.players[0].inventoryState,&changed) && changed,
        "A replica must accept an imported object without a catalog entry or combat grip");
    const auto* restored=replica.find(item.id());
    require(restored && restored->custom() && !restored->crafted() && restored->inventory().width==2 && restored->inventory().height==3 &&
            restored->tags()==item.tags() && restored->inventory().model.mesh.size()==3 && restored->inventory().model.mesh[1].color==model.mesh[1].color,
        "An imported object's footprint, tags, and exact colored visual triangles must survive inventory replication");
    require(multiplayer::applyInventory(replica,catalog,*decoded.players[0].inventoryState,&changed) && !changed,
        "Identical ungripped definitions must not continually rebuild the inventory");
    auto edited=*decoded.players[0].inventoryState; edited.items[0].crafted->model.mesh[0].color.x=.9f;
    require(multiplayer::applyInventory(replica,catalog,edited,&changed) && changed &&
            replica.find(item.id())->inventory().model.mesh[0].color.x==.9f,
        "Changing the visual mesh on a stable imported item must refresh its definition");
    viewer::Weapons weapons(root/"storage/weapons",root/"inventory.toml",true);
    viewer::GroundItems ground(std::make_shared<viewer::PhysicsWorld>()), replicaGround(std::make_shared<viewer::PhysicsWorld>(),true);
    viewer::InventoryDrag drag(owner); viewer::CollisionWorld collision(viewer::Model{});
    require(drag.pickUp(item.id()) && ground.drop(owner,drag,weapons,{}, {0,0,-1},collision),
        "An ungripped imported object must use the ordinary inventory-to-ground transfer");
    multiplayer::Snapshot dropped; dropped.groundItems=multiplayer::captureGround(ground,{});
    const auto transferred=roundTrip(dropped); multiplayer::applyGround(replicaGround,catalog,transferred.groundItems);
    const auto pose=replicaGround.poses()[0];
    require(pose.entry.item.custom() && !pose.entry.item.crafted() && pose.entry.item.inventory().model.mesh.size()==3 &&
            pose.entry.item.inventory().model.mesh[2].position==model.mesh[2].position,
        "A ground replica must retain the imported object's actual visual mesh without requiring a grip");
    const auto source=ground.poses()[0];
    require(ground.take(source.entry.id,owner,weapons,{reference,0,0,0},source.body.center) && owner.find(item.id())->custom(),
        "An imported ground object must return to inventory without losing its custom marker");
    auto invalid=decoded; invalid.players[0].slot=static_cast<std::uint8_t>(viewer::craftedSlot);
    invalid.players[0].equippedCrafted=decoded.players[0].inventoryState->items[0].crafted;
    rejects([&] { multiplayer::encode(invalid); },"An ungripped custom definition must not become combat equipment");
    invalid=decoded; invalid.players[0].inventoryState->items[0].crafted->model.mesh.pop_back();
    rejects([&] { multiplayer::encode(invalid); },"Wire accepted an incomplete imported visual triangle");
    invalid=decoded; invalid.players[0].inventoryState->items[0].crafted->model.mesh[0].color.x=std::numeric_limits<float>::quiet_NaN();
    rejects([&] { multiplayer::encode(invalid); },"Wire accepted a nonfinite visual mesh color");
}
bool exact(glm::vec3 a,glm::vec3 b) {
    for (int axis=0;axis<3;++axis) {
        std::uint32_t left=0,right=0;
        std::memcpy(&left,&a[axis],sizeof(float)); std::memcpy(&right,&b[axis],sizeof(float));
        if (left!=right) return false;
    }
    return true;
}
void fullBottleBackpack(const viewer::PlayerInventory& catalog) {
    const auto visual=viewer::loadModel(std::filesystem::path(VIEWER_DOODAD_ASSETS)/"beer_bottle.glb");
    const auto center=(visual.boundsMin+visual.boundsMax)*.5f;
    viewer::ItemModelConfig model; model.massKg=.35f;
    model.parts.push_back({"body",viewer::ItemModelShape::Box,"unknown",{},visual.boundsMax-visual.boundsMin,{.3f,.12f,.035f}});
    model.mesh=viewer::propItemVisual(visual,glm::translate(glm::mat4(1),-center));
    require(model.mesh.size()==132,"The representative bottle must retain the real 44-triangle visual mesh");
    viewer::ItemInventoryConfig config; config.name="Beer bottle"; config.type="equipment";
    config.width=config.height=1; config.acoustics="beer_bottle";
    const auto bottle=viewer::Item("world_bottle",config,{}, {"crafting"}).withModel(model);
    viewer::InventoryContents empty; empty.skills=catalog.skills();
    viewer::PlayerInventory backpack(catalog.storage(),empty);
    const auto reference=viewer::backpackStorageReference(catalog.storage().backpack->id);
    for (int y=0;y<10;++y) for (int x=0;x<8;++x)
        require(backpack.insert(bottle.renamed("bottle_"+std::to_string(y*8+x)),0,{reference,x,y,0}),
            "Eighty one-cell world bottles must fit the authored 8x10 backpack");
    multiplayer::Snapshot snapshot; multiplayer::PlayerState player; player.id=1;
    player.slot=static_cast<std::uint8_t>(viewer::handlingSlot); player.inventoryState=multiplayer::captureInventory(backpack);
    snapshot.players.push_back(player);
    const auto rawMeshBytes=80*(4+bottle.inventory().model.mesh.size()*9*sizeof(float));
    require(rawMeshBytes>multiplayer::maxMessageBytes,"The regression must exceed the packet limit with raw mesh floats alone");
    const auto bytes=multiplayer::encode(snapshot);
    require(bytes.size()<160*1024,"A full real-prop backpack must leave room in the existing snapshot budget");
    const auto decoded=std::get<multiplayer::Snapshot>(multiplayer::decode(bytes.data(),bytes.size()));
    require(decoded.players[0].inventoryState->items.size()==80,"Mesh interning must preserve all eighty inventory records");
    for (const auto& entry:decoded.players[0].inventoryState->items) {
        require(entry.crafted && entry.crafted->model.mesh.size()==132,"An interned mesh lost its complete triangle stream");
        for (std::size_t vertex=0;vertex<132;++vertex) {
            const auto& actual=entry.crafted->model.mesh[vertex]; const auto& expected=bottle.inventory().model.mesh[vertex];
            require(exact(actual.position,expected.position) && exact(actual.normal,expected.normal) && exact(actual.color,expected.color),
                "Independent mesh dictionaries must preserve all float bits and vertex/material seams exactly");
        }
    }
    // Locate the first bounded mesh stream, then damage only its dictionary.
    const std::vector<std::uint8_t> prefix{132,0,0,0,1,24,0,0,0};
    const auto found=std::search(bytes.begin(),bytes.end(),prefix.begin(),prefix.end());
    require(found!=bytes.end(),"The repeated bottle must use the compact independent-stream encoding");
    const auto start=static_cast<std::size_t>(found-bytes.begin());
    auto malformed=bytes; malformed[start+4]=2;
    rejects([&] { multiplayer::decode(malformed.data(),malformed.size()); },"Mesh decoding accepted an invalid encoding flag");
    malformed=bytes; malformed[start+5]=0xff; malformed[start+6]=0xff;
    rejects([&] { multiplayer::decode(malformed.data(),malformed.size()); },"Mesh decoding accepted an oversized dictionary");
    std::size_t indexOffset=start+5;
    for (int stream=0;stream<3;++stream) {
        std::uint32_t count=0;
        for (int byte=0;byte<4;++byte) count|=std::uint32_t(bytes.at(indexOffset+byte))<<(byte*8);
        indexOffset+=4+count*12;
    }
    malformed=bytes; malformed.at(indexOffset)=0xff; malformed.at(indexOffset+1)=0xff;
    rejects([&] { multiplayer::decode(malformed.data(),malformed.size()); },"Mesh decoding accepted an out-of-range dictionary index");
    std::cout<<"80-bottle snapshot: "<<bytes.size()<<" bytes; raw mesh streams alone: "<<rawMeshBytes<<" bytes\n";
}
}
int main() {
    try {
        const std::filesystem::path root(VIEWER_ITEM_ROOT);
        const auto catalog=viewer::loadPlayerInventory(root);
        ungrippedTransfers(catalog,root);
        fullBottleBackpack(catalog);
        const auto& base=*catalog.find("broom");
        auto model=base.inventory().model;
        model.parts[0].rotation=glm::angleAxis(.35f,glm::vec3(0,0,1));
        const auto custom=base.withCrafting(model,{{{0,.45f,.015f},glm::angleAxis(.7f,glm::vec3(0,1,0))}}).renamed("crafted_broom_1");
        viewer::InventoryContents empty; empty.skills=catalog.skills();
        viewer::PlayerInventory owner(catalog.storage(),empty), replicaInventory(catalog.storage(),empty);
        const auto reference=viewer::backpackStorageReference(catalog.storage().backpack->id);
        require(owner.insert(custom,0,{reference,0,0,0}),"Could not insert crafted inventory fixture");
        multiplayer::Snapshot snapshot;
        multiplayer::PlayerState player; player.id=1; player.slot=static_cast<std::uint8_t>(viewer::craftedSlot);
        player.inventoryState=multiplayer::captureInventory(owner);
        player.equippedCrafted=viewer::captureCraftedItem(custom);
        snapshot.players.push_back(player);
        snapshot.gripPreviews.push_back({{1,2,3},glm::angleAxis(.5f,glm::vec3(1,0,0))});
        const auto decoded=roundTrip(snapshot);
        require(decoded.players[0].equippedCrafted && decoded.players[0].inventoryState->items[0].crafted &&
                decoded.gripPreviews.size()==1 && decoded.gripPreviews[0].position==glm::vec3(1,2,3),
            "Wire transfer discarded the held model, inventory custom data, or ghost grip");
        auto retained=snapshot; retained.players[0].slot=static_cast<std::uint8_t>(viewer::axeSlot);
        require(roundTrip(retained).players[0].equippedCrafted->id==custom.id(),
            "Switching to the axe must retain the custom equipment definition");
        auto missing=snapshot; missing.players[0].equippedCrafted.reset();
        rejects([&] { multiplayer::encode(missing); },"A selected custom slot must include its complete item definition");
        bool changed=false;
        require(multiplayer::applyInventory(replicaInventory,catalog,*decoded.players[0].inventoryState,&changed) && changed,
            "A complete custom definition must apply to a replica inventory");
        const auto* restored=replicaInventory.find(custom.id());
        require(restored && restored->crafted() && restored->prototype()==base.prototype() && restored->grips().size()==1 &&
                std::abs(glm::dot(restored->grips()[0].rotation,custom.grips()[0].rotation))>.99999f,
            "Applying an inventory lost its local grip frame");
        changed=true;
        require(multiplayer::applyInventory(replicaInventory,catalog,*decoded.players[0].inventoryState,&changed) && !changed,
            "An unchanged custom definition must not rebuild the inventory layout every snapshot");
        auto equivalent=*decoded.players[0].inventoryState;
        equivalent.items[0].crafted->grips[0].rotation=-equivalent.items[0].crafted->grips[0].rotation;
        require(multiplayer::applyInventory(replicaInventory,catalog,equivalent,&changed) && !changed,
            "Equivalent quaternion signs must not rebuild the inventory layout");
        auto edited=*decoded.players[0].inventoryState;
        edited.items[0].crafted->grips[0].position.x+=.02f;
        require(multiplayer::applyInventory(replicaInventory,catalog,edited,&changed) && changed &&
                replicaInventory.find(custom.id())->grips()[0].position.x==.02f,
            "An edited grip on the same item ID must update the replica");

        viewer::Weapons weapons(root/"storage/weapons",root/"inventory.toml",true);
        viewer::GroundItems ground(std::make_shared<viewer::PhysicsWorld>()), replicaGround(std::make_shared<viewer::PhysicsWorld>(),true);
        viewer::InventoryDrag drag(owner);
        viewer::CollisionWorld collision(viewer::Model{});
        require(drag.pickUp(custom.id()) && ground.drop(owner,drag,weapons,{}, {0,0,-1},collision),
            "Could not transfer a crafted item from inventory into the world");
        require(!owner.owns(custom.id()),"Dropping a crafted item duplicated ownership");
        multiplayer::Snapshot groundSnapshot; groundSnapshot.groundItems=multiplayer::captureGround(ground,{});
        const auto transferred=roundTrip(groundSnapshot);
        multiplayer::applyGround(replicaGround,catalog,transferred.groundItems);
        require(replicaGround.count()==1 && replicaGround.poses()[0].entry.item.crafted() &&
                replicaGround.poses()[0].entry.item.grips()[0].position==custom.grips()[0].position,
            "A ground item snapshot must retain the authored grip and actual model");

        // Generated world props have no starting catalog entry.
        auto generated=viewer::captureCraftedItem(custom); generated.id="crafted_crate_1"; generated.prototype="world_crate";
        auto generatedInventory=*decoded.players[0].inventoryState;
        generatedInventory.items[0].id=generated.id; generatedInventory.items[0].prototype=generated.prototype;
        generatedInventory.items[0].crafted=generated;
        require(multiplayer::applyInventory(replicaInventory,catalog,generatedInventory),
            "A self-contained crafted world object must not require a catalog prototype");
        auto invalid=snapshot; invalid.players[0].equippedCrafted->grips[0].rotation={0,0,0,0};
        rejects([&] { multiplayer::encode(invalid); },"Wire accepted a zero custom grip orientation");
        invalid=snapshot; invalid.players[0].equippedCrafted->model.parts.resize(65);
        rejects([&] { multiplayer::encode(invalid); },"Wire accepted more than 64 custom model parts");
        invalid=snapshot; invalid.players[0].equippedCrafted->model.parts[0].center.x=std::numeric_limits<float>::quiet_NaN();
        rejects([&] { multiplayer::encode(invalid); },"Wire accepted a nonfinite custom model coordinate");
        invalid=snapshot; invalid.gripPreviews[0].rotation={0,0,0,0};
        rejects([&] { multiplayer::encode(invalid); },"Wire accepted a nonrigid ghost grip");
        invalid=snapshot; invalid.gripPreviews.resize(33);
        rejects([&] { multiplayer::encode(invalid); },"Wire exceeded the shared 32-editor preview limit");
        auto mismatched=*decoded.players[0].inventoryState; mismatched.items[0].crafted->id="different";
        require(!multiplayer::applyInventory(replicaInventory,catalog,mismatched),"Inventory accepted a custom descriptor for another instance");
        multiplayer::Commands command; multiplayer::Command begin; begin.sequence=1; begin.crafting=true;
        begin.inventoryActions.push_back({viewer::InventoryDragActionKind::SetGripBegin,"",""}); command.commands.push_back(begin);
        const auto bytes=multiplayer::encode(command);
        require(std::get<multiplayer::Commands>(multiplayer::decode(bytes.data(),bytes.size())).commands[0].inventoryActions[0].kind==
                viewer::InventoryDragActionKind::SetGripBegin,"Wire discarded the inventory-independent Set grip action");
        std::cout<<"Crafted inventory and network checks passed\n";
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
