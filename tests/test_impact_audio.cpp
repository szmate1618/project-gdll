#include "audio.hpp"
#include "audio_wave.hpp"
#include "impact_audio.hpp"
#include <AL/al.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
double distance(const std::vector<std::int16_t>& a,const std::vector<std::int16_t>& b) {
    double value=0;
    for(std::size_t i=0;i<std::min(a.size(),b.size());++i) value+=std::abs(double(a[i])-b[i]);
    return value;
}
}
int main(int argc,char** argv) {
    try {
        require(argc==2,"Expected prepared audio directory");
        viewer::AudioOptions options; options.directory=argv[1]; options.ambience=false; options.hrtf=false;
        viewer::AudioWave wave(options.directory/"impact_metal_hard.wav");
        const auto source=wave.read(static_cast<std::size_t>(wave.frames()));
        viewer::ImpactSound s; s.material=viewer::AcousticMaterial::metal; s.hardSurface=true;
        s.sharpness=.5f; s.hollowness=.5f;
        const auto normal=viewer::synthesizeImpact(source,48000,s);
        auto changed=s; changed.sharpness=1;
        require(distance(normal,viewer::synthesizeImpact(source,48000,changed))>10000,"Sharpness did not change the waveform");
        changed=s; changed.hollowness=0;
        require(distance(normal,viewer::synthesizeImpact(source,48000,changed))>10000,"Hollowness did not change resonance");
        changed=s; changed.size=4;
        require(viewer::synthesizeImpact(source,48000,changed).size()>normal.size(),"Larger acoustic size must lower pitch and lengthen playback");
        viewer::AudioSystem audio(options);
        require(audio.enabled(),"Null backend must initialize");
        audio.setListener({}, {0,0,-1});
        audio.impact(s);
        s.gain=.2f; audio.impact(s);
        require(audio.stats().impactGenerations==1 && audio.stats().impactCacheHits==1,
                "Gain must stay outside the synthesis cache key");
        require(audio.stats().effects==2,"Overlapping impacts must use independent voices");
        auto far=s; far.position={1000,0,0}; far.hollowness=1;
        audio.impact(far);
        require(audio.stats().impactGenerations==1,"Inaudible impacts must not trigger sample generation");
        // Exercise actual buffer attachments while churning a bounded cache.
        viewer::ImpactAudio cache(options.directory);
        const auto pinned=cache.buffer(s,{});
        ALuint voice=0; alGenSources(1,&voice);
        alSourcei(voice,AL_BUFFER,static_cast<ALint>(pinned));
        for(unsigned i=0;i<180;++i) {
            cache.service();
            auto v=s; v.material=static_cast<viewer::AcousticMaterial>(i%5);
            v.hardSurface=(i/5)%2; v.sharpness=((i/10)%8)/7.f;
            v.hollowness=((i/80)%8)/7.f;
            cache.buffer(v,{pinned});
        }
        require(cache.stats().cached==128 && alIsBuffer(pinned),"Cache eviction must remain bounded and preserve attached buffers");
        alDeleteSources(1,&voice);
        cache.service();
        for(unsigned i=0;i<4;++i) { auto v=s; v.size=.25f*std::exp2(i*.25f); cache.buffer(v,{}); }
        require(cache.stats().fallbacks>=2,"A burst must fall back immediately after its generation budget");
        require(alGetError()==AL_NO_ERROR,"Cache eviction or playback produced an OpenAL error");
        std::cout << "Impact cache: " << cache.stats().cached << " buffers; max synchronous generation/upload "
                  << cache.stats().maxGenerationMs << " ms\n";
    } catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
