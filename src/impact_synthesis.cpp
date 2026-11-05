#include "impact_synthesis.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace viewer {
ImpactKey impactKey(const ImpactSound& s) {
    if (!validImpactSound(s)) throw std::invalid_argument("Invalid impact synthesis parameters");
    return {static_cast<unsigned>(s.material), s.hardSurface ? 1u : 0u,
            static_cast<unsigned>(std::lround(s.sharpness*7)), static_cast<unsigned>(std::lround(s.hollowness*7)),
            static_cast<unsigned>(std::lround((std::log2(s.size)+2)*4))};
}
ImpactSound quantizedImpact(const ImpactSound& s) {
    const auto key = impactKey(s);
    auto result = s;
    result.sharpness = key[2]/7.f;
    result.hollowness = key[3]/7.f;
    result.size = std::exp2(key[4]/4.f-2);
    return result;
}
std::vector<std::int16_t> synthesizeImpact(const std::vector<std::int16_t>& source, int rate, const ImpactSound& s) {
    if (!validImpactSound(s) || rate < 8000 || rate > 96000 || source.empty() || source.size() > std::size_t(rate)*2)
        throw std::invalid_argument("Impact synthesis requires bounded mono PCM and valid parameters");
    constexpr double pi = 3.14159265358979323846;
    const float pitch = 1/std::sqrt(s.size);
    const auto dryFrames = static_cast<std::size_t>(std::ceil(source.size()/pitch));
    const auto frames = dryFrames + static_cast<std::size_t>(rate * (.03f+.3f*s.hollowness));
    std::vector<float> samples(frames);
    struct Resonator { float a, b, gain, y1 = 0, y2 = 0; };
    constexpr std::array<float,acousticMaterialCount> frequency{1100, 390, 1650, 650, 220};
    constexpr std::array<float,acousticMaterialCount> decay{.13f, .055f, .16f, .04f, .025f};
    std::array<Resonator,3> modes{};
    const auto material = static_cast<unsigned>(s.material);
    for (std::size_t i = 0; i < modes.size(); ++i) {
        const float f = std::min(rate*.4f, frequency[material]*pitch*(1+1.37f*i));
        const float r = std::exp(-1/(rate*decay[material]*(.5f+1.5f*s.hollowness)));
        const float angle = static_cast<float>(2*pi*f/rate);
        modes[i] = {2*r*std::cos(angle), -r*r, .22f*std::sin(angle)/float(i+1)};
    }
    const float lowPass = 1-std::exp(static_cast<float>(-2*pi*1800/rate));
    float low = 0, previous = 0, peak = .85f;
    for (std::size_t i = 0; i < frames; ++i) {
        float dry = 0;
        const double read = double(i)*pitch;
        if (read < source.size()) {
            const auto a = static_cast<std::size_t>(read);
            const auto b = std::min(a+1, source.size()-1);
            dry = (source[a]+float(read-a)*(source[b]-float(source[a])))/32768.f;
        }
        low += lowPass*(dry-low);
        float value = low + (.4f+s.sharpness)*(dry-low);
        if (i < std::size_t(rate*.012f)) value *= 1+.35f*s.sharpness;
        float ringing = 0;
        for (auto& mode : modes) {
            const float y = mode.a*mode.y1+mode.b*mode.y2+mode.gain*(dry-previous);
            mode.y2=mode.y1; mode.y1=y;
            ringing += y;
        }
        previous=dry;
        value += s.hollowness*ringing;
        const float fade = std::min(1.f, float(frames-1-i)/std::max(1.f, rate*.01f));
        samples[i]=value*fade;
        peak=std::max(peak,std::abs(samples[i]));
    }
    std::vector<std::int16_t> result(frames);
    const float headroom = .85f/peak;
    for (std::size_t i=0; i<frames; ++i) result[i]=static_cast<std::int16_t>(std::lround(samples[i]*headroom*32767));
    return result;
}
} // namespace viewer
