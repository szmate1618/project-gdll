#include "network.hpp"
#include <steam/isteamnetworkingutils.h>
#include <stdexcept>
#include <algorithm>
#include <cstring>
#include <mutex>
#include <random>

namespace multiplayer {
namespace {
// GNS has one process-wide interface and callback queue. Serialize callback
// dispatch with endpoint access; a callback may belong to another session thread.
std::recursive_mutex runtimeMutex;
std::size_t runtimeUsers = 0;
float runtimeLag = 0, runtimeLoss = 0;
std::map<HSteamListenSocket, Network*> listeners;
std::map<HSteamNetConnection, Network*> owners;
}
Network::Network(float lag, float loss) {
    std::lock_guard<std::recursive_mutex> lock(runtimeMutex);
    // Packet simulation is a GNS global setting. Integrated server/client
    // endpoints use the same application options; never silently overwrite it.
    if (runtimeUsers && (lag != runtimeLag || loss != runtimeLoss))
        throw std::invalid_argument("Packet simulation settings must match within one process");
    if (!runtimeUsers) {
        SteamDatagramErrMsg error;
        if (!GameNetworkingSockets_Init(nullptr, error)) throw std::runtime_error(error);
    }
    sockets_ = SteamNetworkingSockets();
    group_ = sockets_->CreatePollGroup();
    if (group_ == k_HSteamNetPollGroup_Invalid) {
        if (!runtimeUsers) GameNetworkingSockets_Kill();
        throw std::runtime_error("Cannot create network poll group");
    }
    if (!runtimeUsers) {
        SteamNetworkingUtils()->SetGlobalConfigValueFloat(k_ESteamNetworkingConfig_FakePacketLag_Send, lag);
        SteamNetworkingUtils()->SetGlobalConfigValueFloat(k_ESteamNetworkingConfig_FakePacketLoss_Send, loss);
        runtimeLag = lag; runtimeLoss = loss;
    }
    ++runtimeUsers;
}
Network::~Network() {
    std::lock_guard<std::recursive_mutex> lock(runtimeMutex);
    for (const auto c : connections_) {
        owners.erase(c);
        sockets_->CloseConnection(c, 0, listener_ != k_HSteamListenSocket_Invalid ? "Host ended the session" : "Client disconnected", false);
    }
    if (listener_ != k_HSteamListenSocket_Invalid) {
        listeners.erase(listener_);
        sockets_->CloseListenSocket(listener_);
    }
    sockets_->DestroyPollGroup(group_);
    if (!--runtimeUsers) GameNetworkingSockets_Kill();
}
void Network::listen(std::uint16_t port, bool loopbackOnly, std::size_t capacity) {
    std::lock_guard<std::recursive_mutex> lock(runtimeMutex);
    if (listener_ != k_HSteamListenSocket_Invalid || !capacity || capacity > maxPlayers)
        throw std::invalid_argument("Invalid listener capacity or duplicate listen");
    capacity_ = capacity;
    SteamNetworkingIPAddr address; address.Clear(); address.m_port = port;
    if (loopbackOnly) address.SetIPv4(0x7f000001, port);
    SteamNetworkingConfigValue_t config;
    config.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged, reinterpret_cast<void*>(status));
    // This GNS version requires a nonzero listen port. For private sessions,
    // try random high ports and let the actual bind arbitrate availability.
    static std::mt19937 ports(std::random_device{}());
    std::uniform_int_distribution<unsigned int> highPort(49152, 65535);
    for (int attempt = 0; attempt < (port ? 1 : 32); ++attempt) {
        if (!port) address.m_port = static_cast<std::uint16_t>(highPort(ports));
        listener_ = sockets_->CreateListenSocketIP(address, 1, &config);
        if (listener_ != k_HSteamListenSocket_Invalid) break;
    }
    if (listener_ == k_HSteamListenSocket_Invalid) throw std::runtime_error("Cannot listen on UDP port (it may already be in use)");
    listeners.emplace(listener_, this);
}
std::uint16_t Network::listeningPort() const {
    std::lock_guard<std::recursive_mutex> lock(runtimeMutex);
    SteamNetworkingIPAddr address;
    if (!sockets_->GetListenSocketAddress(listener_, &address)) throw std::runtime_error("Cannot query server port");
    return address.m_port;
}
HSteamNetConnection Network::connect(const std::string& text) {
    std::lock_guard<std::recursive_mutex> lock(runtimeMutex);
    SteamNetworkingIPAddr address;
    if (!address.ParseString(text.c_str()) || !address.m_port) throw std::runtime_error("Use a numeric IP:port, e.g. 127.0.0.1:27020");
    SteamNetworkingConfigValue_t config;
    config.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged, reinterpret_cast<void*>(status));
    const auto c = sockets_->ConnectByIPAddress(address, 1, &config);
    if (c == k_HSteamNetConnection_Invalid) throw std::runtime_error("Cannot connect to server");
    connections_.insert(c);
    owners[c] = this;
    if (!configureLanes(c)) throw std::runtime_error("Cannot configure network priorities");
    if (!sockets_->SetConnectionPollGroup(c, group_)) throw std::runtime_error("Cannot assign connection poll group");
    return c;
}
void Network::status(SteamNetConnectionStatusChangedCallback_t* info) {
    const auto c = info->m_hConn;
    Network* owner = nullptr;
    if (const auto it = owners.find(c); it != owners.end()) owner = it->second;
    else if (const auto listener = listeners.find(info->m_info.m_hListenSocket); listener != listeners.end())
        owner = listener->second;
    if (!owner) return; // Late callback for an endpoint already destroyed.
    auto& self = *owner;
    if (info->m_info.m_eState == k_ESteamNetworkingConnectionState_Connecting && self.listener_ != k_HSteamListenSocket_Invalid) {
        if (self.connections_.size() >= self.capacity_ || self.sockets_->AcceptConnection(c) != k_EResultOK) {
            self.sockets_->CloseConnection(c, 0, "Server full", false); return;
        }
        self.connections_.insert(c);
        owners[c] = &self;
        if (!self.configureLanes(c) || !self.sockets_->SetConnectionPollGroup(c, self.group_)) {
            self.close(c, "Cannot configure peer transport"); return;
        }
    }
    self.pendingEvents_.push_back({c, info->m_info.m_eState, info->m_info.m_szEndDebug});
}
std::vector<std::pair<HSteamNetConnection, Message>> Network::receive() {
    std::lock_guard<std::recursive_mutex> lock(runtimeMutex);
    sockets_->RunCallbacks();
    for (const auto c : connections_) pump(c);
    std::vector<std::pair<HSteamNetConnection, Message>> result;
    // Bound receive work so a peer cannot indefinitely postpone simulation ticks.
    for (int i = 0; i < 128; ++i) {
        SteamNetworkingMessage_t* raw = nullptr;
        const int count = sockets_->ReceiveMessagesOnPollGroup(group_, &raw, 1);
        if (count < 0) throw std::runtime_error("Network receive failed");
        if (!count) break;
        const auto c = raw->m_conn;
        try {
            if (connections_.count(c)) {
                auto message = decode(raw->m_pData, raw->m_cbSize);
                if (const auto* fragment = std::get_if<Fragment>(&message)) {
                    if (const auto bytes = fragments_[c].add(*fragment)) {
                        auto complete = decode(bytes->data(), bytes->size());
                        if (std::holds_alternative<Fragment>(complete)) throw std::runtime_error("Nested fragments");
                        result.emplace_back(c, std::move(complete));
                    }
                } else result.emplace_back(c, std::move(message));
            }
        }
        catch (const std::exception&) { close(c, "Malformed or incompatible message"); }
        raw->Release();
    }
    events.clear(); events.swap(pendingEvents_);
    return result;
}
double Network::estimatedOneWaySeconds(HSteamNetConnection c) const {
    std::lock_guard<std::recursive_mutex> lock(runtimeMutex);
    if (!connections_.count(c)) return 0;
    SteamNetConnectionRealTimeStatus_t status{};
    if (sockets_->GetConnectionRealTimeStatus(c, &status, 0, nullptr) != k_EResultOK ||
        status.m_eState != k_ESteamNetworkingConnectionState_Connected || status.m_nPing <= 0)
        return 0;
    return std::min(.5, double(status.m_nPing) / 2000);
}
bool Network::send(HSteamNetConnection c, const Message& message, bool reliable) {
    std::lock_guard<std::recursive_mutex> lock(runtimeMutex);
    if (!connections_.count(c)) return false;
    if (std::holds_alternative<BirdFrame>(message) && !reliable)
        throw std::invalid_argument("Boid commands require ordered reliable delivery");
    auto bytes = encode(message);
    if (std::holds_alternative<CorpsePose>(message) || std::holds_alternative<ZombieSetups>(message) ||
        std::holds_alternative<CharacterAppearanceChunk>(message)) {
        if (!reliable) throw std::invalid_argument("Appearance, zombie setup and physics poses require reliable bulk delivery");
        SteamNetConnectionRealTimeStatus_t status{};
        SteamNetConnectionRealTimeLaneStatus_t lanes[2]{};
        if (sockets_->GetConnectionRealTimeStatus(c, &status, 2, lanes) != k_EResultOK ||
            lanes[1].m_cbPendingReliable + lanes[1].m_cbSentUnackedReliable + bytes.size() > 32 * 1024)
            return false;
        auto* packet = SteamNetworkingUtils()->AllocateMessage(static_cast<int>(bytes.size()));
        if (!packet) return false;
        std::memcpy(packet->m_pData, bytes.data(), bytes.size());
        packet->m_conn = c; packet->m_nFlags = k_nSteamNetworkingSend_Reliable; packet->m_idxLane = 1;
        int64 result = 0;
        sockets_->SendMessages(1, &packet, &result);
        return result > 0;
    }
    if (!reliable && (bytes.size() > fragmentBytes || std::holds_alternative<Snapshot>(message))) {
        outgoing_[c].enqueue(std::move(bytes)); pump(c); return true;
    }
    const auto result = sockets_->SendMessageToConnection(c, bytes.data(), static_cast<std::uint32_t>(bytes.size()),
        reliable ? k_nSteamNetworkingSend_Reliable : k_nSteamNetworkingSend_UnreliableNoDelay, nullptr);
    if (result == k_EResultLimitExceeded && reliable) close(c, "Outgoing queue full");
    return result == k_EResultOK;
}
bool Network::configureLanes(HSteamNetConnection c) {
    // Reserve a share for bulk even when essential snapshots saturate the link.
    // Separate lanes also prevent reliable pose retransmits blocking movement.
    const int priorities[] = {0, 0};
    const uint16 weights[] = {4, 1};
    return sockets_->ConfigureConnectionLanes(c, 2, priorities, weights) == k_EResultOK;
}
void Network::pump(HSteamNetConnection c) {
    const auto queued = outgoing_.find(c);
    if (queued == outgoing_.end() || queued->second.empty()) return;
    SteamNetConnectionRealTimeStatus_t status{};
    SteamNetConnectionRealTimeLaneStatus_t lanes[2]{};
    if (sockets_->GetConnectionRealTimeStatus(c, &status, 2, lanes) != k_EResultOK ||
        status.m_eState != k_ESteamNetworkingConnectionState_Connected) return;
    // Bound the essential lane to roughly 10 ms of transport capacity. Query
    // before each pump so repeated calls in one simulation tick cannot burst.
    const int window = std::clamp(status.m_nSendRateBytesPerSecond / 100, 1200, 16 * 1024);
    const int available = window - lanes[0].m_cbPendingUnreliable - lanes[0].m_cbPendingReliable;
    if (available <= 0) return;
    queued->second.pump(static_cast<std::size_t>(available), [&](const auto& packet) {
        return sockets_->SendMessageToConnection(c, packet.data(), static_cast<std::uint32_t>(packet.size()),
            k_nSteamNetworkingSend_UnreliableNoDelay, nullptr) == k_EResultOK;
    });
}
void Network::close(HSteamNetConnection c, const std::string& reason) {
    std::lock_guard<std::recursive_mutex> lock(runtimeMutex);
    if (connections_.erase(c)) {
        owners.erase(c);
        fragments_.erase(c);
        outgoing_.erase(c);
        sockets_->CloseConnection(c, 0, reason.c_str(), false);
        pendingEvents_.push_back({c, k_ESteamNetworkingConnectionState_ProblemDetectedLocally, reason});
    }
}
} // namespace multiplayer
