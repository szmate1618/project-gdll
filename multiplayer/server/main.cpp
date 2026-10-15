#include "network.hpp"
#include "world.hpp"
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

namespace {
volatile std::sig_atomic_t running = 1;
void stop(int) { running = 0; }
}
int main(int argc, char** argv) {
    try {
        const auto options = multiplayer::parseOptions(argc, argv, true);
        multiplayer::Scene scene(options);
        multiplayer::World world(scene);
        multiplayer::Network network(options.fakeLagMs, options.fakeLossPercent);
        network.listen(options.port);
        std::signal(SIGINT, stop); std::signal(SIGTERM, stop);
        using Clock = std::chrono::steady_clock;
        struct Peer { std::uint32_t id = 0; Clock::time_point last = Clock::now(); };
        std::map<HSteamNetConnection, Peer> peers;
        auto disconnect = [&](HSteamNetConnection c, const std::string& reason) {
            const auto p = peers.find(c);
            if (p != peers.end()) {
                world.leave(p->second.id); peers.erase(p);
                std::cout << "Disconnected: " << reason << std::endl;
            }
            network.close(c, reason);
        };
        const auto start = Clock::now(); auto next = start;
        const auto step = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / 60));
        std::cout << "Listening on UDP " << options.port << " (maximum " << multiplayer::maxPlayers << " players)" << std::endl;
        while (running && (!options.seconds || std::chrono::duration<double>(Clock::now() - start).count() < options.seconds)) {
            const auto messages = network.receive();
            for (const auto& event : network.events) {
                if (event.state == k_ESteamNetworkingConnectionState_Connected) peers.try_emplace(event.connection);
                else if (event.state == k_ESteamNetworkingConnectionState_ClosedByPeer || event.state == k_ESteamNetworkingConnectionState_ProblemDetectedLocally)
                    disconnect(event.connection, event.reason);
            }
            for (const auto& [c, message] : messages) {
                const auto it = peers.find(c);
                if (it == peers.end()) continue;
                try {
                    if (std::holds_alternative<multiplayer::Goodbye>(message)) { disconnect(c, "Client left"); continue; }
                    auto& peer = it->second;
                    if (const auto* hello = std::get_if<multiplayer::Hello>(&message)) {
                        if (peer.id || hello->fingerprint != scene.fingerprint) throw std::runtime_error("Content mismatch or duplicate hello");
                        peer.id = world.join();
                        if (!network.send(c, multiplayer::Welcome{peer.id}, true)) throw std::runtime_error("Cannot send welcome");
                        network.send(c, world.snapshot(peer.id), false);
                        std::cout << "Joined player " << peer.id << std::endl;
                    } else if (const auto* commands = std::get_if<multiplayer::Commands>(&message); commands && peer.id)
                        world.submit(peer.id, *commands);
                    else throw std::runtime_error("Unexpected client message");
                    peer.last = Clock::now();
                } catch (const std::exception& error) { disconnect(c, error.what()); }
            }
            const auto now = Clock::now();
            for (auto it = peers.begin(); it != peers.end();) {
                const auto current = it++;
                if (std::chrono::duration<double>(now - current->second.last).count() > (current->second.id ? 10 : 5))
                    disconnect(current->first, "Client timeout");
            }
            int steps = 0;
            while (next <= now && steps++ < 8) {
                world.step(); next += step;
                if (world.tick() % 3 == 0)
                    for (const auto& [c, peer] : peers) if (peer.id) network.send(c, world.snapshot(peer.id), false);
            }
            // Bound catch-up work under overload; gameplay dt remains fixed.
            if (next + std::chrono::milliseconds(500) < now) next = now;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        std::cout << "SERVER ticks=" << world.tick() << " players=" << world.players().size() << '\n';
        return 0;
    } catch (const std::exception& error) { std::cerr << "Server: " << error.what() << '\n'; return 1; }
}
