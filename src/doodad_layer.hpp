#pragma once

#include "doodad_placement.hpp"
#include "model.hpp"
#include <array>
#include <memory>

namespace viewer {
class DoorLayer;
class DoodadState;
class PhysicsScene;
class PhysicsWorld;

struct DoodadRenderInstance {
    std::uint64_t id;
    DoodadKind kind;
    glm::mat4 transform;
    glm::vec3 minimum, maximum;
};

// Compact nearby cell instances become grabbable rigid props only within hand
// reach. Moved identities survive both source-cell eviction and destination
// changes. Generator/state/world lifetimes must outlive this layer.
class DoodadLayer {
public:
    DoodadLayer(std::shared_ptr<PhysicsWorld> world,
                const DoodadPlacementGenerator& generator, DoodadState& state,
                const std::array<Model,doodadKindCount>& models);
    ~DoodadLayer();
    DoodadLayer(const DoodadLayer&) = delete;
    DoodadLayer& operator=(const DoodadLayer&) = delete;
    // Before physics: stream cells, promote nearby props and prepare swept
    // terrain/door contacts. Positions are world meters (eyes or feet).
    void update(glm::vec3 playerPosition, PhysicsScene& scene, DoorLayer& doors);
    // After physics, and before saving: publish solved poses to sparse state
    // and refresh this frame's render instances.
    void captureChanges();
    const std::vector<DoodadRenderInstance>& instances() const;
    std::uint64_t revision() const;
    std::size_t cellCount() const;
    std::size_t activeBodyCount() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace viewer
