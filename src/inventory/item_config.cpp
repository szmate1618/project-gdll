#include "item_config.hpp"
#include "item_model_config.hpp"

#include <toml++/toml.hpp>
#include <algorithm>
#include <cstdint>
#include <fstream>
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
void appendTag(std::vector<std::string>& tags, std::string tag, const Path& file) {
    if (tag.empty() || !std::all_of(tag.begin(), tag.end(), [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        })) invalid(file, "tags must use nonempty lowercase letters, digits and underscores");
    if (std::find(tags.begin(), tags.end(), tag) != tags.end()) invalid(file, "duplicate tag " + tag);
    tags.push_back(std::move(tag));
}
std::vector<std::string> readTags(const Path& file) {
    std::ifstream input(file, std::ios::binary);
    if (!input) invalid(file, "cannot read tag file");
    const std::string text{std::istreambuf_iterator<char>(input), {}};
    std::vector<std::string> result;
    std::size_t start = 0;
    while (true) {
        const auto end = text.find(',', start);
        auto tag = text.substr(start, end == std::string::npos ? end : end - start);
        const auto first = tag.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) invalid(file, "empty tag in comma-separated list");
        tag = tag.substr(first, tag.find_last_not_of(" \t\r\n") - first + 1);
        appendTag(result, std::move(tag), file);
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return result;
}
std::vector<std::string> readTags(const toml::table& table, const Path& file, const Path& sidecar) {
    if (!table.contains("tags")) return readTags(sidecar);
    const auto* entries = table["tags"].as_array();
    if (!entries || entries->empty()) invalid(file, "tags must be a nonempty array of strings");
    std::vector<std::string> tags;
    for (const auto& entry : *entries) {
        const auto tag = entry.value<std::string>();
        if (!tag) invalid(file, "tags must be a nonempty array of strings");
        appendTag(tags, *tag, file);
    }
    return tags;
}
} // namespace

Item loadItem(const Path& directory, const std::string& id) {
    if (id.empty() || !std::all_of(id.begin(), id.end(), [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        })) invalid(directory, "invalid item ID " + id);
    const auto file = directory / (id + ".inventory.toml");
    const auto table = read(file);
    if (!table["schema_version"].is_integer() || required<int>(table, "schema_version", file) != 2)
        invalid(file, "schema_version must be integer 2");
    ItemInventoryConfig item;
    item.acoustics = id;
    const auto itemId = label(table, "id", file);
    item.name = label(table, "name", file);
    item.type = label(table, "type", file);
    if (itemId != id) invalid(file, "expected matching item/weapon IDs");
    WeaponConfig gameplay;
    if (item.type == "weapon") {
        const auto weapon = label(table, "weapon", file);
        if (weapon != id) invalid(file, "expected matching item/weapon IDs and type = 'weapon'");
        if (!std::filesystem::is_regular_file(directory / (weapon + ".gameplay.toml")))
            invalid(file, "missing gameplay definition for weapon " + weapon);
        gameplay = loadWeaponConfig(directory / (id + ".gameplay.toml"), id);
    } else if (item.type == "ammo" || item.type == "medical") {
        if (item.type == "ammo") item.ammoType = label(table, "ammo_type", file);
        item.stackable = required<bool>(table, "stackable", file);
        if (!item.stackable) invalid(file, "consumables must be stackable");
    } else if (item.type != "equipment") invalid(file, "type must be 'weapon', 'ammo', 'medical' or 'equipment'");
    if (item.type == "equipment" && table.contains("stackable")) item.stackable = required<bool>(table, "stackable", file);
    if (table.contains("quantity_unit")) item.quantityUnit = label(table, "quantity_unit", file);
    if (item.quantityUnit != "count" && item.quantityUnit != "m") invalid(file, "quantity_unit must be 'count' or 'm'");
    if (item.quantityUnit == "m" && (item.type != "equipment" || !item.stackable))
        invalid(file, "quantity_unit = 'm' requires stackable equipment");
    if (item.stackable) {
        if (item.quantityUnit == "count" && !table["max_stack"].is_integer())
            invalid(file, "max_stack must be a positive integer");
        const auto amount = table["max_stack"].value<double>();
        const auto quantity = amount ? itemQuantityFromDisplay(item, *amount) : std::nullopt;
        if (!quantity || *quantity < 1) invalid(file, "max_stack must be positive and fit the item's quantity precision and range");
        item.maxStack = *quantity;
    }
    const auto dimension = [&](const char* key) {
        if (!table[key].is_integer()) invalid(file, std::string(key) + " must be an integer cell count");
        const auto value = required<std::int64_t>(table, key, file);
        if (value < 1 || value > 16) invalid(file, std::string(key) + " must be between 1 and 16 cells");
        return static_cast<int>(value);
    };
    item.width = dimension("width");
    item.height = dimension("height");
    if (table.contains("model")) item.model = loadItemModelConfig(file);
    return Item(id, std::move(item), std::move(gameplay),
        readTags(table, file, directory / (id + ".tags.txt")));
}
} // namespace viewer
