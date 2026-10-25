#include "storage_config.hpp"

#include <toml++/toml.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <unordered_set>

namespace viewer {
namespace {
using Path = std::filesystem::path;

[[noreturn]] void invalid(const Path& file, const std::string& field, const std::string& reason) {
    throw std::runtime_error(file.string() + ": " + field + " " + reason);
}

template<class T>
T required(const toml::table& table, const std::string& key, const Path& file) {
    const auto value = table[key].value<T>();
    if (!value) invalid(file, key, "is missing or has the wrong type");
    return *value;
}

std::string label(const toml::table& table, const char* key, const Path& file) {
    auto result = required<std::string>(table, key, file);
    if (result.empty()) invalid(file, key, "must not be empty");
    return result;
}

toml::table read(const Path& file) {
    try {
        auto table = toml::parse_file(file.string());
        if (!table["schema_version"].is_integer() || required<int>(table, "schema_version", file) != 1)
            invalid(file, "schema_version", "must be integer 1");
        return table;
    } catch (const toml::parse_error& error) {
        // toml++ may omit the path for an unreadable file.
        throw std::runtime_error(file.string() + ": " + std::string(error.description()));
    }
}

double time(const toml::table& table, const char* key, const Path& file) {
    const double result = required<double>(table, key, file);
    if (!std::isfinite(result) || result < 0) invalid(file, key, "must be finite and nonnegative (seconds)");
    return result;
}

StorageGrid grid(const toml::table& table, const Path& file) {
    StorageGrid result;
    const auto dimension = [&](const char* key) {
        if (!table[key].is_integer()) invalid(file, key, "must be an integer");
        const auto value = required<std::int64_t>(table, key, file);
        if (value < 1 || value > 16) invalid(file, key, "must be between 1 and 16 cells");
        return static_cast<int>(value);
    };
    result.width = dimension("width");
    result.height = dimension("height");
    result.accessTime = time(table, "access_time", file);
    if (table.contains("allowed_tags")) {
        const auto* tags = table["allowed_tags"].as_array();
        if (!tags) invalid(file, "allowed_tags", "must be an array of tag strings");
        std::unordered_set<std::string> seen;
        for (const auto& node : *tags) {
            const auto tag = node.value<std::string>();
            if (!tag || tag->empty() || !std::all_of(tag->begin(), tag->end(), [](char c) {
                    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
                })) invalid(file, "allowed_tags", "must contain nonempty lowercase tag identifiers");
            if (!seen.insert(*tag).second) invalid(file, "allowed_tags", "contains duplicate tag " + *tag);
            result.allowedTags.push_back(*tag);
        }
    }
    return result;
}

const toml::array& array(const toml::table& table, const char* key, const Path& file) {
    auto* result = table[key].as_array();
    if (!result) invalid(file, key, "must be an array");
    return *result;
}

StorageEquipment equipment(const toml::table& table, const Path& file) {
    StorageEquipment result;
    result.id = label(table, "id", file);
    result.name = label(table, "name", file);
    std::unordered_set<std::string> ids;
    for (const auto& node : array(table, "slots", file)) {
        const auto* slot = node.as_table();
        if (!slot) invalid(file, result.id + ".slots", "must contain tables");
        StorageSlot entry;
        entry.id = label(*slot, "id", file);
        entry.name = label(*slot, "name", file);
        if (!ids.insert(entry.id).second) invalid(file, result.id + ".slots", "contains duplicate id " + entry.id);
        entry.x = required<double>(*slot, "x", file);
        entry.y = required<double>(*slot, "y", file);
        if (!std::isfinite(entry.x) || !std::isfinite(entry.y) ||
            std::abs(entry.x) > 1 || std::abs(entry.y) > 1)
            invalid(file, result.id + "." + entry.id, "coordinates must be finite and within [-1, 1]");
        entry.grid = grid(*slot, file);
        result.slots.push_back(std::move(entry));
    }
    return result;
}

template<class T, class Parse>
std::vector<T> catalog(const Path& file, const char* key, Parse parse) {
    const auto table = read(file);
    std::vector<T> result;
    std::unordered_set<std::string> ids;
    for (const auto& node : array(table, key, file)) {
        const auto* entry = node.as_table();
        if (!entry) invalid(file, key, "must contain tables");
        auto item = parse(*entry, file);
        if (!ids.insert(item.id).second) invalid(file, key, "contains duplicate id " + item.id);
        result.push_back(std::move(item));
    }
    return result;
}

template<class T>
const T& resolve(const std::vector<T>& definitions, const std::string& id, const Path& file, const char* key) {
    for (const auto& definition : definitions)
        if (definition.id == id) return definition;
    invalid(file, key, "references unknown id " + id);
}
} // namespace

StorageLoadout loadStorageLoadout(const Path& directory) {
    const auto equipmentTypes = catalog<StorageEquipment>(directory / "equipment.toml", "equipment", equipment);
    const auto backpackTypes = catalog<StorageBackpack>(directory / "backpacks.toml", "backpacks",
        [](const toml::table& table, const Path& file) {
            StorageBackpack result;
            static_cast<StorageEquipment&>(result) = equipment(table, file);
            const auto* main = table["grid"].as_table();
            if (!main) invalid(file, result.id + ".grid", "must be a table");
            result.grid = grid(*main, file);
            return result;
        });
    const auto file = directory / "loadout.toml";
    const auto selection = read(file);
    StorageLoadout result;
    std::unordered_set<std::string> equipped;
    for (const auto& node : array(selection, "equipment", file)) {
        const auto id = node.value<std::string>();
        if (!id) invalid(file, "equipment", "must contain string IDs");
        if (!equipped.insert(*id).second) invalid(file, "equipment", "contains duplicate id " + *id);
        result.equipment.push_back(resolve(equipmentTypes, *id, file, "equipment"));
    }
    if (selection.contains("backpack"))
        result.backpack = resolve(backpackTypes, required<std::string>(selection, "backpack", file), file, "backpack");
    return result;
}
} // namespace viewer
