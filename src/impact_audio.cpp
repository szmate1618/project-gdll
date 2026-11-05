#include "impact_audio.hpp"
#include "audio_wave.hpp"
#include <AL/al.h>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace viewer {
namespace {
void upload(ALuint buffer, int rate, const std::vector<std::int16_t>& samples) {
    alBufferData(buffer, AL_FORMAT_MONO16, samples.data(), static_cast<ALsizei>(samples.size()*2), rate);
    if (alGetError() != AL_NO_ERROR) throw std::runtime_error("Cannot upload impact buffer");
}
}
ImpactAudio::ImpactAudio(const std::filesystem::path& directory) {
    constexpr std::array<const char*,acousticMaterialCount> materials{"metal", "wood", "glass", "rock", "soft"};
    cache_.reserve(128);
    for (unsigned material=0; material<acousticMaterialCount; ++material) for (unsigned hard=0; hard<2; ++hard) {
        auto& base = bases_[material*2+hard];
        const auto path = directory/(std::string("impact_")+materials[material]+(hard ? "_hard.wav" : "_soft.wav"));
        try {
            AudioWave wave(path);
            if (wave.channels()!=1 || wave.seconds()>2) throw std::runtime_error("Expected mono impact WAV, at most 2 seconds");
            base.samples=wave.read(static_cast<std::size_t>(wave.frames()));
            base.rate=wave.sampleRate();
            alGenBuffers(1,&base.buffer);
            upload(base.buffer,base.rate,base.samples);
        } catch (const std::exception& e) {
            if (base.buffer) alDeleteBuffers(1,&base.buffer);
            base={};
            std::cerr << "Impact audio skipped: " << path << ": " << e.what() << '\n';
        }
    }
}
ImpactAudio::~ImpactAudio() {
    for (const auto& cached : cache_) alDeleteBuffers(1,&cached.buffer);
    for (const auto& base : bases_) if (base.buffer) alDeleteBuffers(1,&base.buffer);
}
std::uint32_t ImpactAudio::buffer(const ImpactSound& sound, const std::vector<std::uint32_t>& protectedBuffers) {
    const auto key=impactKey(sound);
    ++clock_;
    for (auto& cached : cache_) if (cached.key==key) {
        cached.used=clock_; ++stats_.cacheHits; return cached.buffer;
    }
    const auto& base=bases_[key[0]*2+key[1]];
    if (!base.buffer) return 0;
    auto victim=cache_.end();
    if (cache_.size()==128) {
        for (auto it=cache_.begin(); it!=cache_.end(); ++it) {
            if (std::find(protectedBuffers.begin(),protectedBuffers.end(),it->buffer)!=protectedBuffers.end()) continue;
            if (victim==cache_.end() || it->used<victim->used) victim=it;
        }
    }
    if (!remaining_ || (cache_.size()==128 && victim==cache_.end())) {
        ++stats_.fallbacks; return base.buffer;
    }
    --remaining_;
    const auto start=std::chrono::steady_clock::now();
    const auto samples=synthesizeImpact(base.samples,base.rate,quantizedImpact(sound));
    ALuint buffer=0;
    alGenBuffers(1,&buffer);
    try { upload(buffer,base.rate,samples); }
    catch (...) { if (buffer) alDeleteBuffers(1,&buffer); throw; }
    if (victim==cache_.end()) cache_.push_back({key,buffer,clock_});
    else { alDeleteBuffers(1,&victim->buffer); *victim={key,buffer,clock_}; }
    ++stats_.generated;
    stats_.cached=cache_.size();
    stats_.lastGenerationMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    stats_.maxGenerationMs=std::max(stats_.maxGenerationMs,stats_.lastGenerationMs);
    return buffer;
}
} // namespace viewer
