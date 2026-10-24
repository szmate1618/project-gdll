#pragma once
#include "vehicle_layer.hpp"
#include <optional>

namespace viewer {
// Prepared chassis-local -Z-forward poses, in world meters. IDs preserve file
// order; that dense order is also the multiplayer vehicle ID order.
struct VehiclePlacement {
    std::string id;
    glm::vec3 center;
    glm::quat rotation;
};
// Missing sidecars return nullopt. Invalid/stale publications throw before any
// rigid bodies are created. An empty publication intentionally places no cars.
std::optional<std::vector<VehiclePlacement>> loadVehiclePlacements(const std::filesystem::path& map);
// Randomize once per level/server session, never independently on clients. A seed
// is explicit so tests and authored scenarios can reproduce the selection.
void placeVehicles(VehicleLayer& cars, const std::vector<VehiclePlacement>& placements, std::uint32_t seed);
}
