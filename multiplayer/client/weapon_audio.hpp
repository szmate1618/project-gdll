#pragma once
#include "audio.hpp"
#include <cstddef>

namespace multiplayer {
viewer::AudioEffect firearmSound(std::size_t slot);
viewer::AudioEffect reloadSound(std::size_t slot);
} // namespace multiplayer
