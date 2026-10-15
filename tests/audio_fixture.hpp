#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

// Independent tiny WAV writer for decoder/backend fixtures. Explicit byte order
// and sample values let tests check decoding without using the production code.
inline void writeAudioFixture(const std::filesystem::path& path, int channels,
                              const std::vector<std::int16_t>& samples, bool oddChunk = false) {
    std::ofstream file(path, std::ios::binary);
    const auto number = [&](std::uint32_t value, int bytes) {
        for (int i = 0; i < bytes; ++i) file.put(static_cast<char>((value >> (8 * i)) & 255));
    };
    file.write("RIFF", 4);
    number(static_cast<std::uint32_t>(36 + samples.size() * 2 + (oddChunk ? 12 : 0)), 4);
    file.write("WAVE", 4);
    if (oddChunk) { file.write("JUNK", 4); number(3, 4); file.write("abc\0", 4); }
    file.write("fmt ", 4); number(16, 4); number(1, 2); number(channels, 2);
    number(48000, 4); number(48000 * 2 * channels, 4); number(2 * channels, 2); number(16, 2);
    file.write("data", 4); number(static_cast<std::uint32_t>(samples.size() * 2), 4);
    for (auto sample : samples) number(static_cast<std::uint16_t>(sample), 2);
}
