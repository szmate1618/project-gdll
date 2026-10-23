#pragma once

#include "zombie_wounds.hpp"

namespace viewer {
// Welds matching reference positions and skin weights across material/normal
// seams, then closes intact torso boundaries. Vertices retain their ownership.
void closeZombieTorso(const AnimatedModel& asset, ZombieTorsoGeometry& geometry);
// Camera-only cap mesh, with the same baked frames and live skinning as its rim.
AnimatedModel zombieTorsoClosureModel(const AnimatedModel& asset, const ZombieTorsoGeometry& geometry);
bool zombieTorsoClosureRemoved(const std::array<ZombieTorsoGeometry::Vertex, 3>& triangle,
                              const ZombieTorsoWound& wound);
} // namespace viewer
