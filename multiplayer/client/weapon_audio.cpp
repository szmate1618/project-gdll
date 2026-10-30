#include "weapon_audio.hpp"
#include <array>

namespace multiplayer {
viewer::AudioEffect firearmSound(std::size_t slot) {
    constexpr std::array<viewer::AudioEffect, 4> clips{viewer::AudioEffect::pistol, viewer::AudioEffect::smg,
        viewer::AudioEffect::shotgun, viewer::AudioEffect::sniper};
    return clips.at(slot);
}
viewer::AudioEffect reloadSound(std::size_t slot) {
    return slot == 2 ? viewer::AudioEffect::reloadShotgun :
        slot == 1 || slot == 3 ? viewer::AudioEffect::reloadRifle : viewer::AudioEffect::reload;
}
} // namespace multiplayer
