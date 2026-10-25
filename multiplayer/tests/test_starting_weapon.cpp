// Exercise the real headless client against a scripted transport and server world.
#include "../client/session.cpp"
#include "server/world.hpp"
#include <fstream>

namespace {
std::filesystem::path resources;
multiplayer::World* world = nullptr;
std::uint32_t player = 0;
int snapshots = 0, commands = 0;
bool departed = false;
constexpr HSteamNetConnection connection = 1;
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
}

namespace multiplayer {
const std::filesystem::path& resourceRoot() { return resources; }
Network::Network(float, float) {}
Network::~Network() = default;
HSteamNetConnection Network::connect(const std::string&) { return connection; }
std::vector<std::pair<HSteamNetConnection, Message>> Network::receive() {
    events.clear();
    if (departed) {
        events.push_back({connection, k_ESteamNetworkingConnectionState_ClosedByPeer, {}});
        return {};
    }
    world->step();
    auto snapshot = world->snapshot(player);
    if (snapshots++ == 0) return {{connection, Welcome{player}}, {connection, snapshot}};
    // Later snapshots must not overwrite the selection used by new client input.
    snapshot.players.front().slot = 0;
    return {{connection, snapshot}};
}
bool Network::send(HSteamNetConnection, const Message& message, bool) {
    if (const auto* batch = std::get_if<Commands>(&message)) {
        for (const auto& command : batch->commands) {
            require(command.slot == 3, "Client replaced the authoritative starting sniper with slot zero");
            require(command.movement.yawDegrees == 35 && command.pitch == -7,
                    "Client must retain the authoritative scenario starting orientation");
            ++commands;
        }
        world->submit(player, *batch);
    } else if (std::holds_alternative<Goodbye>(message)) departed = true;
    return true;
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 3) throw std::runtime_error("Test asset and fixture directory arguments missing");
        resources = argv[2];
        const auto copy = std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing;
        for (const auto* directory : {"storage", "vehicles"})
            std::filesystem::copy(std::filesystem::path(MP_ROOT) / directory, resources / directory, copy);
        {
            std::ofstream inventory(resources / "inventory.toml");
            inventory << "equipped = 'sniper'\n[weapons]\npistol = 12\nsniper = 5\n"
                         "[ammo]\npistol_rounds = 120\nsmg_rounds = 300\nsniper_rounds = 30\nshotgun_shells = 48\nhe_grenades = 0\n";
            require(bool(inventory), "Could not write starting weapon fixture");
        }
        multiplayer::Options options; options.map = argv[1];
        options.trees = options.forests = false; options.zombieCount = 0;
        multiplayer::Scene scene(options);
        scene.spawnOrientation = {35, -7};
        multiplayer::World server(scene);
        world = &server;
        player = world->join();
        require(world->players().at(player).weapons.slot() == 3, "Fixture did not equip the configured sniper");
        for (int tick = 0; tick < 20; ++tick) {
            world->step();
            require(world->snapshot(player).players.front().slot == 3,
                    "Server replaced the starting weapon before receiving input");
        }
        char program[] = "godollo_client", noTrees[] = "--no-trees", count[] = "--zombie-count", zero[] = "0";
        char headless[] = "--headless", seconds[] = "--seconds", duration[] = "0.5";
        char* args[] = {program, argv[1], noTrees, count, zero, headless, seconds, duration};
        require(multiplayer::runClientSession(multiplayer::parseOptions(8, args, false)) == 0 && commands > 1, "Client did not preserve its initial authoritative selection");
        require(world->players().at(player).weapons.slot() == 3, "Client commands replaced the configured starting weapon");
        // Vehicle resources affect the handshake, while a live session keeps
        // the definitions captured before the edit.
        if (!scene.vehicles->count()) scene.vehicles->add({0,2,0});
        const float capturedWeight = scene.vehicles->config(0).weightKg;
        const auto carFile = resources / "vehicles/toyota_corolla_1999.toml";
        std::ifstream carInput(carFile);
        std::string carText((std::istreambuf_iterator<char>(carInput)), {});
        const auto weight = carText.find("weight_kg = ");
        require(weight != std::string::npos, "Vehicle fixture has no mass");
        carText.replace(weight, carText.find('\n', weight)-weight,
                        "weight_kg = " + std::to_string(capturedWeight + 100));
        { std::ofstream carOutput(carFile); carOutput << carText; }
        multiplayer::Scene changedCars(options);
        require(changedCars.fingerprint != scene.fingerprint && scene.vehicles->config(0).weightKg == capturedWeight,
                "Vehicle config did not affect fingerprint or changed a running session");
        auto fingerprint = changedCars.fingerprint;
        for (const auto* suffix : {".inventory.toml", ".gameplay.toml", ".tags.txt"}) {
            { std::ofstream file(resources / "storage/weapons" / (std::string("pistol") + suffix), std::ios::app);
              file << "\n"; }
            multiplayer::Scene changedDefinition(options);
            require(changedDefinition.fingerprint != fingerprint,
                    "Inventory, gameplay or tags file was omitted from the scene fingerprint");
            fingerprint = changedDefinition.fingerprint;
        }
        // A later join must use the session's fingerprinted starting definitions,
        // even if the source inventory is edited while the server is running.
        { std::ofstream inventory(resources / "inventory.toml"); inventory << "invalid inventory\n"; }
        const auto late = world->join();
        require(world->players().at(late).weapons.slot() == 3 &&
                world->players().at(late).weapons.equipped().loaded == 5,
                "Late join reread resources or inherited another player's ammo");
        std::cout << "Starting weapon checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
