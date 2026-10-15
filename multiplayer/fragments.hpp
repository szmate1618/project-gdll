#pragma once
#include "protocol.hpp"
#include <map>
#include <optional>

namespace multiplayer {
// Keep only three in-flight replaceable messages per connection. Incomplete
// older snapshots never block publication of a newer complete snapshot.
class Fragments {
public:
    std::optional<std::vector<std::uint8_t>> add(const Fragment& fragment);
private:
    struct Assembly {
        std::uint32_t total = 0;
        std::size_t received = 0;
        std::vector<std::vector<std::uint8_t>> parts;
    };
    std::map<std::uint64_t, Assembly> pending_;
    std::uint64_t completed_ = 0;
};
} // namespace multiplayer
