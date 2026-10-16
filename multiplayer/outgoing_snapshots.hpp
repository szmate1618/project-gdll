#pragma once
#include "protocol.hpp"
#include <functional>
#include <optional>

namespace multiplayer {
// One active transfer plus the newest pending state. A temporary transport
// failure resumes the same fragment instead of starting another partial prefix.
class OutgoingSnapshots {
public:
    void enqueue(std::vector<std::uint8_t> bytes);
    void pump(std::size_t byteBudget, const std::function<bool(const std::vector<std::uint8_t>&)>& send);
    bool empty() const { return !active_; }
private:
    struct Transfer { std::vector<std::uint8_t> bytes; std::size_t offset = 0; std::uint64_t id = 0; };
    std::optional<Transfer> active_;
    std::optional<std::vector<std::uint8_t>> pending_;
    std::uint64_t nextMessage_ = 1;
};
} // namespace multiplayer
