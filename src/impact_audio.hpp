#pragma once
#include "impact_synthesis.hpp"
#include <filesystem>

namespace viewer {
struct ImpactAudioStats {
    std::size_t cached = 0;
    std::uint64_t generated = 0, cacheHits = 0, fallbacks = 0;
    double lastGenerationMs = 0, maxGenerationMs = 0;
};
// OpenAL resources live on the same application thread/context as AudioSystem.
// At most two cache misses are generated per service interval; excess impacts
// play their original material/surface sample immediately. No workers or delay.
class ImpactAudio {
public:
    explicit ImpactAudio(const std::filesystem::path& directory);
    ~ImpactAudio();
    ImpactAudio(const ImpactAudio&) = delete;
    ImpactAudio& operator=(const ImpactAudio&) = delete;
    std::uint32_t buffer(const ImpactSound& sound, const std::vector<std::uint32_t>& protectedBuffers);
    void service() { remaining_ = 2; }
    ImpactAudioStats stats() const { return stats_; }
private:
    struct Base { std::uint32_t buffer = 0; int rate = 48000; std::vector<std::int16_t> samples; };
    struct Cached { ImpactKey key; std::uint32_t buffer; std::uint64_t used; };
    std::array<Base,acousticMaterialCount*2> bases_{};
    std::vector<Cached> cache_;
    ImpactAudioStats stats_;
    std::uint64_t clock_ = 0;
    unsigned remaining_ = 2;
};
} // namespace viewer
