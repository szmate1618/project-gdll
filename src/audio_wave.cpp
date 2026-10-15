#include "audio_wave.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <string>

namespace viewer {
namespace {
std::uint32_t little(const unsigned char* bytes, int count) {
    std::uint32_t value = 0;
    for (int i = 0; i < count; ++i) value |= std::uint32_t(bytes[i]) << (8 * i);
    return value;
}
}

AudioWave::AudioWave(const std::filesystem::path& path) : file_(path, std::ios::binary) {
    const auto invalid = [&] { return std::runtime_error("Invalid PCM16 WAV: " + path.string()); };
    if (!file_) throw std::runtime_error("Cannot open audio: " + path.string());
    file_.seekg(0, std::ios::end);
    const auto end = file_.tellg();
    if (end < 12) throw invalid();
    const auto size = static_cast<std::uint64_t>(end);
    file_.seekg(0);
    std::array<unsigned char, 16> header{};
    file_.read(reinterpret_cast<char*>(header.data()), 12);
    if (std::string(reinterpret_cast<char*>(header.data()), 4) != "RIFF" ||
        std::string(reinterpret_cast<char*>(header.data() + 8), 4) != "WAVE") throw invalid();
    const auto riffEnd = std::uint64_t(little(header.data() + 4, 4)) + 8;
    if (riffEnd > size || riffEnd < 12) throw invalid();
    bool format = false, data = false;
    std::uint64_t offset = 12;
    while (offset + 8 <= riffEnd) {
        file_.seekg(static_cast<std::streamoff>(offset));
        file_.read(reinterpret_cast<char*>(header.data()), 8);
        if (!file_) throw invalid();
        const std::string id(reinterpret_cast<char*>(header.data()), 4);
        const std::uint64_t bytes = little(header.data() + 4, 4);
        offset += 8;
        if (bytes > riffEnd - offset) throw invalid();
        if (id == "fmt ") {
            if (format || bytes < 16) throw invalid();
            file_.read(reinterpret_cast<char*>(header.data()), 16);
            channels_ = static_cast<int>(little(header.data() + 2, 2));
            const auto rate = little(header.data() + 4, 4);
            if (!file_ || little(header.data(), 2) != 1 ||
                (channels_ != 1 && channels_ != 2) || rate < 8000 || rate > 192000 ||
                little(header.data() + 14, 2) != 16 ||
                little(header.data() + 12, 2) != std::uint32_t(2 * channels_) ||
                little(header.data() + 8, 4) != rate * 2 * channels_) throw invalid();
            sampleRate_ = static_cast<int>(rate);
            format = true;
        } else if (id == "data") {
            if (data) throw invalid();
            dataOffset_ = offset;
            dataBytes_ = bytes;
            data = true;
        }
        offset += bytes + (bytes & 1);
        if (offset > riffEnd) throw invalid();
    }
    if (!format || !data || !dataBytes_ || dataBytes_ % (2 * channels_) != 0 || offset != riffEnd)
        throw invalid();
    rewind();
}

void AudioWave::rewind() {
    file_.clear();
    file_.seekg(static_cast<std::streamoff>(dataOffset_));
    if (!file_) throw std::runtime_error("Cannot seek audio stream");
    remainingBytes_ = dataBytes_;
}

std::vector<std::int16_t> AudioWave::read(std::size_t frames, bool loop) {
    if (frames > std::numeric_limits<std::size_t>::max() / (2 * channels_))
        throw std::length_error("Audio read is too large");
    const auto requested = frames * 2 * channels_;
    // Callers bound this to a short effect or one streaming buffer.
    const auto bytes = loop ? requested : std::min<std::uint64_t>(requested, remainingBytes_);
    std::vector<std::int16_t> samples(static_cast<std::size_t>(bytes / 2));
    std::array<unsigned char, 8192> scratch{};
    std::size_t written = 0;
    while (written < samples.size()) {
        if (remainingBytes_ == 0) rewind();
        const auto count = std::min<std::uint64_t>({scratch.size(), remainingBytes_, (samples.size() - written) * 2});
        file_.read(reinterpret_cast<char*>(scratch.data()), static_cast<std::streamsize>(count));
        if (!file_) throw std::runtime_error("Truncated audio stream");
        for (std::size_t i = 0; i < count; i += 2) {
            const auto value = little(scratch.data() + i, 2);
            samples[written++] = static_cast<std::int16_t>(value <= 32767 ? int(value) : int(value) - 65536);
        }
        remainingBytes_ -= count;
    }
    return samples;
}

} // namespace viewer
