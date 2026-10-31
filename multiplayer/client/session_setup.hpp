#pragma once
#include "session.hpp"

namespace multiplayer {
// A short-lived query connection closes before expensive asset loading, so no
// player slot or handshake timeout is held while the client prepares the world.
std::optional<Options> discoverSession(const Options& local, const SessionFeedback& feedback,
                                     const std::function<void()>& checkServer);
} // namespace multiplayer
