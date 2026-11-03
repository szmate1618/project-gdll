#pragma once

#include "doorway_guidance.hpp"

namespace viewer {
// Humanoid dimensions; requests remain owned by each ZombieCrowd instance.
class ZombieDoorwayGuidance : public DoorwayGuidance {
public:
    ZombieDoorwayGuidance();
};
} // namespace viewer
