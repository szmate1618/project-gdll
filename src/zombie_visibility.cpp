#include "zombie_visibility.hpp"
#include "frame_stage_profile.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>

namespace viewer {
namespace {
constexpr double cellSize = 20.0;
constexpr double blockCells = 8.0;
VisibilityBounds emptyBounds() {
    return {glm::vec3(std::numeric_limits<float>::max()), glm::vec3(std::numeric_limits<float>::lowest())};
}
void include(VisibilityBounds& target, const VisibilityBounds& bounds) {
    target.minimum = glm::min(target.minimum, bounds.minimum);
    target.maximum = glm::max(target.maximum, bounds.maximum);
}
void padBlockBounds(VisibilityBounds& bounds) {
    // Parent center/half-size arithmetic can round an enclosing face inward,
    // especially far from the origin. Widen only the coarse bounds; exact
    // member predicates retain their existing clip-plane tolerance.
    const double limit = std::numeric_limits<float>::max();
    for (int axis = 0; axis < 3; ++axis) {
        const double scale = std::max({1.0, std::abs(double(bounds.minimum[axis])), std::abs(double(bounds.maximum[axis]))});
        const double padding = 32 * std::numeric_limits<float>::epsilon() * scale;
        bounds.minimum[axis] = static_cast<float>(std::max(-limit, double(bounds.minimum[axis]) - padding));
        bounds.maximum[axis] = static_cast<float>(std::min(limit, double(bounds.maximum[axis]) + padding));
    }
}
bool rayIntersects(const VisibilityBounds& bounds, glm::vec3 origin, glm::vec3 direction) {
    double near = 0, far = std::numeric_limits<double>::infinity();
    for (int axis = 0; axis < 3; ++axis) {
        if (direction[axis] == 0) {
            if (origin[axis] < bounds.minimum[axis] || origin[axis] > bounds.maximum[axis]) return false;
            continue;
        }
        const double first = (double(bounds.minimum[axis]) - origin[axis]) / direction[axis];
        const double second = (double(bounds.maximum[axis]) - origin[axis]) / direction[axis];
        near = std::max(near, std::min(first, second));
        far = std::min(far, std::max(first, second));
        if (near > far) return false;
    }
    return true;
}
}

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
    blocks_.clear();
    blockGroups_.clear();
    freeBlocks_.clear();
    dirtyBlocks_.clear();
    std::vector<ZombieVisibilityResult>().swap(sortScratch_);
}

bool ZombieVisibility::contains(std::size_t instance) const {
    return instance < locations_.size() && locations_[instance].group != noGroup;
}

void ZombieVisibility::markDirty(std::size_t group) {
    if (!groups_[group].dirty) {
        dirtyGroups_.push_back(group);
        groups_[group].dirty = true;
    }
    const auto block = groups_[group].block;
    if (!blockGroups_[block].dirty) {
        dirtyBlocks_.push_back(block);
        blockGroups_[block].dirty = true;
    }
}

void ZombieVisibility::attachGroup(std::size_t index) {
    auto& group = groups_[index];
    const Cell cell{std::floor(group.cell.x / blockCells), std::floor(group.cell.z / blockCells)};
    auto found = blocks_.find(cell);
    std::size_t block;
    if (found != blocks_.end()) block = found->second;
    else {
        if (freeBlocks_.empty()) {
            block = blockGroups_.size();
            blockGroups_.push_back({});
        } else {
            block = freeBlocks_.back();
            freeBlocks_.pop_back();
        }
        // Preserve a reused block's queued dirty flag, just as for fine cells.
        blockGroups_[block].cell = cell;
        blocks_.emplace(cell, block);
    }
    auto& parent = blockGroups_[block];
    group.block = block;
    group.blockSlot = parent.groups.size();
    parent.groups.push_back(index);
}

void ZombieVisibility::detachGroup(std::size_t index) {
    auto& group = groups_[index];
    auto& block = blockGroups_[group.block];
    const auto moved = block.groups.back();
    block.groups[group.blockSlot] = moved;
    groups_[moved].blockSlot = group.blockSlot;
    block.groups.pop_back();
    if (block.groups.empty()) {
        blocks_.erase(block.cell);
        freeBlocks_.push_back(group.block);
    }
    group.block = noGroup;
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
        detachGroup(index);
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
        attachGroup(index);
    }
    auto& group = groups_[index];
    group.members.push_back(instance);
    location = {&bounds, index, group.members.size() - 1};
    markDirty(index);
}

