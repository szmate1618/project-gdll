#pragma once
#include "scene.hpp"

namespace multiplayer {
SessionSettings sessionSettings(const Options& options, const Scene& scene);
Options applySessionSettings(const Options& local, const SessionSettings& settings);
} // namespace multiplayer
