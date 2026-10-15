#include "fragments.hpp"
#include <algorithm>
#include <stdexcept>

namespace multiplayer {
std::optional<std::vector<std::uint8_t>> Fragments::add(const Fragment& f) {
    if (f.message <= completed_) return std::nullopt;
    const auto count = (f.totalBytes + fragmentBytes - 1) / fragmentBytes;
    if (!f.message || f.totalBytes <= fragmentBytes || f.totalBytes > maxMessageBytes || f.index >= count ||
        f.bytes.size() != std::min(fragmentBytes, f.totalBytes - f.index * fragmentBytes))
        throw std::runtime_error("Invalid multiplayer fragment");
    auto it = pending_.find(f.message);
    if (it == pending_.end()) {
        if (pending_.size() == 3) {
            if (f.message < pending_.begin()->first) return std::nullopt;
            pending_.erase(pending_.begin());
        }
        Assembly a; a.total = f.totalBytes; a.parts.resize(count);
        it = pending_.emplace(f.message, std::move(a)).first;
    }
    auto& assembly = it->second;
    if (assembly.total != f.totalBytes) throw std::runtime_error("Conflicting fragment size");
    auto& part = assembly.parts.at(f.index);
    if (part.empty()) { part = f.bytes; ++assembly.received; }
    else if (part != f.bytes) throw std::runtime_error("Conflicting duplicate fragment");
    if (assembly.received != assembly.parts.size()) return std::nullopt;
    std::vector<std::uint8_t> result;
    result.reserve(assembly.total);
    for (const auto& bytes : assembly.parts) result.insert(result.end(), bytes.begin(), bytes.end());
    completed_ = f.message;
    pending_.erase(pending_.begin(), pending_.upper_bound(completed_));
    return result;
}
} // namespace multiplayer
