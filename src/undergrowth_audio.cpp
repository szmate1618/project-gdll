#include "undergrowth_audio.hpp"
#include <array>

namespace viewer {
void playUndergrowth(AudioSystem& audio, const UndergrowthEvent& event) {
    constexpr std::array<AudioEffect,3> leafy{AudioEffect::rustle,AudioEffect::rustle2,AudioEffect::rustle3};
    constexpr std::array<AudioEffect,2> dry{AudioEffect::dryRustle,AudioEffect::dryRustle2};
    const auto kind=event.noise.kind;
    const auto effect=kind==NoiseEvent::Kind::twigSnap?AudioEffect::twigSnap:
        kind==NoiseEvent::Kind::dryRustle?dry[event.variation%dry.size()]:leafy[event.variation%leafy.size()];
    const float gain=kind==NoiseEvent::Kind::twigSnap?.75f:event.running?.6f:.36f;
    const float pitch=.96f+.02f*static_cast<float>((event.variation*7)%5);
    audio.play(effect,event.noise.position,gain*event.gainScale,pitch);
}
}
