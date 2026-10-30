#include "character_appearances.hpp"
#include <algorithm>
#include <stdexcept>

namespace multiplayer {
std::optional<std::vector<std::uint8_t>> CharacterAppearanceAssembly::add(const CharacterAppearanceChunk& chunk) {
    validateCharacterAppearanceChunk(chunk);
    if (!chunk.totalBytes || complete_ || chunk.offset != bytes_.size() || (total_ && total_ != chunk.totalBytes))
        throw std::runtime_error("Unexpected or inconsistent character appearance upload");
    if (!total_) { total_ = chunk.totalBytes; bytes_.reserve(total_); }
    bytes_.insert(bytes_.end(), chunk.bytes.begin(), chunk.bytes.end());
    if (bytes_.size() != total_) return {};
    complete_ = true;
    return std::move(bytes_);
}
void CharacterAppearanceOutbox::push(std::uint32_t player, std::shared_ptr<const std::vector<std::uint8_t>> bytes) {
    if (pending_.size() >= maxPlayers * 4) { overflow_ = true; return; }
    pending_.push_back({player, 0, std::move(bytes)});
}
void CharacterAppearanceOutbox::remove(std::uint32_t player) {
    pending_.erase(std::remove_if(pending_.begin(), pending_.end(), [&](const auto& job) { return job.player == player; }), pending_.end());
    push(player, {});
}
bool CharacterAppearanceOutbox::pump(const std::function<bool(const CharacterAppearanceChunk&)>& send) {
    if (overflow_) return false;
    for (int i = 0; i < 4 && !pending_.empty(); ++i) {
        auto& next = pending_.front();
        CharacterAppearanceChunk chunk; chunk.player = next.player; chunk.offset = next.offset;
        if (next.bytes) {
            chunk.totalBytes = std::uint32_t(next.bytes->size());
            const auto end = std::min(next.bytes->size(), std::size_t(next.offset) + characterAppearanceChunkBytes);
            chunk.bytes.assign(next.bytes->begin() + next.offset, next.bytes->begin() + end);
        }
        if (!send(chunk)) break;
        next.offset += std::uint32_t(chunk.bytes.size());
        if (!next.bytes || next.offset == next.bytes->size()) pending_.pop_front();
    }
    return true;
}
void ServerCharacterAppearances::join(std::uint32_t player) {
    if (!player || players_.size() >= maxPlayers || players_.count(player)) throw std::runtime_error("Invalid character appearance owner");
    auto& joined = players_[player];
    for (const auto& [id, peer] : players_)
        if (peer.appearance) joined.outgoing.push(id, peer.appearance);
}
void ServerCharacterAppearances::leave(std::uint32_t player) {
    if (!players_.erase(player)) return;
    for (auto& [id, peer] : players_) { (void)id; peer.outgoing.remove(player); }
}
bool ServerCharacterAppearances::receive(std::uint32_t player, const CharacterAppearanceChunk& upload) {
    // The connection's joined ID is authoritative. Clients cannot assign an
    // appearance to another player, remove one, or change it repeatedly.
    if (upload.player || !upload.totalBytes) throw std::runtime_error("Spoofed character appearance owner");
    auto& owner = players_.at(player);
    auto encoded = owner.upload.add(upload);
    if (!encoded) return false;
    decodeCharacterAppearance(*encoded); // Fully validate before caching or distributing.
    owner.appearance = std::make_shared<const std::vector<std::uint8_t>>(std::move(*encoded));
    for (auto& [id, peer] : players_) { (void)id; peer.outgoing.push(player, owner.appearance); }
    return true;
}
bool ServerCharacterAppearances::pump(std::uint32_t player, const std::function<bool(const CharacterAppearanceChunk&)>& send) {
    return players_.at(player).outgoing.pump(send);
}
bool ClientCharacterAppearances::receive(const CharacterAppearanceChunk& chunk) {
    validateCharacterAppearanceChunk(chunk);
    if (!chunk.player) throw std::runtime_error("Server character appearance lacks an owner");
    if (!chunk.totalBytes) { players_.erase(chunk.player); return false; }
    if (!players_.count(chunk.player) && players_.size() >= maxPlayers) throw std::runtime_error("Too many character appearances");
    auto& player = players_[chunk.player];
    if (const auto bytes = player.assembly.add(chunk)) {
        player.appearance = decodeCharacterAppearance(*bytes);
        return true;
    }
    return false;
}
const CharacterAppearance& ClientCharacterAppearances::get(std::uint32_t player) const { return players_.at(player).appearance.value(); }
} // namespace multiplayer
