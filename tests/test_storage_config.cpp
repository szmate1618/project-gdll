#include "storage_config.hpp"

#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
struct Fixture {
    std::filesystem::path directory = std::filesystem::current_path() /
        ("storage-fixture-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Fixture() {
        std::filesystem::create_directory(directory);
        for (const auto* name : {"equipment.toml", "backpacks.toml", "loadout.toml"})
            std::filesystem::copy_file(std::filesystem::path(VIEWER_STORAGE_DIR) / name, directory / name);
    }
    ~Fixture() { std::filesystem::remove_all(directory); }
    void write(const char* name, const std::string& text) { std::ofstream(directory / name) << text; }
    void replace(const char* name, const std::string& from, const std::string& to) {
        std::ifstream file(directory / name);
        std::string text{std::istreambuf_iterator<char>(file), {}};
        const auto position = text.find(from);
        require(position != std::string::npos, "Fixture text missing");
        text.replace(position, from.size(), to);
        write(name, text);
    }
    void rejects(const char* file, const char* reason) {
        try { viewer::loadStorageLoadout(directory); }
        catch (const std::exception& error) {
            const std::string message = error.what();
            require(message.find(file) != std::string::npos, "Error omitted source file");
            require(message.find(reason) != std::string::npos, "Error omitted invalid field/reason");
            return;
        }
        throw std::runtime_error("Invalid storage configuration was accepted");
    }
};
void selectedStorage() {
    const auto loadout = viewer::loadStorageLoadout(VIEWER_STORAGE_DIR);
    require(loadout.equipment.size() == 4 && loadout.equipment[0].id == "hip_holster" &&
        loadout.equipment[1].id == "chest_pouch" &&
        loadout.equipment[2].id == "left_shoulder_sling" && loadout.equipment[3].id == "right_shoulder_sling",
        "Selection must resolve only equipped equipment, in order");
    for (std::size_t index = 2; index < 4; ++index) {
        const auto& sling = loadout.equipment[index].slots.at(0);
        require(sling.grid.width == 2 && sling.grid.height == 5 && sling.id == "carry",
            "Shoulder slings must be vertical 2x5 compartments");
        require(sling.y == -1 && sling.x == (index == 2 ? -1 : 1),
            "Shoulder slings must use the top corners of the body storage panel");
    }
    const auto& holster = loadout.equipment[0].slots.at(0);
    require(holster.grid.width == 2 && holster.grid.height == 1 && holster.grid.accessTime == .35,
        "Holster dimensions and timing were lost");
    require(holster.x == -.8 && holster.y == .45, "Panel-relative coordinates were changed");
    require(loadout.backpack && loadout.backpack->id == "tactical_backpack" &&
        loadout.backpack->grid.width == 8 && loadout.backpack->grid.height == 10 &&
        loadout.backpack->slots.size() == 4, "Starter tactical pack must have an 8x10 grid and four external pockets");
    require(loadout.backpack->grid.depthTimePerRow == .12 && loadout.backpack->grid.accessTime == 2.0,
        "Backpack depth/access times were lost");
    Fixture fixture;
    fixture.write("loadout.toml", "schema_version=1\nbackpack='hiking_backpack'\nequipment=['jacket']\n");
    const auto alternative = viewer::loadStorageLoadout(fixture.directory);
    require(alternative.backpack->grid.width == 10 && alternative.backpack->grid.height == 14 &&
        alternative.backpack->slots.size() == 3 && alternative.equipment[0].slots.size() == 2,
        "Alternate catalog selection must change storage capacity and compartments");
    require(alternative.backpack->slots[0].x == -1 && alternative.backpack->slots[1].x == 1 &&
        alternative.backpack->slots[2].y == -1, "Edge coordinates must remain panel-relative");
    fixture.write("loadout.toml", "schema_version=1\nequipment=[]\n");
    const auto empty = viewer::loadStorageLoadout(fixture.directory);
    require(empty.equipment.empty() && !empty.backpack, "Unequipped loadout must not invent storage");
}
void invalidReferences() {
    Fixture fixture;
    fixture.write("loadout.toml", "schema_version=1\nbackpack='missing'\nequipment=[]\n");
    fixture.rejects("loadout.toml", "unknown id missing");
    fixture.write("loadout.toml", "schema_version=1\nequipment=['missing']\n");
    fixture.rejects("loadout.toml", "unknown id missing");
    fixture.write("loadout.toml", "schema_version=1\nequipment=['hip_holster','hip_holster']\n");
    fixture.rejects("loadout.toml", "duplicate id");
    fixture.write("loadout.toml", "schema_version=1\nequipment=[7]\n");
    fixture.rejects("loadout.toml", "string IDs");
    fixture.write("loadout.toml", "schema_version=2\nequipment=[]\n");
    fixture.rejects("loadout.toml", "schema_version");
}
void invalidCatalogs() {
    for (const auto& value : {"0", "17", "2.5", "'four'"}) {
        Fixture fixture;
        fixture.replace("backpacks.toml", "width = 4", std::string("width = ") + value);
        fixture.rejects("backpacks.toml", "width");
    }
    for (const auto& value : {"-0.1", "nan", "inf"}) {
        Fixture fixture;
        fixture.replace("equipment.toml", "access_time = 0.35", std::string("access_time = ") + value);
        fixture.rejects("equipment.toml", "access_time");
    }
    for (const auto& value : {"1.01", "-1.01", "nan"}) {
        Fixture fixture;
        fixture.replace("equipment.toml", "x = -0.8", std::string("x = ") + value);
        fixture.rejects("equipment.toml", "coordinates");
    }
    {
        Fixture fixture;
        fixture.replace("backpacks.toml", "depth_time_per_row = 0.08", "depth_time_per_row = -1");
        fixture.rejects("backpacks.toml", "depth_time_per_row");
    }
    {
        Fixture fixture;
        fixture.replace("equipment.toml", "id = \"jacket\"", "id = \"hip_holster\"");
        fixture.rejects("equipment.toml", "duplicate id");
    }
    {
        Fixture fixture;
        fixture.replace("equipment.toml", "id = \"right_pocket\"", "id = \"left_pocket\"");
        fixture.rejects("equipment.toml", "duplicate id");
    }
    {
        Fixture fixture;
        std::filesystem::remove(fixture.directory / "equipment.toml");
        fixture.rejects("equipment.toml", "");
    }
}
} // namespace
int main() {
    try {
        selectedStorage(); invalidReferences(); invalidCatalogs();
        std::cout << "Storage configuration checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
