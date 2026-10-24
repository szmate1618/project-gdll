#include "client/options.hpp"
#include "client/session.hpp"
#include "server/runtime.hpp"
#include "network.hpp"
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

namespace {
using Clock = std::chrono::steady_clock;
void require(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }
template<class Predicate> void waitFor(Predicate predicate, const char* message) {
    const auto deadline = Clock::now() + std::chrono::seconds(8);
    while (!predicate()) {
        require(Clock::now() < deadline, message);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}
std::uint16_t ready(multiplayer::LocalServer& server) {
    waitFor([&] { const auto state = server.state(); return state.port || state.finished; }, "Server did not finish startup");
    const auto state = server.state();
    require(state.error.empty() && state.port && !state.finished, "Server startup: " + state.error);
    return state.port;
}
struct Peer {
    multiplayer::Network network;
    HSteamNetConnection connection;
    std::string fingerprint, ended;
    std::size_t population = 0, snapshots = 0;
    std::uint32_t id = 0;
    Peer(std::uint16_t port, std::string identity) :
        connection(network.connect("127.0.0.1:" + std::to_string(port))), fingerprint(std::move(identity)) {}
    void poll() {
        const auto messages = network.receive();
        for (const auto& event : network.events) {
            if (event.state == k_ESteamNetworkingConnectionState_Connected)
                network.send(connection, multiplayer::Hello{fingerprint}, true);
            if (event.state == k_ESteamNetworkingConnectionState_ClosedByPeer ||
                event.state == k_ESteamNetworkingConnectionState_ProblemDetectedLocally) ended = event.reason;
        }
        for (const auto& [c, message] : messages) {
            (void)c;
            if (const auto* welcome = std::get_if<multiplayer::Welcome>(&message)) id = welcome->player;
            if (const auto* snapshot = std::get_if<multiplayer::Snapshot>(&message)) {
                ++snapshots; population = snapshot->players.size();
            }
        }
    }
};
void parser() {
    const auto parse = [](std::vector<std::string> values) {
        std::vector<char*> pointers;
        for (auto& value : values) pointers.push_back(value.data());
        return multiplayer::parseGameOptions(static_cast<int>(pointers.size()), pointers.data());
    };
    using multiplayer::GameMode;
    require(parse({"game"}).mode == GameMode::Menu, "Default must open menu");
    require(parse({"game", "--single-player"}).mode == GameMode::SinglePlayer, "Missing single-player flag");
    const auto host = parse({"game", "--host", "--port", "27022"});
    require(host.mode == GameMode::Host && host.scene.port == 27022, "Host port was lost");
    require(parse({"game", "--connect", "127.0.0.1:27022"}).mode == GameMode::Join, "Join flag changed");
    require(parse({"game", "--headless"}).mode == GameMode::Join, "Legacy headless client changed");
    bool rejected = false;
    try { parse({"game", "--single-player", "--host"}); } catch (const std::exception&) { rejected = true; }
    require(rejected, "Conflicting modes accepted");
}
}
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected synthetic map");
        parser();
        multiplayer::ServerOptions options;
        options.scene.map = argv[1]; options.scene.port = 0;
        options.scene.trees = options.scene.forests = false; options.scene.zombieCount = 0;
        std::string fingerprint;
        { multiplayer::Scene scene(options.scene); fingerprint = scene.fingerprint; }
        for (int cycle = 0; cycle < 3; ++cycle) {
            multiplayer::LocalServer server(options, {true, 1});
            const auto port = ready(server);
            Peer local(port, fingerprint);
            waitFor([&] { local.poll(); return local.id && local.snapshots; }, "Private player failed to join");
            require(local.population == 1, "Single-player population is not one");
            {
                Peer other(port, fingerprint);
                waitFor([&] { local.poll(); other.poll(); return !other.ended.empty(); }, "Second local client was not rejected");
                require(other.ended.find("Server full") != std::string::npos && !other.id,
                        "Private session accepted another player");
            } // Destroying another endpoint must not kill the server/local client.
            const auto received = local.snapshots;
            waitFor([&] { local.poll(); return local.snapshots > received; }, "Endpoint destruction killed shared networking");
            server.requestStop();
            waitFor([&] { local.poll(); return !local.ended.empty(); }, "Host shutdown did not notify client");
            require(local.ended.find("Host ended") != std::string::npos, "Host shutdown reason was lost: " + local.ended);
        }
        {
            multiplayer::LocalServer server(options, {});
            const auto port = ready(server);
            std::vector<std::unique_ptr<Peer>> peers;
            for (std::size_t i = 0; i < multiplayer::maxPlayers; ++i) {
                peers.push_back(std::make_unique<Peer>(port, fingerprint));
                waitFor([&] { for (auto& peer : peers) peer->poll(); return peers.back()->id != 0; }, "Host join failed");
            }
            waitFor([&] {
                bool all = true;
                for (auto& peer : peers) { peer->poll(); all &= peer->population == 8; }
                return all;
            }, "Eight players did not share authoritative state");
            Peer ninth(port, fingerprint);
            waitFor([&] { for (auto& peer : peers) peer->poll(); ninth.poll(); return !ninth.ended.empty(); }, "Ninth player was not rejected");
            require(!ninth.id && ninth.ended.find("Server full") != std::string::npos, "Wrong full-server behavior");
            // A rejected handshake must release its transport slot for a retry.
            peers.back()->network.send(peers.back()->connection, multiplayer::Goodbye{}, true);
            waitFor([&] { for (auto& peer : peers) peer->poll(); return !peers.back()->ended.empty(); }, "Leaving peer was not removed");
            peers.pop_back();
            Peer mismatch(port, std::string(64, 'a'));
            waitFor([&] { mismatch.poll(); return !mismatch.ended.empty(); }, "Content mismatch was not rejected");
            require(!mismatch.id && mismatch.ended.find("Content mismatch") != std::string::npos, "Content validation changed");
            Peer retry(port, fingerprint);
            waitFor([&] { retry.poll(); return retry.id && retry.population == 8; }, "Released slot could not be reused");
            server.requestStop();
        }
        // Exercise the actual client session repeatedly in this same process.
        for (bool single : {true, false, true}) {
            multiplayer::LocalServer server(options, {single, single ? 1u : multiplayer::maxPlayers});
            auto client = options.scene;
            client.address = "127.0.0.1:" + std::to_string(ready(server));
            client.headless = true; client.bot = true; client.seconds = .25;
            require(multiplayer::runClientSession(client) == 0, "Repeated client session failed");
        }
        {
            auto invalid = options; invalid.scene.map = "missing-session-map.glb";
            multiplayer::LocalServer server(invalid, {});
            waitFor([&] { return server.state().finished; }, "Failed startup did not finish");
            require(!server.state().error.empty() && !server.state().port, "Startup error was not published");
        }
        {
            multiplayer::LocalServer first(options, {});
            auto conflict = options; conflict.scene.port = ready(first);
            multiplayer::LocalServer second(conflict, {});
            waitFor([&] { return second.state().finished; }, "Port conflict did not fail");
            require(!second.state().error.empty() && !second.state().port, "Port conflict reported readiness");
        }
        {
            multiplayer::LocalServer cancelled(options, {});
            cancelled.requestStop();
            waitFor([&] { return cancelled.state().finished; }, "Cancelled startup did not finish");
        }
        std::cout << "Private admission, eight-player hosting, shutdown, retries and repeated client sessions passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
