#pragma once

#include "scene.hpp"

namespace multiplayer {

// Server-owned navigation is deliberately absent from the shared scene options
// and content handshake. Clients receive authoritative poses, never routes.
struct ServerOptions {
    Options scene;
    std::filesystem::path navmesh;
};

ServerOptions parseServerOptions(int argc, char** argv);

} // namespace multiplayer
