#pragma once
#include "model.hpp"

namespace viewer {
// Metres, grip at origin, handle along +Y. Local +X leads every drag swing.
constexpr float axeHeadBottom = .72f, axeHeadTop = 1.0f;
constexpr float axeHeadBack = -.06f, axeHeadFront = .15f, axeHeadHalfDepth = .025f;
constexpr float axeHeadBackBottom = .80f, axeHeadBackTop = .92f;
constexpr float axeEdgeLead = .05f;
constexpr float axeEdgeX = axeHeadFront + axeEdgeLead;
Model makeAxeModel();
}
