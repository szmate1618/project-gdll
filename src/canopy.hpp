#pragma once

#include "box_layer.hpp"
#include <array>

namespace viewer {
class PhysicsScene;
// A 3 m square, X/Z at rest. The owning ground item supplies panel (2,2);
// this class owns the other 35 bodies and all 60 shared-edge spring hinges.
class Canopy {
public:
    static constexpr int side = 6, panelCount = side*side, rootPanel = 2*side+2;
    static constexpr float panelSize = .5f, thickness = .02f, panelMass = .08f;
    static glm::vec3 panelOffset(int index);
    static BoxSpawnMotion motion(std::shared_ptr<PhysicsWorld> world, bool replica = false);
    Canopy(PhysicsProp root, bool replica = false);
    ~Canopy();
    Canopy(const Canopy&) = delete;
    Canopy& operator=(const Canopy&) = delete;
    std::vector<BoxPose> poses() const;
    PhysicsProp prop(int panel) const;
    bool owns(const PhysicsProp& prop) const;
    bool grabbed() const;
    void prepare(PhysicsScene& scene);
    void synchronize(const std::vector<BoxPose>& poses);
    std::size_t hingeCount() const { return joints_.size(); }
    // Shared edges, in canonical row-major panel IDs; no diagonal connections.
    static std::vector<std::pair<int,int>> edges();
private:
    PhysicsProp root_;
    BoxLayer panels_;
    std::vector<b3JointId> joints_;
};
}
