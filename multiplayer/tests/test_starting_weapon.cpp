// Exercise the real headless client against a scripted transport and server world.
#define main clientMain
#include "../client/main.cpp"
#undef main
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
            require(command.slot == 2, "Client replaced the authoritative starting sniper with slot zero");
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
        std::filesystem::create_directories(resources / "items");
        for (const auto& file : std::filesystem::directory_iterator(std::filesystem::path(MP_ROOT) / "items"))
            if (file.path().extension() == ".toml")
                std::filesystem::copy_file(file.path(), resources / "items" / file.path().filename(),
                                           std::filesystem::copy_options::overwrite_existing);
        {
            std::ofstream inventory(resources / "inventory.toml");
            inventory << "equipped = 'sniper'\n[weapons]\npistol = 12\nsniper = 5\n"
                         "[ammo]\npistol_rounds = 120\nsmg_rounds = 300\nsniper_rounds = 30\nshotgun_shells = 48\n";
            require(bool(inventory), "Could not write starting weapon fixture");
        }
        multiplayer::Options options; options.map = argv[1];
        options.trees = options.forests = false; options.zombieCount = 0;
        multiplayer::Scene scene(options);
        multiplayer::World server(scene);
        world = &server;
        player = world->join();
        require(world->players().at(player).weapons.slot() == 2, "Fixture did not equip the configured sniper");
        for (int tick = 0; tick < 20; ++tick) {
            world->step();
            require(world->snapshot(player).players.front().slot == 2,
                    "Server replaced the starting weapon before receiving input");
        }
        char program[] = "godollo_client", noTrees[] = "--no-trees", count[] = "--zombie-count", zero[] = "0";
        char headless[] = "--headless", seconds[] = "--seconds", duration[] = "0.5";
        char* args[] = {program, argv[1], noTrees, count, zero, headless, seconds, duration};
        require(clientMain(8, args) == 0 && commands > 1, "Client did not preserve its initial authoritative selection");
        require(world->players().at(player).weapons.slot() == 2, "Client commands replaced the configured starting weapon");
        // A later join must use the session's fingerprinted starting definitions,
        // even if the source inventory is edited while the server is running.
        { std::ofstream inventory(resources / "inventory.toml"); inventory << "invalid inventory\n"; }
        const auto late = world->join();
        require(world->players().at(late).weapons.slot() == 2 &&
                world->players().at(late).weapons.equipped().loaded == 5,
                "Late join reread resources or inherited another player's ammo");
        std::cout << "Starting weapon checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
