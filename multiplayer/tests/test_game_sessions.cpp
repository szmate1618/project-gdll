#include "client/options.hpp"
#include "client/session.hpp"
#include "server/runtime.hpp"
#include "server/world.hpp"
#include "network.hpp"
#include <atomic>
#include <chrono>
#include <exception>
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
    require(!parse({"game", "--single-player", "--no-cars"}).scene.cars, "Single-player lost --no-cars");
    const auto host = parse({"game", "--host", "--port", "27022"});
    require(host.mode == GameMode::Host && host.scene.port == 27022, "Host port was lost");
    require(parse({"game", "--connect", "127.0.0.1:27022"}).mode == GameMode::Join, "Join flag changed");
    require(parse({"game", "--headless"}).mode == GameMode::Join, "Legacy headless client changed");
    bool rejected = false;
    try { parse({"game", "--single-player", "--host"}); } catch (const std::exception&) { rejected = true; }
    require(rejected, "Conflicting modes accepted");
}
void setupBeforeWelcome(const multiplayer::Options& options) {
    multiplayer::Scene scene(options);
    multiplayer::World world(scene);
    multiplayer::Network server;
    server.listen(0, true, 1);
    std::atomic<bool> stop{false}, acknowledgedBeforeWelcome{false};
    std::exception_ptr serverError;
    std::thread worker([&] {
        try {
            HSteamNetConnection peer = k_HSteamNetConnection_Invalid;
            std::optional<multiplayer::Snapshot> initial;
            auto nextSnapshot = Clock::now();
            while (!stop) {
                const auto messages = server.receive();
                for (const auto& [connection, message] : messages) {
                    if (const auto* hello = std::get_if<multiplayer::Hello>(&message)) {
                        require(hello->fingerprint == scene.fingerprint, "Setup-order fixture fingerprint differs");
                        peer = connection;
                        const auto id = world.join();
                        require(id == 1, "Setup-order fixture assigned unexpected player ID");
                        initial = world.snapshot(id);
                        // Lane 1 may outrun a delayed/retransmitted Welcome on
                        // lane 0. Force that order without timing or packet-loss
                        // assumptions by withholding Welcome until setup ack.
                        require(server.send(peer, multiplayer::ZombieSetups{}, true),
                            "Cannot send setup-order fixture metadata");
                    } else if (const auto* ack = std::get_if<multiplayer::ZombieSetupAck>(&message)) {
                        require(ack->zombies.empty() && !acknowledgedBeforeWelcome,
                            "Setup-order fixture received unexpected setup acknowledgement");
                        acknowledgedBeforeWelcome = true;
                        require(server.send(peer, multiplayer::Welcome{1}, true),
                            "Cannot send delayed fixture welcome");
                    } else if (std::holds_alternative<multiplayer::Goodbye>(message)) {
                        server.close(connection, "Setup-order fixture departure");
                        peer = k_HSteamNetConnection_Invalid;
                    }
                }
                if (acknowledgedBeforeWelcome && peer != k_HSteamNetConnection_Invalid && Clock::now() >= nextSnapshot) {
                    require(initial && server.send(peer, *initial, false), "Cannot send setup-order fixture snapshot");
                    nextSnapshot = Clock::now() + std::chrono::milliseconds(20);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        } catch (...) { serverError = std::current_exception(); }
    });
    auto client = options;
    client.address = "127.0.0.1:" + std::to_string(server.listeningPort());
    client.headless = true; client.bot = true; client.seconds = .35;
    std::exception_ptr clientError;
    try { require(multiplayer::runClientSession(client) == 0, "Setup-order client session failed"); }
    catch (...) { clientError = std::current_exception(); }
    stop = true;
    worker.join();
    if (serverError) std::rethrow_exception(serverError);
    if (clientError) std::rethrow_exception(clientError);
    require(acknowledgedBeforeWelcome, "Client did not acknowledge setup before Welcome");
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
        setupBeforeWelcome(options.scene);
        {
            auto invalid = options; invalid.scene.map = "missing-session-map.glb";
            multiplayer::LocalServer server(invalid, {});
            waitFor([&] { return server.state().finished; }, "Failed startup did not finish");
            require(!server.state().error.empty() && !server.state().port, "Startup error was not published");
        }
        {
            auto first = std::make_unique<multiplayer::LocalServer>(options, multiplayer::ServerSettings{});
            auto conflict = options; conflict.scene.port = ready(*first);
            multiplayer::LocalServer second(conflict, {});
            waitFor([&] { const auto state = second.state(); return state.port || state.finished; }, "Second server startup did not finish");
            require(!second.state().error.empty() && !second.state().port, "Port conflict reported readiness");
            {
                Peer original(conflict.scene.port, fingerprint);
                waitFor([&] { original.poll(); return original.id && original.snapshots; }, "Failed bind disrupted the original host");
            }
            first->requestStop();
            waitFor([&] { return first->state().finished; }, "Original host did not stop");
            first.reset();
            multiplayer::LocalServer replacement(conflict, {});
            require(ready(replacement) == conflict.scene.port, "Stopped host did not release its port");
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
