#pragma once

#include "house_floorplan.hpp"
#include <map>
#include <memory>

namespace viewer {
class CollisionWorld;
class DoorLayer;
class BuildingFootprints;
class HouseNavigationGraph;
// Immutable map and shared doors/collision must outlive this layer. Only source
// draw references and an entrance grid exist until a nearby exterior door opens.
class FloorplanLayer {
public:
    FloorplanLayer(const Model& model, DoorLayer& doors, CollisionWorld& collision);
    ~FloorplanLayer();
    FloorplanLayer(const FloorplanLayer&) = delete;
    FloorplanLayer& operator=(const FloorplanLayer&) = delete;
    // Physical support points retain existing interiors without opening or
    // generating additional buildings. Points include sleeping corpse parts.
    void update(const std::vector<glm::vec3>& observers,
                const std::vector<glm::vec3>& physicsSupport = {});
    std::uint64_t revision() const;
    std::size_t residentCount() const;
    const std::map<std::string, HouseFloorplan>& resident() const;
    const Model& renderModel() const;
    const std::map<std::size_t, std::size_t>& doorDraws() const; // Render draw -> stable door ID.
    const DoorLayer& doors() const;
    const BuildingFootprints& footprints() const;
    const HouseNavigationGraph* navigationGraph(const std::string& building) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace viewer
