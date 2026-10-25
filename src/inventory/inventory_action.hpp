#pragma once

#include <string>

namespace viewer {
// User operations replayed through InventoryDrag on the authoritative server.
// Timed completions and displaced items follow from these operations.
enum class InventoryDragActionKind { PickUp, Rotate, Drop, Cancel };
struct InventoryDragAction {
    InventoryDragActionKind kind = InventoryDragActionKind::Cancel;
    std::string item, storage;
    int x = 0, y = 0, turns = 0;
};
} // namespace viewer
