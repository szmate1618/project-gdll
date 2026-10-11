#pragma once

#include "collision_world.hpp"
#include "door_layer.hpp"
#include <array>

namespace viewer {

struct DoorOpening {
    glm::vec3 hinge, right, outward;
    float width, bottom, top;
};
using CollisionFace = std::array<glm::vec3, 3>;

// One opening list per original draw. Only v2 wall alternatives with actual
// rendered cutouts qualify; legacy overlay maps retain their solid walls.
std::vector<std::vector<DoorOpening>> collisionDoorOpenings(const Model& model, const DoorLayer& doors);
std::vector<CollisionFace> cutCollisionDoorways(const CollisionFace& face, const std::vector<DoorOpening>& openings);

// Box3D narrow phase against the same oriented box used by the hinge body.
// No opening-angle threshold or state flag is involved.
void appendDoorContacts(const DoorLayer& doors, glm::vec3 feet, float radius, float height,
                        float skin, std::vector<Contact>& contacts);
bool doorsBlockSight(const DoorLayer& doors, glm::vec3 origin, glm::vec3 target);
std::vector<glm::vec3> doorCollisionDebugLines(const DoorLayer& doors, glm::vec3 minimum, glm::vec3 maximum);
} // namespace viewer
