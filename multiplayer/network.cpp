#include "network.hpp"
#include <steam/isteamnetworkingutils.h>
#include <stdexcept>
#include <algorithm>
#include <cstring>

namespace multiplayer {
thread_local Network* Network::callbackOwner_ = nullptr;
Network::Network(float lag, float loss) {
    SteamDatagramErrMsg error;
    if (!GameNetworkingSockets_Init(nullptr, error)) throw std::runtime_error(error);
    sockets_ = SteamNetworkingSockets();
    group_ = sockets_->CreatePollGroup();
    if (group_ == k_HSteamNetPollGroup_Invalid) {
        GameNetworkingSockets_Kill(); throw std::runtime_error("Cannot create network poll group");
    }
    SteamNetworkingUtils()->SetGlobalConfigValueFloat(k_ESteamNetworkingConfig_FakePacketLag_Send, lag);
    SteamNetworkingUtils()->SetGlobalConfigValueFloat(k_ESteamNetworkingConfig_FakePacketLoss_Send, loss);
}
Network::~Network() {
    for (const auto c : connections_) sockets_->CloseConnection(c, 0, "Shutdown", false);
    if (listener_ != k_HSteamListenSocket_Invalid) sockets_->CloseListenSocket(listener_);
    sockets_->DestroyPollGroup(group_);
    GameNetworkingSockets_Kill();
}
void Network::listen(std::uint16_t port) {
    SteamNetworkingIPAddr address; address.Clear(); address.m_port = port;
    SteamNetworkingConfigValue_t callback;
    callback.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged, reinterpret_cast<void*>(status));
    listener_ = sockets_->CreateListenSocketIP(address, 1, &callback);
    if (listener_ == k_HSteamListenSocket_Invalid) throw std::runtime_error("Cannot listen on UDP port");
}
HSteamNetConnection Network::connect(const std::string& text) {
    SteamNetworkingIPAddr address;
    if (!address.ParseString(text.c_str()) || !address.m_port) throw std::runtime_error("Use a numeric IP:port, e.g. 127.0.0.1:27020");
    SteamNetworkingConfigValue_t callback;
    callback.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged, reinterpret_cast<void*>(status));
    const auto c = sockets_->ConnectByIPAddress(address, 1, &callback);
    if (c == k_HSteamNetConnection_Invalid) throw std::runtime_error("Cannot connect to server");
    connections_.insert(c);
    if (!configureLanes(c)) throw std::runtime_error("Cannot configure network priorities");
    if (!sockets_->SetConnectionPollGroup(c, group_)) throw std::runtime_error("Cannot assign connection poll group");
    return c;
}
void Network::status(SteamNetConnectionStatusChangedCallback_t* info) {
    auto& self = *callbackOwner_;
    const auto c = info->m_hConn;
    if (info->m_info.m_eState == k_ESteamNetworkingConnectionState_Connecting && self.listener_ != k_HSteamListenSocket_Invalid) {
        if (self.connections_.size() >= maxPlayers || self.sockets_->AcceptConnection(c) != k_EResultOK) {
            self.sockets_->CloseConnection(c, 0, "Server full", false); return;
        }
        self.connections_.insert(c);
        if (!self.configureLanes(c) || !self.sockets_->SetConnectionPollGroup(c, self.group_)) {
            self.close(c, "Cannot configure peer transport"); return;
        }
    }
    self.pendingEvents_.push_back({c, info->m_info.m_eState, info->m_info.m_szEndDebug});
}
std::vector<std::pair<HSteamNetConnection, Message>> Network::receive() {
    callbackOwner_ = this; sockets_->RunCallbacks(); callbackOwner_ = nullptr;
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
bool Network::send(HSteamNetConnection c, const Message& message, bool reliable) {
    if (!connections_.count(c)) return false;
    auto bytes = encode(message);
    if (std::holds_alternative<CorpsePose>(message)) {
        if (!reliable) throw std::invalid_argument("Corpse poses require reliable bulk delivery");
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
    if (connections_.erase(c)) {
        fragments_.erase(c);
        outgoing_.erase(c);
        sockets_->CloseConnection(c, 0, reason.c_str(), false);
        pendingEvents_.push_back({c, k_ESteamNetworkingConnectionState_ProblemDetectedLocally, reason});
    }
}
} // namespace multiplayer
