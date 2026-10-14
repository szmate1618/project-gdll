#include "zombie_render_updates.hpp"

#include <utility>

namespace viewer {

ZombieRenderUpdates& ZombieRenderUpdates::operator=(ZombieRenderUpdates&& other) noexcept {
    if (this == &other) return *this;
    ids_ = std::move(other.ids_);
    flags_ = std::move(other.flags_);
    // Preserve this object's publication history: replacing a layer at the same
    // address must invalidate existing consumers even if revisions coincide.
    invalidate();
    return *this;
}

void ZombieRenderUpdates::clear() const {
    for (const auto id : ids_) flags_[id] = 0;
    ids_.clear();
}

void ZombieRenderUpdates::invalidate() {
    clear();
    ++revision_;
    full_ = true;
}

void ZombieRenderUpdates::mark(std::size_t id, bool bounds) {
    if (!full_) {
        if (id >= flags_.size()) flags_.resize(id + 1, 0);
        if (flags_[id] == 0) ids_.push_back(id);
        flags_[id] |= bounds ? 3 : 1;
    }
    ++revision_;
}

ZombieRenderUpdates::View ZombieRenderUpdates::since(std::uint64_t revision) const {
    if (revision == revision_) return {revision_, false, nullptr};
    return {revision_, full_ || revision < acknowledged_ || revision > revision_, &ids_};
}

void ZombieRenderUpdates::acknowledge(std::uint64_t revision) const {
    if (revision != revision_) return;
    clear();
    full_ = false;
    acknowledged_ = revision;
}

} // namespace viewer