void ZombieVisibility::refit(ZombieVisibilityStats& stats) {
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
        group.bounds = emptyBounds();
        for (const auto instance : group.members) include(group.bounds, *locations_[instance].bounds);
        stats.refittedMembers += group.members.size();
        ++stats.refittedGroups;
    }
    dirtyGroups_.clear();
    // Children are current before any parent is refitted. A parent encloses
    // actual geometry, including bounds extending outside its nominal cells.
    for (const auto index : dirtyBlocks_) {
        auto& block = blockGroups_[index];
        block.dirty = false;
        if (block.groups.empty()) {
            std::vector<std::size_t>().swap(block.groups);
            continue;
        }
        block.bounds = emptyBounds();
        for (const auto group : block.groups) include(block.bounds, groups_[group].bounds);
        padBlockBounds(block.bounds);
        ++stats.refittedBlocks;
    }
    dirtyBlocks_.clear();
}

void ZombieVisibility::orderVisible(std::vector<ZombieVisibilityResult>& visible) {
    // Sparse camera/light unions avoid scratch setup and counting 256 buckets
    // per digit. Larger unions retain source-ID order with linear radix passes.
    constexpr std::size_t comparisonSortLimit = 256;
    if (visible.size() <= comparisonSortLimit) {
        std::sort(visible.begin(), visible.end(), [](const auto& a, const auto& b) { return a.instance < b.instance; });
        return;
    }
    sortScratch_.resize(visible.size());
    auto* input = &visible;
    auto* output = &sortScratch_;
    // IDs are indices into locations_; this bound avoids unnecessary high-byte
    // passes without narrowing size_t. A population of 100,000 uses three.
    for (std::size_t remaining = locations_.size() - 1, shift = 0; remaining; remaining >>= 8, shift += 8) {
        std::array<std::size_t, 256> offsets{};
        for (const auto& result : *input) ++offsets[(result.instance >> shift) & 0xffu];
        std::size_t prefix = 0;
        for (auto& offset : offsets) {
            const auto count = offset;
            offset = prefix;
            prefix += count;
        }
        // Forward scattering preserves lower-byte order and copies each ID's
        // independent pass flags together with it.
        for (const auto& result : *input)
            (*output)[offsets[(result.instance >> shift) & 0xffu]++] = result;
        std::swap(input, output);
    }
    if (input != &visible) visible.swap(sortScratch_);
}

void ZombieVisibility::queryRay(glm::vec3 origin, glm::vec3 direction,
                                std::vector<std::size_t>& candidates) {
    candidates.clear();
    for (int axis = 0; axis < 3; ++axis)
        if (!std::isfinite(origin[axis]) || !std::isfinite(direction[axis])) return;
    if (direction == glm::vec3(0)) return;
    ZombieVisibilityStats stats;
    refit(stats);
    for (const auto& entry : blocks_) {
        const auto& block = blockGroups_[entry.second];
        if (!rayIntersects(block.bounds, origin, direction)) continue;
        for (const auto groupId : block.groups) {
            const auto& group = groups_[groupId];
            if (!rayIntersects(group.bounds, origin, direction)) continue;
            for (const auto id : group.members)
                if (rayIntersects(*locations_[id].bounds, origin, direction)) candidates.push_back(id);
        }
    }
    std::sort(candidates.begin(), candidates.end());
}

