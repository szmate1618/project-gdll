#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace viewer {

struct StorageGrid {
    int width = 0;
    int height = 0;
    double accessTime = 0; // Seconds at row zero.
    double depthTimePerRow = 0; // Seconds per row; main backpack grid only.
    // Empty accepts any item; otherwise at least one item tag must match.
    std::vector<std::string> allowedTags{};
};

struct StorageSlot {
    std::string id;
    std::string name;
    // Normalized center in the panel's inner storage rectangle, not the body
    // placeholder or main backpack grid. X grows right; Y grows down.
    double x = 0;
    double y = 0;
    StorageGrid grid;
};

struct StorageEquipment {
    std::string id;
    std::string name;
    std::vector<StorageSlot> slots;
};

struct StorageBackpack : StorageEquipment {
    StorageGrid grid;
};

struct StorageLoadout {
    std::vector<StorageEquipment> equipment;
    std::optional<StorageBackpack> backpack;
};

// Reads catalogs and resolves loadout.toml once. Throws with file/field context
// for invalid definitions or references. No connection to weapon ownership.
StorageLoadout loadStorageLoadout(const std::filesystem::path& directory);

} // namespace viewer
