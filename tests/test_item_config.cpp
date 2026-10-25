#include "item_config.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
struct Fixture {
    std::filesystem::path root = std::filesystem::current_path() /
        ("item-fixture-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Fixture() {
        std::filesystem::create_directory(root);
        for (const auto* folder : {"items", "weapons"})
            std::filesystem::copy(std::filesystem::path(VIEWER_ITEM_ROOT) / folder, root / folder,
                std::filesystem::copy_options::recursive);
        std::filesystem::copy_file(std::filesystem::path(VIEWER_ITEM_ROOT) / "inventory.toml", root / "inventory.toml");
    }
    ~Fixture() { std::filesystem::remove_all(root); }
    void inventory(const char* text) { std::ofstream(root / "inventory.toml") << text; }
    void replace(const char* filename, const std::string& from, const std::string& to) {
        std::ifstream file(root / filename);
        std::string text{std::istreambuf_iterator<char>(file), {}};
        const auto position = text.find(from);
        require(position != std::string::npos, "Fixture text missing");
        text.replace(position, from.size(), to);
        std::ofstream(root / filename) << text;
    }
    void rejects(const char* file, const char* reason) {
        try { viewer::loadInventoryItems(root); }
        catch (const std::exception& error) {
            const std::string message(error.what());
            require(message.find(file) != std::string::npos, "Error omitted filename");
            require(message.find(reason) != std::string::npos, "Error omitted invalid field");
            return;
        }
        throw std::runtime_error("Invalid item configuration was accepted");
    }
};
void starterInventory() {
    const auto inventory = viewer::loadInventoryItems(VIEWER_ITEM_ROOT);
    const std::array<const char*, 5> expected{"pistol", "submachine_gun", "shotgun", "sniper", "he_grenade"};
    require(inventory.owned.size() == 5, "All five weapons must have owned item definitions");
    for (std::size_t slot = 0; slot < expected.size(); ++slot) {
        require(inventory.hotbar[slot] == expected[slot], "Weapon hotbar order must match slots 0–4");
        const auto& item = inventory.owned.at(expected[slot]);
        require(item.weapon == expected[slot] && item.type == "weapon", "Item gameplay reference was lost");
        require(item.width > 0 && item.height > 0 && !item.tags.empty(), "Item footprint/tags missing");
    }
    for (std::size_t slot = 5; slot < inventory.hotbar.size(); ++slot)
        require(inventory.hotbar[slot].empty(), "Slots 5–9 must start unassigned");
    const auto& pistol = inventory.owned.at("pistol");
    require(pistol.width == 1 && pistol.height == 1, "Pistol must have a 1x1 cell footprint");
    const auto& sniper = inventory.owned.at("sniper");
    require(sniper.width == 5 && sniper.height == 2, "Sniper footprint must come from its item descriptor");
    Fixture fixture;
    fixture.inventory("[weapons]\npistol=0\n[hotbar]\n9='pistol'\n");
    const auto partial = viewer::loadInventoryItems(fixture.root);
    require(partial.owned.size() == 1 && partial.hotbar[9] == "pistol" && partial.hotbar[0].empty(),
        "Ownership must follow the inventory, including an owned empty firearm, and allow sparse shortcuts");
    fixture.replace("items/pistol.toml", "width = 1", "width = 3");
    require(viewer::loadInventoryItems(fixture.root).owned.at("pistol").width == 3,
        "Editing the item descriptor must change its grid footprint independently of gameplay");
    fixture.inventory("[weapons]\n");
    require(viewer::loadInventoryItems(fixture.root).owned.empty(), "No ownership entries must mean no owned items");
}
void invalidInventory() {
    Fixture fixture;
    for (const auto* text : {
        "[weapons]\npistol=1\n[hotbar]\n0='sniper'\n",
        "[weapons]\npistol=1\n[hotbar]\n0=5\n",
        "[weapons]\npistol=1\n[hotbar]\n10='pistol'\n",
        "[weapons]\npistol=1\n[hotbar]\n'01'='pistol'\n",
        "[weapons]\npistol=1\n[hotbar]\n'-1'='pistol'\n",
        "hotbar=[]\n[weapons]\npistol=1\n"}) {
        fixture.inventory(text);
        fixture.rejects("inventory.toml", "hotbar");
    }
    fixture.inventory("[weapons]\npistol=-1\n");
    fixture.rejects("inventory.toml", "round count");
    fixture.inventory("[weapons]\n'../pistol'=1\n");
    fixture.rejects("inventory.toml", "weapon ID");
    for (const auto* text : {
        "[weapons]\npistol=1\n[placements.sniper]\nstorage='body/hip_holster/holster'\nx=0\ny=0\n",
        "[weapons]\npistol=1\n[placements.pistol]\nstorage='body/hip_holster/holster'\nx=-1\ny=0\n",
        "[weapons]\npistol=1\n[placements.pistol]\nstorage='body/hip_holster/holster'\nx=0.5\ny=0\n",
        "[weapons]\npistol=1\n[placements.pistol]\nstorage='body/hip_holster/holster'\nx=0\ny=16\n"}) {
        fixture.inventory(text);
        fixture.rejects("inventory.toml", "placements");
    }

}
void invalidDefinitions() {
    for (const auto* value : {"0", "17", "2.5", "'two'"}) {
        Fixture fixture;
        fixture.replace("items/pistol.toml", "width = 1", std::string("width = ") + value);
        fixture.rejects("pistol.toml", "width");
    }
    {
        Fixture fixture;
        fixture.replace("items/pistol.toml", "height = 1", "height = 0");
        fixture.rejects("pistol.toml", "height");
    }
    {
        Fixture fixture;
        fixture.replace("items/pistol.toml", "schema_version = 1", "schema_version = 2");
        fixture.rejects("pistol.toml", "schema_version");
    }
    {
        Fixture fixture;
        fixture.replace("items/pistol.toml", "weapon = \"pistol\"", "weapon = \"sniper\"");
        fixture.rejects("pistol.toml", "matching item/weapon IDs");
    }
    {
        Fixture fixture;
        fixture.replace("items/pistol.toml", "[\"handgun\", \"ranged\"]", "[7]");
        fixture.rejects("pistol.toml", "tags");
    }
    {
        Fixture fixture;
        std::filesystem::remove(fixture.root / "weapons/pistol.toml");
        fixture.rejects("pistol.toml", "gameplay definition");
    }
    {
        Fixture fixture;
        std::filesystem::remove(fixture.root / "items/pistol.toml");
        fixture.rejects("pistol.toml", "");
    }
}
} // namespace
int main() {
    try {
        starterInventory(); invalidInventory(); invalidDefinitions();
        std::cout << "Item definitions and inventory shortcuts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
