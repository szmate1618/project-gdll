#pragma once
#include "options.hpp"
#include <atomic>
#include <functional>
#include <mutex>
#include <thread>

namespace multiplayer {
struct ServerSettings {
    bool loopbackOnly = false;
    std::size_t capacity = maxPlayers;
};
// Blocking authoritative server loop, shared by dedicated and integrated hosts.
// ready is called only after scene validation and successful socket binding.
void runServer(ServerOptions options, const ServerSettings& settings,
               const std::function<bool()>& shouldStop,
               const std::function<void(std::uint16_t)>& ready = {});

class LocalServer {
public:
    struct State { std::uint16_t port = 0; bool finished = false; std::string error; };
    LocalServer(ServerOptions options, ServerSettings settings);
    ~LocalServer();
    LocalServer(const LocalServer&) = delete;
    LocalServer& operator=(const LocalServer&) = delete;
    void requestStop();
    State state() const;
private:
    std::atomic<bool> stop_{false};
    mutable std::mutex mutex_;
    State state_;
    std::thread worker_;
};
} // namespace multiplayer
