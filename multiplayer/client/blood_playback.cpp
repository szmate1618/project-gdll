#include "blood_playback.hpp"
#include "zombie_layer.hpp"
#include <algorithm>

namespace multiplayer {
void BloodPlayback::apply(const std::vector<viewer::BloodSpurtStart>& starts, double snapshotSeconds,
                          double presentationSeconds, double now, viewer::ZombieLayer& zombies,
                          const viewer::CollisionWorld& world) {
    std::array<std::uint32_t,viewer::BloodSpurts::capacity> retained{};
    std::size_t count = 0;
    for (const auto& start : starts) {
        const double age = presentationSeconds-(snapshotSeconds-start.age);
        if (!start.id || age<0 || age>viewer::BloodSpurts::historySeconds) continue;
        if (count==retained.size()) break;
        retained[count++] = start.id;
        if (std::find(seen_.begin(),seen_.end(),start.id)!=seen_.end()) continue;
        zombies.startBloodSpurt(start,now-age,&world);
    }
    // History is complete and snapshot ticks never regress. Forget only IDs
    // that left that history, independent of its rotated ring storage order.
    seen_ = retained;
    zombies.updateBloodEffects(now);
}
} // namespace multiplayer
