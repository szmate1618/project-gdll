#include "network.hpp"
#include <steam/isteamnetworkingutils.h>
#include <stdexcept>

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
        self.sockets_->SetConnectionPollGroup(c, self.group_);
    }
    self.pendingEvents_.push_back({c, info->m_info.m_eState, info->m_info.m_szEndDebug});
}
std::vector<std::pair<HSteamNetConnection, Message>> Network::receive() {
    callbackOwner_ = this; sockets_->RunCallbacks(); callbackOwner_ = nullptr;
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
    const auto bytes = encode(message);
    const auto transmit = [&](const std::vector<std::uint8_t>& data) {
        const auto result = sockets_->SendMessageToConnection(c, data.data(), static_cast<std::uint32_t>(data.size()),
            reliable ? k_nSteamNetworkingSend_Reliable : k_nSteamNetworkingSend_UnreliableNoDelay, nullptr);
        if (result == k_EResultLimitExceeded && reliable) close(c, "Outgoing queue full");
        return result == k_EResultOK;
    };
    if (reliable || bytes.size() <= fragmentBytes) return transmit(bytes);
    // GNS v1.4.1 promotes unreliable messages over 16.5 KB to reliable. Small
    // application fragments keep obsolete snapshots out of its reliable queue.
    const auto messageId = nextMessage_++;
    for (std::size_t offset = 0; offset < bytes.size(); offset += fragmentBytes) {
        Fragment f{messageId, static_cast<std::uint32_t>(bytes.size()), static_cast<std::uint16_t>(offset / fragmentBytes), {}};
        const auto end = std::min(bytes.size(), offset + fragmentBytes);
        f.bytes.assign(bytes.begin() + offset, bytes.begin() + end);
        if (!transmit(encode(f))) return false;
    }
    return true;
}
void Network::close(HSteamNetConnection c, const std::string& reason) {
    if (connections_.erase(c)) {
        fragments_.erase(c);
        sockets_->CloseConnection(c, 0, reason.c_str(), false);
        pendingEvents_.push_back({c, k_ESteamNetworkingConnectionState_ProblemDetectedLocally, reason});
    }
}
} // namespace multiplayer
