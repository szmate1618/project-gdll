#include "zombie_visibility.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>

namespace viewer {
namespace { constexpr double cellSize = 20.0; }

std::size_t ZombieVisibility::Hash::operator()(const Cell& cell) const {
    const auto x = std::hash<double>{}(cell.x), z = std::hash<double>{}(cell.z);
    return x ^ (z + 0x9e3779b9u + (x << 6) + (x >> 2));
}

void ZombieVisibility::reset(std::size_t population) {
    cells_.clear();
    locations_.assign(population, {});
    groups_.clear();
    freeGroups_.clear();
    dirtyGroups_.clear();
}

bool ZombieVisibility::contains(std::size_t instance) const {
    return instance < locations_.size() && locations_[instance].group != noGroup;
}

void ZombieVisibility::markDirty(std::size_t group) {
    if (!groups_[group].dirty) {
        dirtyGroups_.push_back(group);
        groups_[group].dirty = true;
    }
}

void ZombieVisibility::erase(std::size_t instance) {
    if (!contains(instance)) return;
    auto& location = locations_[instance];
    const auto index = location.group;
    auto& group = groups_[index];
    const auto moved = group.members.back();
    group.members[location.slot] = moved;
    locations_[moved].slot = location.slot;
    group.members.pop_back();
    location = {};
    markDirty(index);
    if (group.members.empty()) {
        cells_.erase(group.cell);
        freeGroups_.push_back(index);
    }
}

void ZombieVisibility::update(std::size_t instance, const VisibilityBounds& bounds) {
    if (instance >= locations_.size()) throw std::out_of_range("Zombie visibility ID exceeds population");
    for (int axis = 0; axis < 3; ++axis)
        if (!std::isfinite(bounds.minimum[axis]) || !std::isfinite(bounds.maximum[axis]) ||
            bounds.minimum[axis] > bounds.maximum[axis])
            throw std::invalid_argument("Zombie visibility requires finite, ordered bounds");
    const Cell cell{std::floor((double(bounds.minimum.x) + bounds.maximum.x) * 0.5 / cellSize),
                    std::floor((double(bounds.minimum.z) + bounds.maximum.z) * 0.5 / cellSize)};
    auto& location = locations_[instance];
    if (contains(instance) && groups_[location.group].cell == cell) {
        location.bounds = &bounds;
        markDirty(location.group);
        return;
    }
    erase(instance);
    auto found = cells_.find(cell);
    std::size_t index;
    if (found != cells_.end()) index = found->second;
    else {
        if (freeGroups_.empty()) {
            index = groups_.size();
            groups_.push_back({});
        } else {
            index = freeGroups_.back();
            freeGroups_.pop_back();
        }
        auto& group = groups_[index];
        group.cell = cell;
        // A reused slot may already be queued for refit after removal. Retain
        // its dirty flag, preventing duplicate entries in the pending list.
        cells_.emplace(cell, index);
    }
    auto& group = groups_[index];
    group.members.push_back(instance);
    location = {&bounds, index, group.members.size() - 1};
    markDirty(index);
}

std::size_t ZombieVisibility::refit() {
    std::size_t count = 0;
    for (const auto index : dirtyGroups_) {
        auto& group = groups_[index];
        group.dirty = false;
        if (group.members.empty()) {
            std::vector<std::size_t>().swap(group.members);
            continue;
        }
        // A crowd can gather in one cell and later disperse. Prevent historical
        // dense cells from retaining population-sized member buffers forever.
        if (group.members.capacity() > std::max(std::size_t(8), group.members.size() * 4))
            std::vector<std::size_t>(group.members).swap(group.members);
        group.bounds = {glm::vec3(std::numeric_limits<float>::max()),
                        glm::vec3(std::numeric_limits<float>::lowest())};
        for (const auto instance : group.members) {
            const auto& bounds = *locations_[instance].bounds;
            group.bounds.minimum = glm::min(group.bounds.minimum, bounds.minimum);
            group.bounds.maximum = glm::max(group.bounds.maximum, bounds.maximum);
        }
        ++count;
    }
    dirtyGroups_.clear();
    return count;
}

ZombieVisibilityStats ZombieVisibility::query(const Frustum& camera, const Frustum* light,
                                             bool cameraCulling, std::vector<ZombieVisibilityResult>& visible) {
    visible.clear();
    ZombieVisibilityStats stats;
    stats.groups = cells_.size();
    stats.refittedGroups = refit();
    using Relation = Frustum::Relation;
    // Visit only occupied groups. Storage for emptied groups is reused as actors
    // travel; historical cell locations do not add to the per-frame query cost.
    for (const auto& entry : cells_) {
        const auto& group = groups_[entry.second];
        const auto cameraRelation = cameraCulling ? camera.classify(group.bounds.minimum, group.bounds.maximum) : Relation::Inside;
        const auto shadowRelation = light ? light->classify(group.bounds.minimum, group.bounds.maximum) : Relation::Outside;
        stats.cameraGroupTests += cameraCulling;
        stats.shadowGroupTests += light != nullptr;
        if (cameraRelation == Relation::Outside && shadowRelation == Relation::Outside) continue;
        for (const auto instance : group.members) {
            const auto& bounds = *locations_[instance].bounds;
            bool inCamera = cameraRelation == Relation::Inside;
            bool inShadow = shadowRelation == Relation::Inside;
            if (cameraRelation == Relation::Intersecting) {
                ++stats.cameraInstanceTests;
                inCamera = camera.intersects(bounds.minimum, bounds.maximum);
            }
            if (shadowRelation == Relation::Intersecting) {
                ++stats.shadowInstanceTests;
                inShadow = light->intersects(bounds.minimum, bounds.maximum);
            }
            if (inCamera || inShadow) visible.push_back({instance, inCamera, inShadow});
        }
    }
    // Hash/group and swap-removal ordering must not change batch draw ordering
    // or first-time GPU palette slot assignment. Preserve stable source IDs.
    std::sort(visible.begin(), visible.end(), [](const auto& a, const auto& b) { return a.instance < b.instance; });
    return stats;
}

} // namespace viewer
