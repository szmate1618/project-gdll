#pragma once
#include "acoustics.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace viewer {
using ImpactKey = std::array<unsigned,5>;
ImpactKey impactKey(const ImpactSound& sound);
ImpactSound quantizedImpact(const ImpactSound& sound);
// Pure, synchronous processing of a short mono PCM16 sample. Gain/position
// stay outside synthesis, so differing impact energies can reuse one buffer.
std::vector<std::int16_t> synthesizeImpact(const std::vector<std::int16_t>& source,
                                        int sampleRate, const ImpactSound& sound);
} // namespace viewer
