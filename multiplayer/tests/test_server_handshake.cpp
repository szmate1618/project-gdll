// Drive the real server entry point with scripted transport events.
#define main serverMain
#include "../server/main.cpp"
#undef main

namespace {
std::string fingerprint, closeReason;
int received = 0, welcomes = 0, snapshots = 0, closes = 0;
bool earlyHello = false;
constexpr HSteamNetConnection connection = 1;
}

// Link this transport in place of network.cpp so callback ordering is deterministic.
namespace multiplayer {
Network::Network(float, float) {}
Network::~Network() = default;
void Network::listen(std::uint16_t) {}
std::vector<std::pair<HSteamNetConnection, Message>> Network::receive() {
    events.clear();
    if (received++ == 0) {
        events.push_back({connection, k_ESteamNetworkingConnectionState_Connecting, {}});
        if (earlyHello) return {{connection, Hello{fingerprint}}};
        return {};
    }
    if (received == 2) {
        events.push_back({connection, k_ESteamNetworkingConnectionState_Connected, {}});
        if (!earlyHello) return {{connection, Hello{fingerprint}}};
        Command command; command.sequence = 1;
        return {{connection, Commands{{command}}}};
    }
    running = 0;
    return {{connection, Goodbye{}}};
}
bool Network::send(HSteamNetConnection c, const Message& message, bool reliable) {
    if (c != connection) throw std::runtime_error("Unexpected handshake recipient");
    if (const auto* welcome = std::get_if<Welcome>(&message)) {
        if (!welcome->player || !reliable) throw std::runtime_error("Invalid welcome");
        ++welcomes;
    } else if (const auto* snapshot = std::get_if<Snapshot>(&message)) {
        if (snapshot->players.size() != 1) throw std::runtime_error("Joined player missing from snapshot");
        ++snapshots;
    }
    return true;
}
void Network::close(HSteamNetConnection, const std::string& reason) { ++closes; closeReason = reason; }
}

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("Test asset argument missing");
        multiplayer::Options options; options.map = argv[1];
        options.trees = options.forests = false; options.zombieCount = 0;
        fingerprint = multiplayer::Scene(options).fingerprint;
        for (const bool early : {false, true}) {
            earlyHello = early; received = welcomes = snapshots = closes = 0;
            closeReason.clear(); running = 1;
            char program[] = "godollo_server", noTrees[] = "--no-trees";
            char count[] = "--zombie-count", zero[] = "0";
            char* args[] = {program, argv[1], noTrees, count, zero};
            const auto result = serverMain(5, args);
            if (result || welcomes != 1 || snapshots < 1 || closes != 1 || closeReason != "Client left")
                throw std::runtime_error("Hello before Connected was lost or later Connected reset the joined peer");
        }
        std::cout << "Server handshake ordering checks passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
