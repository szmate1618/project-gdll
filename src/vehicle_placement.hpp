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
// Shared by standalone and authoritative servers, including the fallback car.
// With the same placement order and config catalog, types repeat across loads.
inline constexpr std::uint32_t vehiclePlacementSeed = 42;
// Clients receive the server's choices. An explicit seed remains available for
// callers that need a different reproducible layout.
void placeVehicles(VehicleLayer& cars, const std::vector<VehiclePlacement>& placements,
                   std::uint32_t seed = vehiclePlacementSeed);
}
