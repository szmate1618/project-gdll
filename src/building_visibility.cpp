#include "building_visibility.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <optional>
#include <stdexcept>

namespace viewer {
namespace {
// Matches ModelVisibility's alternative identity: a named group under one
// transform. Separate instances must never borrow each other's LoD decisions.
using DetailKey = std::pair<std::string, std::array<float, 16>>;
DetailKey detailKey(const DrawInstance& draw) {
    std::array<float, 16> transform{};
    for (int col = 0; col < 4; ++col) for (int row = 0; row < 4; ++row)
        transform[static_cast<std::size_t>(col * 4 + row)] = draw.transform[col][row];
    return {draw.detailGroup, transform};
}
} // namespace

BuildingVisibility::BuildingVisibility(const Model& model, const ModelVisibility& source,
                                       const BuildingBatches& roofs, const BuildingBatches& walls)
    : batches_(roofs.meshes.size() + walls.meshes.size()), sourceCount_(model.draws.size()) {
    if (roofs.batchForDraw.size() != sourceCount_ || walls.batchForDraw.size() != sourceCount_ ||
        walls.details.size() != walls.meshes.size())
        throw std::invalid_argument("Building visibility requires matching source/batch populations");
    const VisibilityBounds empty{glm::vec3(std::numeric_limits<float>::max()),
                                 glm::vec3(std::numeric_limits<float>::lowest())};
    for (std::size_t b = 0; b < batches_.size(); ++b) {
        batches_[b].bounds = empty;
        batches_[b].roof = b < roofs.meshes.size();
        if (!batches_[b].roof) batches_[b].detail = walls.details[b - roofs.meshes.size()];
    }
    std::vector<std::size_t> owner(sourceCount_, noBuildingBatch);
    std::vector<bool> simple(sourceCount_, false);
    std::map<DetailKey, std::size_t> detailOwners;
    for (std::size_t draw = 0; draw < sourceCount_; ++draw) {
        const auto roof = roofs.batchForDraw[draw], wall = walls.batchForDraw[draw];
        if (roof != noBuildingBatch) {
            if (roof >= roofs.meshes.size() || wall != noBuildingBatch)
                throw std::invalid_argument("Invalid roof visibility ownership");
            owner[draw] = roof;
        } else if (wall != noBuildingBatch) {
            if (wall >= walls.meshes.size()) throw std::invalid_argument("Invalid wall visibility ownership");
            owner[draw] = roofs.meshes.size() + wall;
            const auto& instance = model.draws[draw];
            if (!instance.detailGroup.empty() && instance.detailDistance > 0) {
                auto inserted = detailOwners.emplace(detailKey(instance), owner[draw]);
                // A group split across materials/cells stays source-selected.
                if (inserted.first->second != owner[draw]) inserted.first->second = noBuildingBatch;
            }
        }
        simple[draw] = owner[draw] != noBuildingBatch;
    }
    for (std::size_t draw = 0; draw < sourceCount_; ++draw) {
        const auto& instance = model.draws[draw];
        if (simple[draw] || instance.detailGroup.empty() || instance.lowDetail || instance.detailDistance == 0) continue;
        const auto it = detailOwners.find(detailKey(instance));
        if (it != detailOwners.end()) owner[draw] = it->second;
    }
    for (std::size_t draw = 0; draw < sourceCount_; ++draw) {
        if (owner[draw] == noBuildingBatch) continue;
        auto& batch = batches_[owner[draw]];
        const auto& bounds = source.drawBounds(draw);
        batch.bounds.minimum = glm::min(batch.bounds.minimum, bounds.minimum);
        batch.bounds.maximum = glm::max(batch.bounds.maximum, bounds.maximum);
        ++batch.count;
        if (simple[draw]) {
            if (batch.simpleCount++ == 0) batch.firstSimple = draw;
        }
    }
    std::size_t count = 0;
    for (auto& batch : batches_) {
        if (!batch.simpleCount) throw std::invalid_argument("Building visibility requires occupied batches");
        batch.first = count;
        count += batch.count;
    }
    members_.resize(count);
    std::vector<std::size_t> offsets(batches_.size(), 0);
    std::vector<VisibilityBounds> bounds;
    bounds.reserve(sourceCount_ - count + batches_.size());
    items_.reserve(bounds.capacity());
    for (std::size_t draw = 0; draw < sourceCount_; ++draw) {
        if (owner[draw] == noBuildingBatch) {
            bounds.push_back(source.drawBounds(draw));
            items_.push_back(draw);
        } else {
            auto& batch = batches_[owner[draw]];
            members_[batch.first + offsets[owner[draw]]++] = {source.drawBounds(draw), draw, simple[draw]};
        }
    }
    for (std::size_t batch = 0; batch < batches_.size(); ++batch) {
        // Complete source geometry, including facade alternatives and moving
        // panel padding, can extend beyond the nominal 128 m cell.
        bounds.push_back(batches_[batch].bounds);
        items_.push_back(sourceCount_ + batch);
    }
    index_ = VisibilityIndex(bounds);
}

BuildingVisibilityStats BuildingVisibility::query(const BuildingVisibilityView& view,
                                                  std::vector<std::size_t>& visible,
                                                  BuildingVisibilityState& state) const {
    BuildingVisibilityStats stats;
    stats.coarse = index_.query(view.projectionView, state.coarse, view.range,
                                view.frustumCulling, &state.temporal);
    const auto frustum = view.frustumCulling ? std::optional<Frustum>(view.projectionView) : std::nullopt;
    visible.clear();
    for (const auto itemIndex : state.coarse) {
        const auto item = items_[itemIndex];
        if (item < sourceCount_) {
            visible.push_back(item);
            continue;
        }
        const auto& batch = batches_[item - sourceCount_];
        const bool grouped = batch.roof ? view.roofs :
            view.walls && !buildingDetailsNear(batch.detail.bounds, batch.detail.distance, view.eye, view.projection);
        const bool contained = batch.count == 1 ||
            ((!frustum || frustum->classify(batch.bounds.minimum, batch.bounds.maximum) == Frustum::Relation::Inside) &&
             (!view.range || view.range->contains(batch.bounds)));
        if (grouped && contained) {
            visible.push_back(batch.firstSimple);
            stats.extraSources += batch.simpleCount - 1;
            stats.skippedDetails += batch.count - batch.simpleCount;
            ++stats.selectedBatches;
            continue;
        }
        std::size_t selected = 0, first = 0;
        for (std::size_t i = batch.first; i < batch.first + batch.count; ++i) {
            const auto& member = members_[i];
            if (grouped && !member.simple) { ++stats.skippedDetails; continue; }
            if (!contained) {
                ++stats.testedMembers;
                if ((view.range && !view.range->intersects(member.bounds)) ||
                    (frustum && !frustum->intersects(member.bounds.minimum, member.bounds.maximum))) continue;
            }
            if (grouped) { if (selected++ == 0) first = member.source; }
            else visible.push_back(member.source);
        }
        if (grouped && selected) {
            visible.push_back(first);
            stats.extraSources += selected - 1;
            ++stats.selectedBatches;
        }
    }
    return stats;
}

} // namespace viewer
