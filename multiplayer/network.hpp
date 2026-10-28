#pragma once
#include "protocol.hpp"
#include "fragments.hpp"
#include "outgoing_snapshots.hpp"
#include <steam/steamnetworkingsockets.h>
#include <set>
#include <utility>

namespace multiplayer {
class Network {
public:
    struct Event { HSteamNetConnection connection; ESteamNetworkingConnectionState state; std::string reason; };
    explicit Network(float fakeLagMs = 0, float fakeLossPercent = 0);
    ~Network();
    Network(const Network&) = delete;
    Network& operator=(const Network&) = delete;
    void listen(std::uint16_t port, bool loopbackOnly = false, std::size_t capacity = maxPlayers);
    std::uint16_t listeningPort() const;
    HSteamNetConnection connect(const std::string& address);
    std::vector<std::pair<HSteamNetConnection, Message>> receive();
    std::vector<Event> events;
    // Unreliable large messages are accepted into a bounded, replaceable queue.
    // CorpsePose must be reliable and uses the lower-priority bulk lane.
    // BirdFrame must be reliable: its watermark completes an ordered tick range.
    bool send(HSteamNetConnection connection, const Message& message, bool reliable);
    void close(HSteamNetConnection connection, const std::string& reason);
private:
    static void status(SteamNetConnectionStatusChangedCallback_t* info);
    bool configureLanes(HSteamNetConnection connection);
    void pump(HSteamNetConnection connection);
    ISteamNetworkingSockets* sockets_ = nullptr;
    HSteamListenSocket listener_ = k_HSteamListenSocket_Invalid;
    HSteamNetPollGroup group_ = k_HSteamNetPollGroup_Invalid;
    std::set<HSteamNetConnection> connections_;
    std::vector<Event> pendingEvents_;
    std::map<HSteamNetConnection, Fragments> fragments_;
    std::map<HSteamNetConnection, OutgoingSnapshots> outgoing_;
    std::size_t capacity_ = maxPlayers;
};
} // namespace multiplayer
