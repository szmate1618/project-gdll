#include "item_config.hpp"

#include <algorithm>
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
        std::filesystem::create_directories(root / "storage");
        std::filesystem::copy(std::filesystem::path(VIEWER_ITEM_ROOT) / "storage/weapons", root / "storage/weapons",
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
        const std::string category = slot == 0 ? "handgun" : slot == 4 ? "grenade" : "long_gun";
        require(std::find(item.tags.begin(), item.tags.end(), category) != item.tags.end(),
            "Weapon category must come from its comma-separated tags file");
    }
    for (std::size_t slot = 5; slot < inventory.hotbar.size(); ++slot)
        require(inventory.hotbar[slot].empty(), "Slots 5–9 must start unassigned");
    const auto& pistol = inventory.owned.at("pistol");
    require(pistol.width == 2 && pistol.height == 1, "Pistol must have a 2x1 cell footprint");
    const auto& sniper = inventory.owned.at("sniper");
    require(sniper.width == 5 && sniper.height == 2, "Sniper footprint must come from its item descriptor");
    Fixture fixture;
    fixture.inventory("[weapons]\npistol=0\n[hotbar]\n9='pistol'\n");
    const auto partial = viewer::loadInventoryItems(fixture.root);
    require(partial.owned.size() == 1 && partial.hotbar[9] == "pistol" && partial.hotbar[0].empty(),
        "Ownership must follow the inventory, including an owned empty firearm, and allow sparse shortcuts");
    fixture.replace("storage/weapons/pistol.inventory.toml", "width = 2", "width = 3");
    require(viewer::loadInventoryItems(fixture.root).owned.at("pistol").width == 3,
        "Editing the item descriptor must change its grid footprint independently of gameplay");
    std::ofstream(fixture.root / "storage/weapons/pistol.tags.txt") << "  handgun ,\n ranged , utility \n";
    require(viewer::loadInventoryItems(fixture.root).owned.at("pistol").tags ==
        std::vector<std::string>{"handgun", "ranged", "utility"}, "Tag parsing must trim whitespace and preserve order");
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
        fixture.replace("storage/weapons/pistol.inventory.toml", "width = 2", std::string("width = ") + value);
        fixture.rejects("pistol.inventory.toml", "width");
    }
    {
        Fixture fixture;
        fixture.replace("storage/weapons/pistol.inventory.toml", "height = 1", "height = 0");
        fixture.rejects("pistol.inventory.toml", "height");
    }
    {
        Fixture fixture;
        fixture.replace("storage/weapons/pistol.inventory.toml", "schema_version = 2", "schema_version = 99");
        fixture.rejects("pistol.inventory.toml", "schema_version");
    }
    {
        Fixture fixture;
        fixture.replace("storage/weapons/pistol.inventory.toml", "weapon = \"pistol\"", "weapon = \"sniper\"");
        fixture.rejects("pistol.inventory.toml", "matching item/weapon IDs");
    }
    for (const auto* tags : {"handgun, ,ranged", "handgun,handgun", "handgun,", "handgun,wrong tag"}) {
        Fixture fixture;
        std::ofstream(fixture.root / "storage/weapons/pistol.tags.txt") << tags;
        fixture.rejects("pistol.tags.txt", "tag");
    }
    {
        Fixture fixture;
        std::filesystem::remove(fixture.root / "storage/weapons/pistol.tags.txt");
        fixture.rejects("pistol.tags.txt", "read");
    }
    {
        Fixture fixture;
        std::filesystem::remove(fixture.root / "storage/weapons/pistol.gameplay.toml");
        fixture.rejects("pistol.inventory.toml", "gameplay definition");
    }
    {
        Fixture fixture;
        std::filesystem::remove(fixture.root / "storage/weapons/pistol.inventory.toml");
        fixture.rejects("pistol.inventory.toml", "");
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
