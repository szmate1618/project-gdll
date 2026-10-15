#include "audio_wave.hpp"
#include "audio_fixture.hpp"

#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void mustReject(const std::filesystem::path& path) {
    bool rejected = false;
    try { viewer::AudioWave wave(path); }
    catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "Invalid WAV must fail before playback");
}
}
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected scratch directory");
        const std::filesystem::path dir(argv[1]);
        std::filesystem::create_directories(dir);
        const auto mono = dir / "mono.wav";
        writeAudioFixture(mono, 1, {-32768, -1, 0, 32767}, true);
        viewer::AudioWave wave(mono);
        require(wave.channels() == 1 && wave.sampleRate() == 48000 && wave.frames() == 4,
                "Header parsing handles unknown odd chunks and PCM frame count");
        require(wave.read(2) == std::vector<std::int16_t>({-32768, -1}), "Signed little-endian samples decode exactly");
        require(wave.read(100) == std::vector<std::int16_t>({0, 32767}), "Reading stops at data boundary");
        require(wave.read(1).empty(), "Non-looping EOF stays empty");
        require(wave.read(6, true) == std::vector<std::int16_t>({-32768, -1, 0, 32767, -32768, -1}),
                "Looping fills a buffer even when the file is shorter than it");
        wave.rewind();
        require(wave.read(1) == std::vector<std::int16_t>{-32768}, "Rewind resets EOF and cursor");
        const auto stereo = dir / "stereo.wav";
        writeAudioFixture(stereo, 2, {1, -1, 2, -2});
        viewer::AudioWave pair(stereo);
        require(pair.frames() == 2 && pair.read(3, true) == std::vector<std::int16_t>({1, -1, 2, -2, 1, -1}),
                "Streaming preserves interleaved stereo frames across loops");
        const auto truncated = dir / "truncated.wav";
        writeAudioFixture(truncated, 1, {1, 2, 3});
        std::filesystem::resize_file(truncated, std::filesystem::file_size(truncated) - 1);
        mustReject(truncated);
        const auto partial = dir / "partial.wav";
        writeAudioFixture(partial, 2, {1, 2, 3});
        mustReject(partial);
        const auto empty = dir / "empty.wav";
        writeAudioFixture(empty, 1, {});
        mustReject(empty); // Looping an empty stream must never hang.
        const auto bad = dir / "bad-format.wav";
        writeAudioFixture(bad, 1, {1, 2});
        {
            std::fstream file(bad, std::ios::binary | std::ios::in | std::ios::out);
            file.seekp(20); file.put(3); // IEEE float, not PCM16.
        }
        mustReject(bad);
        mustReject(dir / "missing.wav");
        std::cout << "Audio WAV tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
