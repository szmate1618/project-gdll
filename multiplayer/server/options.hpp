#pragma once

#include "scene.hpp"

namespace multiplayer {

// Scenario loading is authoritative. Clients receive poses, never scenario
// definitions or navigation routes.
struct ServerOptions {
    Options scene;
    std::filesystem::path scenario;
};

ServerOptions parseServerOptions(int argc, char** argv);

} // namespace multiplayer
