#include "session_setup.hpp"
#include "session_settings.hpp"
#include "network.hpp"
#include <chrono>
#include <thread>

namespace multiplayer {
std::optional<Options> discoverSession(const Options& local, const SessionFeedback& feedback,
                                     const std::function<void()>& checkServer) {
    Network network(local.fakeLagMs, local.fakeLossPercent);
    const auto connection = network.connect(local.address);
    std::optional<Options> configured;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        if (checkServer) checkServer();
        if (feedback && !feedback("Reading host session settings...")) return std::nullopt;
        const auto messages = network.receive();
        for (const auto& event : network.events) {
            if (event.state == k_ESteamNetworkingConnectionState_Connected) {
                if (!network.send(connection, RequestSessionSettings{}, true))
                    throw std::runtime_error("Cannot request host session settings");
            } else if (event.state == k_ESteamNetworkingConnectionState_ClosedByPeer && configured && event.reason == "Client left") {
                // Wait for the server to release this transport slot before
                // reconnecting, including fast loads in one-player sessions.
                return configured;
            } else if (event.state == k_ESteamNetworkingConnectionState_ClosedByPeer ||
                       event.state == k_ESteamNetworkingConnectionState_ProblemDetectedLocally)
                throw std::runtime_error("Connection ended: " + event.reason);
        }
        for (const auto& [sender, message] : messages) {
            if (sender != connection) continue;
            if (const auto* settings = std::get_if<SessionSettings>(&message)) {
                if (configured) throw std::runtime_error("Duplicate host session settings");
                configured = applySessionSettings(local, *settings);
                if (!network.send(connection, Goodbye{}, true)) throw std::runtime_error("Cannot finish session settings query");
                continue;
            }
            throw std::runtime_error("Unexpected host session settings response");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    throw std::runtime_error("Timed out reading host session settings");
}
} // namespace multiplayer
