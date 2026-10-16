#include "outgoing_snapshots.hpp"
#include <algorithm>
#include <stdexcept>

namespace multiplayer {
void OutgoingSnapshots::enqueue(std::vector<std::uint8_t> bytes) {
    if (bytes.empty() || bytes.size() > maxMessageBytes) throw std::invalid_argument("Invalid outgoing snapshot size");
    if (active_) pending_ = std::move(bytes);
    else active_ = Transfer{std::move(bytes), 0, nextMessage_++};
}
void OutgoingSnapshots::pump(std::size_t budget,
        const std::function<bool(const std::vector<std::uint8_t>&)>& send) {
    while (active_) {
        auto& transfer = *active_;
        auto end = transfer.bytes.size();
        std::vector<std::uint8_t> packet;
        if (end <= fragmentBytes) packet = transfer.bytes;
        else {
            end = std::min(end, transfer.offset + fragmentBytes);
            Fragment fragment{transfer.id, static_cast<std::uint32_t>(transfer.bytes.size()),
                static_cast<std::uint16_t>(transfer.offset / fragmentBytes), {}};
            fragment.bytes.assign(transfer.bytes.begin() + transfer.offset, transfer.bytes.begin() + end);
            packet = encode(fragment);
        }
        if (packet.size() > budget || !send(packet)) return;
        budget -= packet.size();
        transfer.offset = end;
        if (end != transfer.bytes.size()) continue;
        active_.reset();
        if (pending_) {
            active_ = Transfer{std::move(*pending_), 0, nextMessage_++};
            pending_.reset();
        }
    }
}
} // namespace multiplayer