ZombieVisibilityStats ZombieVisibility::query(const Frustum& camera, const Frustum* light,
                                             bool cameraCulling, std::vector<ZombieVisibilityResult>& visible,
                                             const Frustum* flashlight, const VisibilityBounds* cameraRegion) {
    visible.clear();
    ZombieVisibilityStats stats;
    stats.groups = cells_.size();
    stats.blocks = blocks_.size();
    {
        frame_stage_profile::Timer timer(frame_stage_profile::Stage::zombieRefit);
        refit(stats);
    }
    {
        frame_stage_profile::Timer timer(frame_stage_profile::Stage::zombieTraversal);
        using Relation = Frustum::Relation;
        const auto inRegion = [&](const VisibilityBounds& bounds) {
            return !cameraRegion ||
                (glm::all(glm::lessThanEqual(bounds.minimum, cameraRegion->maximum)) &&
                 glm::all(glm::greaterThanEqual(bounds.maximum, cameraRegion->minimum)));
        };
        // Only occupied blocks are visited. A rejection skips all child groups;
        // camera distance never restricts either light's independent traversal.
        for (const auto& entry : blocks_) {
            const auto& block = blockGroups_[entry.second];
            const bool blockCameraCandidate = inRegion(block.bounds);
            if (!blockCameraCandidate) stats.cameraRegionRejectedGroups += block.groups.size();
            const auto blockCamera = !blockCameraCandidate ? Relation::Outside :
                cameraCulling ? camera.classify(block.bounds.minimum, block.bounds.maximum) : Relation::Inside;
            const auto blockShadow = light ? light->classify(block.bounds.minimum, block.bounds.maximum) : Relation::Outside;
            const auto blockFlashlight = flashlight ? flashlight->classify(block.bounds.minimum, block.bounds.maximum) : Relation::Outside;
            stats.cameraBlockTests += cameraCulling && blockCameraCandidate;
            stats.shadowBlockTests += light != nullptr;
            stats.flashlightBlockTests += flashlight != nullptr;
            if (blockCamera == Relation::Outside && blockShadow == Relation::Outside && blockFlashlight == Relation::Outside) continue;
            for (const auto index : block.groups) {
                ++stats.visitedGroups;
                const auto& group = groups_[index];
                const bool cameraCandidate = blockCameraCandidate && inRegion(group.bounds);
                if (blockCameraCandidate && !cameraCandidate) ++stats.cameraRegionRejectedGroups;
                const auto cameraRelation = !cameraCandidate ? Relation::Outside : blockCamera == Relation::Intersecting ?
                    camera.classify(group.bounds.minimum, group.bounds.maximum) : blockCamera;
                const auto shadowRelation = blockShadow == Relation::Intersecting ?
                    light->classify(group.bounds.minimum, group.bounds.maximum) : blockShadow;
                const auto flashlightRelation = blockFlashlight == Relation::Intersecting ?
                    flashlight->classify(group.bounds.minimum, group.bounds.maximum) : blockFlashlight;
                stats.cameraGroupTests += cameraCandidate && blockCamera == Relation::Intersecting;
                stats.shadowGroupTests += blockShadow == Relation::Intersecting;
                stats.flashlightGroupTests += blockFlashlight == Relation::Intersecting;
                if (cameraRelation == Relation::Outside && shadowRelation == Relation::Outside && flashlightRelation == Relation::Outside) continue;
                for (const auto instance : group.members) {
                    const auto& bounds = *locations_[instance].bounds;
                    const bool cameraMember = cameraRelation != Relation::Outside && inRegion(bounds);
                    bool inCamera = cameraMember && cameraRelation == Relation::Inside;
                    bool inShadow = shadowRelation == Relation::Inside;
                    bool inFlashlight = flashlightRelation == Relation::Inside;
                    if (cameraMember && cameraRelation == Relation::Intersecting) {
                        ++stats.cameraInstanceTests;
                        inCamera = camera.intersects(bounds.minimum, bounds.maximum);
                    }
                    if (shadowRelation == Relation::Intersecting) {
                        ++stats.shadowInstanceTests;
                        inShadow = light->intersects(bounds.minimum, bounds.maximum);
                    }
                    if (flashlightRelation == Relation::Intersecting) {
                        ++stats.flashlightInstanceTests;
                        inFlashlight = flashlight->intersects(bounds.minimum, bounds.maximum);
                    }
                    if (inCamera || inShadow || inFlashlight) visible.push_back({instance, inCamera, inShadow, inFlashlight});
                }
            }
        }
    }
    // Restore initial source-ID order for mostly sequential instance, staging
    // and bounds reads in the renderer. Spatial/swap-removal order loses locality.
    // Retain this order through batch selection; it also stabilizes draw order
    // and first-time GPU palette slot assignment.
    {
        frame_stage_profile::Timer timer(frame_stage_profile::Stage::zombieSort);
        orderVisible(visible);
    }
    return stats;
}

} // namespace viewer
