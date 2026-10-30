#include "runtime.hpp"
#include "network.hpp"
#include "world.hpp"
#include "character_appearances.hpp"
#include "zombie_navigation.hpp"
#include "zombie_scenario.hpp"
#include <chrono>
#include <iostream>
#include <thread>

namespace multiplayer {
void runServer(ServerOptions serverOptions, const ServerSettings& settings,
               const std::function<bool()>& shouldStop,
               const std::function<void(std::uint16_t)>& ready,
               const std::function<void(double)>& simulationMilliseconds) {
    if (shouldStop()) return;
    auto& options = serverOptions.scene;
    auto navmesh = options.map;
    navmesh.replace_extension(".navmesh");
    multiplayer::ZombiePopulationLoader populationLoader;
    std::optional<viewer::ZombieScenarioConfig::Player> scenarioPlayer;
    if (!serverOptions.scenario.empty()) {
        const auto scenario = viewer::loadZombieScenarioConfig(serverOptions.scenario);
        if (scenario.count() > multiplayer::maxZombies)
            throw std::runtime_error("Scenario exceeds the multiplayer zombie limit");
        options.zombieCount = scenario.count();
        scenarioPlayer = scenario.player;
        populationLoader = [scenario, navmesh](const auto& source, const auto& collision, const auto& model) {
            return viewer::loadZombieScenario(source, collision, model, navmesh, scenario);
        };
    }
    multiplayer::Scene scene(options, populationLoader, scenarioPlayer);
    if (shouldStop()) return;
    multiplayer::World world(scene);
    if (const auto* navigation = scene.zombies.navigation()) {
        std::cout << "Server zombie navigation: " << navmesh
                  << "; g+8h, 50000 nodes, " << navigation->pathArenaBytes() << " path bytes"
                  << "; cache SHA256 " << navigation->meshSha256()
                  << "; embedded replay SHA256 " << navigation->sourceSha256() << '\n';
    }
    multiplayer::Network network(options.fakeLagMs, options.fakeLossPercent);
    network.listen(settings.loopbackOnly ? 0 : options.port, settings.loopbackOnly, settings.capacity);
    if (shouldStop()) return;
    if (ready) ready(network.listeningPort());
    using Clock = std::chrono::steady_clock;
    struct Peer {
        std::uint32_t id = 0;
        // MSVC cannot resolve the enclosing function's Clock alias here.
        std::chrono::steady_clock::time_point last = std::chrono::steady_clock::now();
    };
    std::map<HSteamNetConnection, Peer> peers;
    ServerCharacterAppearances appearances;
    auto disconnect = [&](HSteamNetConnection c, const std::string& reason) {
        const auto p = peers.find(c);
        if (p != peers.end()) {
            appearances.leave(p->second.id);
            world.leave(p->second.id); peers.erase(p);
            std::cout << "Disconnected: " << reason << std::endl;
        }
        network.close(c, reason);
    };
    const auto sendState = [&](HSteamNetConnection c, std::uint32_t id) {
        network.send(c, world.snapshot(id), false);
        for (std::size_t i = 0; i < 4; ++i) {
            const auto setups = world.nextSetups(id);
            if (!setups || !network.send(c, *setups, true)) break;
            world.setupsSent(id, *setups);
        }
        if (const auto frame=world.birdFrame(id); frame && network.send(c,*frame,true))
            world.birdFrameSent(id,frame->tick);
        for (std::size_t i = 0; i < multiplayer::Replication::maxPoseTransfers; ++i) {
            const auto pose = world.nextPose(id);
            if (!pose || !network.send(c, *pose, true)) break;
            world.poseSent(id, *pose);
        }
    };
    const auto start = Clock::now(); auto next = start;
    auto timingStart = start;
    double simulationSeconds = 0;
    std::size_t simulationTicks = 0;
    const auto step = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / 60));
    std::cout << "Listening on UDP " << network.listeningPort() << " (maximum " << settings.capacity << " players)" << std::endl;
    while (!shouldStop() && (!options.seconds || std::chrono::duration<double>(Clock::now() - start).count() < options.seconds)) {
        const auto messages = network.receive();
        for (const auto& event : network.events) {
            // Network emits Connecting only after accepting the peer; Hello may already be queued.
            if (event.state == k_ESteamNetworkingConnectionState_Connecting) peers.try_emplace(event.connection);
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
                    appearances.join(peer.id);
                    if (!network.send(c, multiplayer::Welcome{peer.id}, true)) throw std::runtime_error("Cannot send welcome");
                    sendState(c, peer.id);
                    std::cout << "Joined player " << peer.id << std::endl;
                } else if (const auto* commands = std::get_if<multiplayer::Commands>(&message); commands && peer.id)
                    world.submit(peer.id, *commands);
                else if (const auto* chunk = std::get_if<CharacterAppearanceChunk>(&message); chunk && peer.id) {
                    if (appearances.receive(peer.id, *chunk))
                        std::cout << "Character appearance accepted for player " << peer.id << std::endl;
                }
                else if (const auto* ack = std::get_if<multiplayer::PoseAck>(&message); ack && peer.id)
                    world.acknowledgePose(peer.id, *ack);
                else if (const auto* ack = std::get_if<multiplayer::ZombieSetupAck>(&message); ack && peer.id)
                    world.acknowledgeSetups(peer.id, *ack);
                else if (std::holds_alternative<multiplayer::BirdResync>(message) && peer.id)
                    world.requestBirdCheckpoint(peer.id);
                else throw std::runtime_error("Unexpected client message");
                peer.last = Clock::now();
            } catch (const std::exception& error) { disconnect(c, error.what()); }
        }
        const auto now = Clock::now();
        for (auto it = peers.begin(); it != peers.end();) {
            const auto current = it++;
            if (current->second.id && !appearances.pump(current->second.id,
                    [&](const auto& chunk) { return network.send(current->first, chunk, true); }))
                disconnect(current->first, "Character appearance queue capacity exceeded");
        }
        for (auto it = peers.begin(); it != peers.end();) {
            const auto current = it++;
            if (std::chrono::duration<double>(now - current->second.last).count() > (current->second.id ? 10 : 5))
                disconnect(current->first, "Client timeout");
        }
        int steps = 0;
        while (next <= now && steps++ < 8) {
            const auto tickStart = simulationMilliseconds ? Clock::now() : Clock::time_point{};
            world.step(); next += step;
            if (simulationMilliseconds) {
                simulationSeconds += std::chrono::duration<double>(Clock::now() - tickStart).count();
                ++simulationTicks;
            }
            if (world.tick() % 3 == 0)
                for (const auto& [c, peer] : peers) if (peer.id) sendState(c, peer.id);
        }
        if (simulationMilliseconds && simulationTicks && now - timingStart >= std::chrono::seconds(1)) {
            simulationMilliseconds(simulationSeconds * 1000 / static_cast<double>(simulationTicks));
            simulationSeconds = 0;
            simulationTicks = 0;
            timingStart = now;
        }
        // Bound catch-up work under overload; gameplay dt remains fixed.
        if (next + std::chrono::milliseconds(500) < now) next = now;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::cout << "SERVER ticks=" << world.tick() << " players=" << world.players().size() << '\n';
}

LocalServer::LocalServer(ServerOptions options, ServerSettings settings) {
    worker_ = std::thread([this, options = std::move(options), settings] {
        try {
            runServer(options, settings, [this] { return stop_.load(); }, [this](std::uint16_t port) {
                std::lock_guard<std::mutex> lock(mutex_);
                state_.port = port;
            }, [this](double milliseconds) {
                std::lock_guard<std::mutex> lock(mutex_);
                state_.simulationMilliseconds = milliseconds;
            });
        } catch (const std::exception& error) {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.error = error.what();
        }
        std::lock_guard<std::mutex> lock(mutex_);
        state_.finished = true;
    });
}
LocalServer::~LocalServer() {
    requestStop();
    if (worker_.joinable()) worker_.join();
}
void LocalServer::requestStop() { stop_.store(true); }
LocalServer::State LocalServer::state() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}
} // namespace multiplayer
