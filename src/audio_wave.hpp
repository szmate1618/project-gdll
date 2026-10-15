#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace viewer {

// Seekable RIFF/WAVE PCM16 decoder. Frames contain one sample per channel.
// Owns its file; reading ambience never allocates the complete recording.
class AudioWave {
public:
    explicit AudioWave(const std::filesystem::path& path);
    int channels() const { return channels_; }
    int sampleRate() const { return sampleRate_; }
    std::uint64_t frames() const { return dataBytes_ / (2 * channels_); }
    double seconds() const { return static_cast<double>(frames()) / sampleRate_; }
    std::vector<std::int16_t> read(std::size_t frames, bool loop = false);
    void rewind();

private:
    std::ifstream file_;
    int channels_ = 0, sampleRate_ = 0;
    std::uint64_t dataOffset_ = 0, dataBytes_ = 0, remainingBytes_ = 0;
};

} // namespace viewer
