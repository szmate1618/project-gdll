#include "inventory_config.hpp"
#include "item_config.hpp"
#include <toml++/toml.hpp>
#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace viewer {
namespace {
using Path = std::filesystem::path;
[[noreturn]] void invalid(const Path& file, const std::string& reason) {
    throw std::runtime_error(file.string() + ": " + reason);
}
toml::table read(const Path& file) {
    try { return toml::parse_file(file.string()); }
    catch (const toml::parse_error& error) { invalid(file, std::string(error.description())); }
}
template<class T>
T required(const toml::table& table, const char* key, const Path& file) {
    auto value = table[key].value<T>();
    if (!value) invalid(file, std::string("missing or invalid ") + key);
    return *value;
}
std::string label(const toml::table& table, const char* key, const Path& file) {
    auto value = required<std::string>(table, key, file);
    if (value.empty()) invalid(file, std::string(key) + " must not be empty");
    return value;
}
} // namespace

InventoryContents loadInventoryContents(const Path& root) {
    const auto file = root / "inventory.toml";
    const auto table = read(file);
    const auto* weapons = table["weapons"].as_table();
    if (!weapons) invalid(file, "expected [weapons] ownership table");
    InventoryContents result;
    for (const auto& [key, node] : *weapons) {
        const std::string id(key.str());
        if (id.empty() || !std::all_of(id.begin(), id.end(), [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        })) invalid(file, "invalid weapon ID " + id);
        const auto count = node.value<std::int64_t>();
        if (!node.is_integer() || !count || *count < 0) invalid(file, "invalid loaded round count for " + id);
        result.owned.emplace(id, loadItem(root / "storage/weapons", id));
    }
    if (table.contains("placements")) {
        const auto* placements = table["placements"].as_table();
        if (!placements) invalid(file, "placements must be a table keyed by owned item ID");
        for (const auto& [key, node] : *placements) {
            const std::string id(key.str());
            const auto* placement = node.as_table();
            if (!placement || result.owned.find(id) == result.owned.end())
                invalid(file, "placements." + id + " must describe an owned item");
            ItemPlacement entry;
            entry.storage = label(*placement, "storage", file);
            const auto coordinate = [&](const char* axis) {
                if (!(*placement)[axis].is_integer())
                    invalid(file, "placements." + id + "." + axis + " must be an integer cell coordinate");
                const auto value = required<std::int64_t>(*placement, axis, file);
                if (value < 0 || value > 15)
                    invalid(file, "placements." + id + "." + axis + " must be between 0 and 15 cells");
                return static_cast<int>(value);
            };
            entry.x = coordinate("x");
            entry.y = coordinate("y");
            if (placement->contains("rotation")) {
                if (!(*placement)["rotation"].is_integer())
                    invalid(file, "placements." + id + ".rotation must be an integer angle");
                const auto rotation = required<std::int64_t>(*placement, "rotation", file);
                if (rotation < 0 || rotation > 270 || rotation % 90 != 0)
                    invalid(file, "placements." + id + ".rotation must be 0, 90, 180 or 270");
                entry.rotation = static_cast<int>(rotation);
            }
            result.placements.emplace(id, std::move(entry));
        }
    }
    if (table.contains("hotbar")) {
        const auto* hotbar = table["hotbar"].as_table();
        if (!hotbar) invalid(file, "hotbar must be a table of slots 0–9");
        for (const auto& [key, node] : *hotbar) {
            const auto slot = key.str();
            if (slot.size() != 1 || slot[0] < '0' || slot[0] > '9') invalid(file, "hotbar slots must be 0–9");
            const auto id = node.value<std::string>();
            if (!id || result.owned.find(*id) == result.owned.end())
                invalid(file, "hotbar slot " + std::string(slot) + " must reference an owned item ID");
            result.hotbar[static_cast<std::size_t>(slot[0] - '0')] = *id;
        }
    }
    return result;
}
PlayerInventory loadPlayerInventory(const Path& root) {
    return PlayerInventory(loadStorageLoadout(root / "storage"), loadInventoryContents(root));
}
} // namespace viewer
