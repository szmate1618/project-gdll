#pragma once
#include "protocol.hpp"
#include <deque>
#include <functional>
#include <map>
#include <memory>

namespace multiplayer {
class CharacterAppearanceAssembly {
public:
    std::optional<std::vector<std::uint8_t>> add(const CharacterAppearanceChunk& chunk);
private:
    std::vector<std::uint8_t> bytes_;
    std::uint32_t total_ = 0;
    bool complete_ = false;
};
// Application-side backpressure: immutable data shared by recipients, with at
// most four chunks submitted per pump. Offsets advance only after acceptance.
class CharacterAppearanceOutbox {
public:
    void push(std::uint32_t player, std::shared_ptr<const std::vector<std::uint8_t>> bytes);
    void remove(std::uint32_t player);
    bool pump(const std::function<bool(const CharacterAppearanceChunk&)>& send);
private:
    struct Transfer { std::uint32_t player = 0, offset = 0; std::shared_ptr<const std::vector<std::uint8_t>> bytes; };
    std::deque<Transfer> pending_;
    bool overflow_ = false;
};
class ServerCharacterAppearances {
public:
    void join(std::uint32_t player);
    void leave(std::uint32_t player);
    bool receive(std::uint32_t player, const CharacterAppearanceChunk& upload);
    bool pump(std::uint32_t player, const std::function<bool(const CharacterAppearanceChunk&)>& send);
private:
    struct Player {
        CharacterAppearanceAssembly upload;
        std::shared_ptr<const std::vector<std::uint8_t>> appearance;
        CharacterAppearanceOutbox outgoing;
    };
    std::map<std::uint32_t, Player> players_;
};
class ClientCharacterAppearances {
public:
    // Returns true only for a complete, validated appearance. Zero-size server
    // messages erase both complete and partial state on the ordered bulk lane.
    bool receive(const CharacterAppearanceChunk& chunk);
    const CharacterAppearance& get(std::uint32_t player) const;
private:
    struct Player { CharacterAppearanceAssembly assembly; std::optional<CharacterAppearance> appearance; };
    std::map<std::uint32_t, Player> players_;
};
} // namespace multiplayer
