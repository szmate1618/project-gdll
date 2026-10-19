#pragma once

#include "model.hpp"

namespace viewer {
// Shared, unlit alpha-mask billboard: two perpendicular upright cards and one
// horizontal card at half height. Atlas halves are side (left), top (right).
// Native visible size is 4 x 1 x 4 metres, with the base at the origin;
// placement scales uniformly to the selected height.
Model forestBushModel(ImageData atlas);
} // namespace viewer
