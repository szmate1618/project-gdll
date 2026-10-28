#include "birds.hpp"
#include "scene.hpp"

namespace multiplayer {
ServerBirds::ServerBirds(const Scene& scene) {
    if (scene.birdsEnabled) birds_=std::make_unique<viewer::BirdFlocks>(*scene.collision,
        scene.trees?&*scene.trees:nullptr,scene.model.boundsMin,scene.model.boundsMax,scene.spawnEye);
}
std::vector<unsigned> ServerBirds::step(const std::vector<viewer::BirdFlockPlayer>& players) {
    if (!birds_) return std::vector<unsigned>(players.size(),0);
    auto hits=birds_->step(players);
    for (const auto& command:birds_->commands()) history_.push_back(command);
    const auto tick=birds_->simulation().state().tick;
    while (!history_.empty() && tick-history_.front().tick>=600) history_.pop_front();
    return hits;
}
std::optional<BirdFrame> ServerBirds::frame(std::optional<std::uint64_t> since) const {
    if (!birds_ || birds_->birds().empty()) return {};
    const auto& simulation=birds_->simulation(); const auto tick=simulation.state().tick;
    if (since && *since==tick) return {};
    BirdFrame result; result.tick=tick;
    if (checksumTick_!=tick) { checksum_=simulation.checksum(); checksumTick_=tick; }
    result.checksum=checksum_;
    if (!since || *since>tick || tick-*since>600) result.hasCheckpoint=true;
    else for (const auto& command:history_) if (command.tick>*since) result.commands.push_back(command);
    if (result.commands.size()>maxBirdCommands) result.hasCheckpoint=true;
    if (result.hasCheckpoint) { result.commands.clear(); result.checkpoint=simulation.state(); }
    return result;
}
} // namespace multiplayer
