#pragma once
#include "box_layer.hpp"
#include <vector>

namespace viewer {
class VehicleLayer;
struct ZombieLayer;
class CollisionWorld;

// Swept chassis contacts against the indexed, kinematic living crowd. Capture
// after vehicle preparation and resolve after each shared fixed physics step.
// Only actively simulated cars are visited; parked fleets need no per-car work.
class VehicleZombieCollision {
public:
    void capture(const VehicleLayer& cars);
    void resolve(VehicleLayer& cars, ZombieLayer& zombies, const CollisionWorld& world,
                 float seconds, double animationSeconds);
private:
    std::vector<BoxPose> previous_;
};
} // namespace viewer
