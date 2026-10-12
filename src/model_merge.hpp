#pragma once
#include "model.hpp"

namespace viewer {
// Transfer a static layer into the town without changing existing indices.
// Preserve source node transforms; remap image/texture/material/primitive refs.
// The destination's geographic frame remains authoritative.
void appendStaticModel(Model& destination, Model&& source);
} // namespace viewer
