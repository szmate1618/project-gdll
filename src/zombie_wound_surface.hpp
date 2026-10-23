#pragma once

#include "zombie_wounds.hpp"

namespace viewer {
// Welds matching reference positions and skin weights across material/normal
// seams, then closes intact region boundaries. Vertices retain their ownership.
void closeZombieWoundSurface(const AnimatedModel& asset, ZombieWoundGeometry& geometry);
// Camera-only cap mesh, with the same baked frames and live skinning as its rim.
AnimatedModel zombieWoundClosureModel(const AnimatedModel& asset, const ZombieWoundGeometry& geometry);
bool zombieWoundClosureRemoved(const std::array<ZombieWoundGeometry::Vertex, 3>& triangle,
                              const ZombieWound& wound);
} // namespace viewer
