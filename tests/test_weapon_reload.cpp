#include "weapon_reload.hpp"
#include "inventory/player_inventory.hpp"
#include "camera.hpp"
#include "collision_world.hpp"
#include "dropped_weapons.hpp"
#include "physics_world.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void near(double actual, double expected, const char* message) { require(std::abs(actual - expected) < 1e-9, message); }
viewer::Weapons weapons() {
    return viewer::Weapons(std::filesystem::path(VIEWER_ITEM_ROOT) / "storage/weapons",
        std::filesystem::path(VIEWER_ITEM_ROOT) / "inventory.toml", true);
}
void fire(viewer::Weapons& weapon, int count) {
    for (int i = 0; i < count; ++i) {
        weapon.trigger(false); weapon.trigger(true);
        require(weapon.fire(i), "Reload fixture must consume its configured magazine");
    }
}
void addStack(viewer::InventoryContents& contents, const std::string& id, const std::string& type,
              int quantity, const std::string& storage, int x, int y) {
    viewer::ItemInventoryConfig config;
    config.name = id; config.type = "ammo"; config.width = config.height = 1;
    config.ammoType = type; config.stackable = true; config.maxStack = 100;
    contents.owned.emplace(id, viewer::Item(id, config, {}, {"ammo"}));
    contents.quantities[id] = quantity;
    contents.placements[id] = {storage, x, y};
}
struct Stack { std::string id; int quantity; double access; std::string type = "pistol_rounds"; };
viewer::PlayerInventory inventory(std::initializer_list<Stack> stacks) {
    viewer::StorageLoadout storage;
    viewer::InventoryContents contents;
    int slot = 0;
    for (const auto& stack : stacks) {
        const auto id = "slot" + std::to_string(slot++);
        storage.equipment.push_back({id, id, {{"main", "MAIN", 0, 0, {1, 1, stack.access}}}});
        addStack(contents, stack.id, stack.type, stack.quantity, viewer::bodyStorageReference(id, "main"), 0, 0);
    }
    return viewer::PlayerInventory(storage, contents);
}
void timingAndInteraction() {
    auto gun = weapons();
    gun.equip(2);
    const auto dropped = gun.discard();
    require(dropped.has_value() && gun.equip(0), "Reload transfer fixture must leave a vacant weapon slot");
    fire(gun, 5);
    auto ammo = inventory({{"small", 3, .5}, {"exact", 5, .5}, {"large", 50, .5}, {"slow", 5, 2}});
    gun.syncAmmo(ammo);
    require(gun.equipped().reserve == 63, "Reserve display must sum inventory stacks");
    viewer::WeaponReload reload;
    require(!reload.begin(gun, ammo, std::numeric_limits<double>::quiet_NaN()), "Invalid reload start times must be rejected");
    require(reload.begin(gun, ammo, 10) && reload.pending() && gun.reloading(), "A stored matching stack must start a reload");
    const double duration = gun.equipped().config.reloadTimeSeconds + .5;
    near(reload.duration(), duration, "Reload must combine configured weapon and compartment times");
    near(reload.remaining(10.2), duration - .2, "Reload countdown must use simulation seconds");
    require(!reload.begin(gun, ammo, 11) && !gun.equip(1) && !gun.discard() && !gun.pickUp(*dropped),
        "Reloading must block overlapping reload, switching and weapon transfers");
    gun.trigger(false); gun.trigger(true);
    require(!gun.fire(11), "A loaded weapon must not fire during reloading");
    require(reload.update(gun, ammo, 10 + duration - .001) == 0 && gun.equipped().loaded == 7 && ammo.quantity("exact") == 5,
        "Magazine and source quantity must remain unchanged until completion");
    gun.trigger(true);
    require(reload.update(gun, ammo, 10 + duration) == 5 && gun.equipped().loaded == 12 && ammo.quantity("exact") == 0,
        "The smallest equally accessible stack sufficient for a full reload must be consumed");
    require(ammo.owns("exact") && ammo.locationOf("exact")->placement.has_value() && ammo.items().size() == 4,
        "A zero quantity stack must retain its item and storage footprint");
    require(ammo.quantity("small") == 3 && ammo.quantity("large") == 50 && gun.equipped().reserve == 58,
        "A reload must consume only its selected source and refresh the reserve display");
    require(!reload.pending() && !gun.reloading() && !gun.fire(100) && reload.update(gun, ammo, 100) == 0,
        "Completion must happen once and discard held fire input");
    require(!reload.begin(gun, ammo, 100), "A full magazine must not start a reload");
    require(gun.pickUp(*dropped), "Completing the reload must allow equippable pickups again");
    gun.equip(viewer::punchSlot);
    require(!reload.begin(gun, ammo, 100), "Fists must not reload");
}
void sourceSelectionAndPartialLoads() {
    {
        auto gun = weapons(); fire(gun, 7);
        auto ammo = inventory({{"partial", 6, .5}, {"enough", 8, .5}, {"excess", 10, .5}, {"wrong", 100, 0, "smg_rounds"}});
        viewer::WeaponReload reload;
        require(reload.begin(gun, ammo, 10), "Tie fixture must begin");
        require(reload.update(gun, ammo, 100) == 7 && ammo.quantity("enough") == 1 && ammo.quantity("excess") == 10,
            "An equally accessible sufficient stack must beat insufficient and larger sufficient stacks");
    }
    {
        auto gun = weapons(); fire(gun, 7);
        auto ammo = inventory({{"small", 3, .5}, {"largest", 6, .5}});
        viewer::WeaponReload reload;
        require(reload.begin(gun, ammo, 10) && reload.update(gun, ammo, 100) == 6 && ammo.quantity("small") == 3,
            "When no equally fast stack can fill the magazine, choose the largest");
        require(gun.equipped().loaded == 11, "A single reload must not chain through multiple ammo stacks");
    }
    {
        auto gun = weapons(); fire(gun, 12);
        auto ammo = inventory({{"empty", 0, 0}, {"fast", 2, .25}, {"slow", 12, .5}});
        viewer::WeaponReload reload;
        require(reload.begin(gun, ammo, 20) && reload.update(gun, ammo, 100) == 2 && ammo.quantity("slow") == 12,
            "Access time takes priority over filling the magazine, and empty stacks are skipped");
        require(gun.equipped().loaded == 2 && ammo.owns("fast"), "A partial reload must preserve the empty source stack");
    }
    {
        auto gun = weapons(); fire(gun, 1);
        auto ammo = inventory({{"empty", 0, 0}, {"wrong", 10, 0, "smg_rounds"}});
        viewer::WeaponReload reload;
        require(!reload.begin(gun, ammo, 10) && !gun.reloading(), "An empty or wrong ammo stack cannot start a reload");
    }
}
void backpackAccess() {
    viewer::StorageLoadout storage;
    storage.backpack = viewer::StorageBackpack{};
    storage.backpack->id = "test";
    storage.backpack->grid = {3, 4, 2, {}, viewer::BackpackAccessConfig{.5, .25}};
    storage.equipment.push_back({"pocket", "POCKET", {{"main", "MAIN", 0, 0, {1, 1, 1.9}}}});
    viewer::InventoryContents contents;
    const auto pack = viewer::backpackStorageReference("test");
    addStack(contents, "blocked", "pistol_rounds", 20, pack, 0, 1);
    addStack(contents, "blocker", "smg_rounds", 1, pack, 0, 0);
    addStack(contents, "supported", "pistol_rounds", 20, pack, 1, 0);
    addStack(contents, "support", "smg_rounds", 1, pack, 1, 3);
    addStack(contents, "pocket", "pistol_rounds", 20, "body/pocket/main", 0, 0);
    viewer::PlayerInventory ammo(storage, contents);
    auto gun = weapons(); fire(gun, 1);
    viewer::WeaponReload reload;
    require(reload.begin(gun, ammo, 10), "Backpack fixture must begin");
    near(reload.duration(), gun.equipped().config.reloadTimeSeconds + 1.75,
        "Backpack support must reduce access time before comparing sources");
    require(reload.update(gun, ammo, 100) == 1 && ammo.quantity("supported") == 19 && ammo.quantity("blocked") == 20 &&
        ammo.quantity("pocket") == 20, "Obstruction and support must determine which matching stack is fastest");
}
void grenadeHandLoading() {
    auto gun = weapons();
    require(gun.equip(4) && gun.equipped().loaded == 1 && gun.equipped().config.magazineCapacity == 1,
        "Only one grenade may be held ready");
    auto ammo = inventory({{"near", 1, .2, "he_grenades"}, {"far", 4, 2, "he_grenades"},
        {"other", 20, 0, "pistol_rounds"}});
    fire(gun, 1);
    require(gun.equipped().loaded == 0, "Throwing must empty the grenade hand");
    viewer::WeaponReload reload;
    require(reload.begin(gun, ammo, 0), "The successful throw workflow must start loading the next matching grenade");
    near(reload.duration(), gun.equipped().config.reloadTimeSeconds + .2,
        "Grenade loading must use the most accessible matching grenade stack");
    require(reload.update(gun, ammo, 100) == 1 && gun.equipped().loaded == 1 && ammo.quantity("near") == 0 &&
        ammo.quantity("far") == 4 && ammo.owns("near"), "Grenade autoload must consume one reserve grenade and retain its empty stack");
}
void reloadingCannotCreateDroppedCopies() {
    auto gun = weapons();
    fire(gun, 1);
    auto ammo = inventory({{"rounds", 10, .5}});
    viewer::WeaponReload reload;
    require(reload.begin(gun, ammo, 10), "Discard regression must start a reload");
    std::array<viewer::Model, viewer::weaponIds.size()> models;
    for (auto& model : models) {
        model.boundsMin = glm::vec3(-.1f);
        model.boundsMax = glm::vec3(.1f);
        model.primitives.emplace_back();
    }
    auto physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::DroppedWeapons dropped(physics, models);
    viewer::Model emptyScene;
    viewer::CollisionWorld world(emptyScene);
    viewer::Camera camera;
    for (int attempt = 0; attempt < 3; ++attempt)
        require(!dropped.discard(gun, camera, world, {}), "Reloading must reject physical weapon discards");
    require(dropped.count() == 0 && gun.equipped().owned && gun.equipped().loaded == 11,
        "Rejected physical discards must not create copies or change loaded weapon ownership");
    reload.update(gun, ammo, 100);
    require(dropped.discard(gun, camera, world, {}) && dropped.count() == 1 && !gun.state(0).owned,
        "After reload completion, discarding must transfer ownership to exactly one physical item");
}
}
int main() {
    try {
        timingAndInteraction(); sourceSelectionAndPartialLoads(); backpackAccess(); grenadeHandLoading(); reloadingCannotCreateDroppedCopies();
        std::cout << "Timed inventory reload, source selection and grenade loading checks passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
