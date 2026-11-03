#include "scent_grid.hpp"

#include <cmath>
#include <limits>

namespace viewer {
std::optional<ScentGrid::Cell> ScentGrid::cellAt(glm::vec3 p) {
    const double x = std::floor(double(p.x)/cellSize), z = std::floor(double(p.z)/cellSize);
    // Leave room for neighbor arithmetic and reject unrepresentable cells.
    const double limit = double(std::numeric_limits<std::int64_t>::max()/2);
    if (!std::isfinite(p.y) || !std::isfinite(x) || !std::isfinite(z) || std::abs(x) > limit || std::abs(z) > limit) return std::nullopt;
    return Cell{static_cast<std::int64_t>(x),static_cast<std::int64_t>(z)};
}
glm::vec3 ScentGrid::center(Cell cell, float y) {
    return {float((double(cell.x)+.5)*cellSize),y,float((double(cell.z)+.5)*cellSize)};
}
float ScentGrid::at(Cell cell) const {
    const auto found = cells_.find(cell); return found == cells_.end() ? 0 : found->second;
}
float ScentGrid::at(glm::vec3 feet) const { const auto cell = cellAt(feet); return cell ? at(*cell) : 0; }
void ScentGrid::update(double seconds, const std::vector<ScentEmitter>& emitters) {
    if (!std::isfinite(seconds) || seconds < 0) return;
    const double tick = std::floor(seconds);
    if (!tick_ || tick < *tick_) { tick_ = tick; return; }
    std::vector<Cell> sources;
    for (const auto& emitter : emitters) if (emitter.bleeding && emitter.outside)
        if (const auto cell = cellAt(emitter.feet)) sources.push_back(*cell);
    if (cells_.empty() && sources.empty()) { tick_ = tick; return; }
    while (*tick_ < tick) {
        std::map<Cell,float> next;
        for (const auto& [cell, value] : cells_) {
            next[cell] += value*.8f*.98f;
            for (int x = -1; x <= 1; ++x) for (int z = -1; z <= 1; ++z) {
                if (!x && !z) continue;
                next[{cell.x+x,cell.z+z}] += value*(x && z ? .01f : .04f)*.98f;
            }
        }
        for (const auto cell : sources) next[cell] += 1.f;
        for (auto it = next.begin(); it != next.end();) {
            if (it->second < minimumScent) it = next.erase(it); else ++it;
        }
        cells_ = std::move(next);
        ++*tick_;
    }
}
std::vector<ScentGrid::Cell> ScentGrid::ascent(glm::vec3 feet) const {
    const auto start = cellAt(feet);
    if (!start || at(*start) < minimumScent) return {};
    std::vector<Cell> path{*start};
    while (path.size() <= cells_.size()) {
        const auto cell = path.back();
        Cell best = cell; float slope = 0;
        for (int x = -1; x <= 1; ++x) for (int z = -1; z <= 1; ++z) {
            if (!x && !z) continue;
            const Cell candidate{cell.x+x,cell.z+z};
            const float rise = (at(candidate)-at(cell))/(cellSize*(x && z ? 1.41421356f : 1.f));
            if (rise > slope) { slope = rise; best = candidate; }
        }
        if (best == cell) break;
        path.push_back(best);
    }
    return path;
}
} // namespace viewer
