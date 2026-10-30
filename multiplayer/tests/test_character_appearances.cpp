#include "character_appearances.hpp"
#include <iostream>
#include <random>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
template<class F> void rejects(F action) {
    bool failed = false; try { action(); } catch (const std::exception&) { failed = true; }
    require(failed, "Malformed or spoofed appearance was accepted");
}
multiplayer::CharacterAppearance solid(std::uint8_t red) {
    multiplayer::CharacterAppearance result; result.rgb.resize(multiplayer::characterTextureBytes);
    for (std::size_t i = 0; i < result.rgb.size(); i += 3) { result.rgb[i] = red; result.rgb[i + 1] = 50; result.rgb[i + 2] = 90; }
    return result;
}
void check() {
    using namespace multiplayer;
    const auto first = solid(200);
    const auto compressed = encodeCharacterAppearance(first);
    require(compressed.size() < 100 && decodeCharacterAppearance(compressed).rgb == first.rgb, "Flat character textures must compress losslessly");
    auto noise = first; std::mt19937 random(42);
    for (auto& byte : noise.rgb) byte = std::uint8_t(random());
    const auto raw = encodeCharacterAppearance(noise);
    require(raw.size() == maxCharacterAppearanceBytes && raw.front() == 0 && decodeCharacterAppearance(raw).rgb == noise.rgb,
            "Incompressible painting must fall back to bounded raw RGB");
    rejects([&] { decodeCharacterAppearance({}); }); rejects([&] { decodeCharacterAppearance({1,0,0,3,4,5}); });
    rejects([&] { decodeCharacterAppearance({1,1,0,3,4,5}); }); rejects([&] { decodeCharacterAppearance({2}); });
    rejects([&] { encodeCharacterAppearance(CharacterAppearance{}); });
    ServerCharacterAppearances server; server.join(1);
    CharacterAppearanceOutbox upload;
    upload.push(0, std::make_shared<const std::vector<std::uint8_t>>(compressed));
    upload.pump([&](const auto& chunk) { return server.receive(1, chunk); });
    // Join after player one's upload: the newcomer needs the retained appearance.
    server.join(2);
    ClientCharacterAppearances client1, client2;
    for (int n = 0; n < 10; ++n) {
        server.pump(1, [&](const auto& chunk) { client1.receive(chunk); return true; });
        server.pump(2, [&](const auto& chunk) { client2.receive(chunk); return true; });
    }
    require(client1.get(1).rgb == first.rgb && client2.get(1).rgb == first.rgb, "Existing players and late arrivals must receive the retained texture");
    CharacterAppearanceOutbox secondUpload;
    secondUpload.push(0, std::make_shared<const std::vector<std::uint8_t>>(raw));
    // Transport rejection must not advance the application's offset.
    secondUpload.pump([](const auto&) { return false; });
    bool complete = false;
    for (int n = 0; n < 100; ++n) secondUpload.pump([&](const auto& chunk) {
        const auto packet = encode(chunk);
        auto copy = std::get<CharacterAppearanceChunk>(decode(packet.data(), packet.size()));
        complete |= server.receive(2, copy); return true;
    });
    require(complete, "A large reliable upload did not finish after backpressure");
    for (int n = 0; n < 100; ++n) {
        server.pump(1, [&](const auto& chunk) { client1.receive(chunk); return true; });
        server.pump(2, [&](const auto& chunk) { client2.receive(chunk); return true; });
    }
    require(client1.get(2).rgb == noise.rgb && client2.get(2).rgb == noise.rgb, "New arrivals must publish all six textures coherently to existing players");
    server.join(3);
    const CharacterAppearanceChunk spoof{1, std::uint32_t(compressed.size()), 0, compressed};
    rejects([&] { server.receive(3, spoof); });
    rejects([&] { server.receive(3, {1,0,0,{}}); });
    server.leave(1);
    server.pump(2, [&](const auto& chunk) { client2.receive(chunk); return true; });
    rejects([&] { client2.get(1); });
    ClientCharacterAppearances client3;
    for (int n = 0; n < 100; ++n) server.pump(3, [&](const auto& chunk) { client3.receive(chunk); return true; });
    require(client3.get(2).rgb == noise.rgb, "Disconnect cleanup must preserve the other active player's appearance");
    rejects([&] { client3.get(1); });
    CharacterAppearanceAssembly partial;
    partial.add({0, std::uint32_t(raw.size()), 0, {raw.begin(), raw.begin() + characterAppearanceChunkBytes}});
    rejects([&] { partial.add({0, std::uint32_t(raw.size()), 0, {raw.begin(), raw.begin() + characterAppearanceChunkBytes}}); });
    rejects([&] { encode(CharacterAppearanceChunk{0, std::uint32_t(maxCharacterAppearanceBytes + 1), 0, std::vector<std::uint8_t>(characterAppearanceChunkBytes)}); });
}
}
int main() {
    try { check(); std::cout << "Appearance compression, bounds, backpressure, late joins, ownership and disconnect cleanup passed\n"; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
