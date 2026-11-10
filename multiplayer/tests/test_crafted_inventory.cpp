#include "inventory_state.hpp"
#include "inventory/crafted_item.hpp"
#include "inventory/inventory_config.hpp"
#include "collision_world.hpp"
#include "weapons.hpp"
#include "physics_world.hpp"
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
}
int main() {
    try {
        const std::filesystem::path root(VIEWER_ITEM_ROOT);
        const auto catalog=viewer::loadPlayerInventory(root);
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
