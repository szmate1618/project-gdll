#include "zombie_spatial_index.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>

namespace viewer {
namespace {
constexpr double cellSize = 20.0;
void removeCoordinate(std::map<double, std::size_t>& counts, double coordinate) {
    const auto found = counts.find(coordinate);
    if (--found->second == 0) counts.erase(found);
}
bool clipRayAxis(double origin, double direction, double low, double high, double& near, double& far) {
    if (direction == 0) return origin >= low && origin <= high;
    const double a = (low - origin) / direction, b = (high - origin) / direction;
    near = std::max(near, std::min(a, b));
    far = std::min(far, std::max(a, b));
    return near <= far;
}
// Include both cells at a touching boundary. Widen calculated projections for
// division/translation roundoff; the final slab test removes extra candidates.
std::pair<double, double> cellRange(double a, double b, double padding, double error) {
    return {std::ceil((std::min(a,b) - padding - error) / cellSize) - 1,
            std::floor((std::max(a,b) + padding + error) / cellSize)};
}
double projectionError(double origin, double direction, double near, double far, double padding) {
    return 16 * std::numeric_limits<double>::epsilon() *
        (std::abs(origin) + std::max(std::abs(near * direction), std::abs(far * direction)) + padding + cellSize);
}
}

std::size_t ZombieSpatialIndex::Hash::operator()(const Cell& cell) const {
    const auto x = std::hash<double>{}(cell.x), z = std::hash<double>{}(cell.z);
    return x ^ (z + 0x9e3779b9u + (x << 6) + (x >> 2));
}

void ZombieSpatialIndex::reset(std::size_t population) {
    cells_.clear();
    columns_.clear(); rows_.clear();
    locations_.assign(population, {});
}

void ZombieSpatialIndex::growPopulation(std::size_t population) {
    if (population > locations_.size()) locations_.resize(population);
}

void ZombieSpatialIndex::erase(std::size_t id) {
    if (id >= locations_.size() || !locations_[id].present) return;
    auto& location = locations_[id];
    auto found = cells_.find(location.cell);
    auto& bucket = found->second;
    const auto moved = bucket.back();
    bucket[location.slot] = moved;
    locations_[moved].slot = location.slot;
    bucket.pop_back();
    if (bucket.empty()) {
        removeCoordinate(columns_, location.cell.x);
        removeCoordinate(rows_, location.cell.z);
        cells_.erase(found);
    }
    location.present = false;
}

void ZombieSpatialIndex::update(std::size_t id, glm::vec3 feet) {
    if (id >= locations_.size()) throw std::out_of_range("Zombie spatial ID exceeds population");
    if (!std::isfinite(feet.x) || !std::isfinite(feet.y) || !std::isfinite(feet.z)) {
        erase(id);
        return;
    }
    const Cell cell{std::floor(static_cast<double>(feet.x) / cellSize),
                    std::floor(static_cast<double>(feet.z) / cellSize)};
    auto& location = locations_[id];
    if (location.present && location.cell == cell) return;
    // Allocate the destination before removing the old membership.
    auto [destination, inserted] = cells_.try_emplace(cell);
    bool addedColumn = false, addedRow = false;
    try {
        if (inserted) {
            ++columns_[cell.x]; addedColumn = true;
            ++rows_[cell.z]; addedRow = true;
        }
        destination->second.push_back(id);
    } catch (...) {
        if (addedColumn) removeCoordinate(columns_, cell.x);
        if (addedRow) removeCoordinate(rows_, cell.z);
        if (inserted) cells_.erase(destination);
        throw;
    }
    erase(id);
    location = {cell, destination->second.size() - 1, true};
}

void ZombieSpatialIndex::query(glm::vec3 center, double radius, std::vector<std::size_t>& candidates) const {
    candidates.clear();
    if (!std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z) ||
        std::isnan(radius) || radius < 0) return;
    const Cell low{std::floor((static_cast<double>(center.x) - radius) / cellSize),
                   std::floor((static_cast<double>(center.z) - radius) / cellSize)};
    const Cell high{std::floor((static_cast<double>(center.x) + radius) / cellSize),
                    std::floor((static_cast<double>(center.z) + radius) / cellSize)};
    const double width = high.x - low.x + 1, depth = high.z - low.z + 1;
    const auto append = [&](const std::vector<std::size_t>& bucket) {
        candidates.insert(candidates.end(), bucket.begin(), bucket.end());
    };
    // Huge/loud queries inspect occupied cells rather than walking an enormous
    // empty rectangle. Infinity means all candidates, without overflowing loops.
    if (std::isfinite(width * depth) && width * depth <= static_cast<double>(cells_.size())) {
        for (std::size_t x = 0; x < static_cast<std::size_t>(width); ++x)
            for (std::size_t z = 0; z < static_cast<std::size_t>(depth); ++z) {
                const auto found = cells_.find({low.x + static_cast<double>(x), low.z + static_cast<double>(z)});
                if (found != cells_.end()) append(found->second);
            }
    } else {
        for (const auto& entry : cells_)
            if (entry.first.x >= low.x && entry.first.x <= high.x &&
                entry.first.z >= low.z && entry.first.z <= high.z) append(entry.second);
    }
    std::sort(candidates.begin(), candidates.end());
}

void ZombieSpatialIndex::visitCellsNear(glm::vec3 center, double radius,
        const std::function<bool(std::size_t)>& visitor) const {
    if (!std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z) ||
        std::isnan(radius) || radius < 0 || cells_.empty()) return;
    struct Candidate {
        Cell cell;
        const std::vector<std::size_t>* members;
        double distance;
    };
    std::vector<Candidate> ordered;
    const auto append = [&](const Cell& cell, const std::vector<std::size_t>& members) {
        const double x = std::clamp(double(center.x), cell.x * cellSize, (cell.x + 1) * cellSize);
        const double z = std::clamp(double(center.z), cell.z * cellSize, (cell.z + 1) * cellSize);
        const double dx = double(center.x) - x, dz = double(center.z) - z;
        const double distance = dx * dx + dz * dz;
        if (distance <= radius * radius) ordered.push_back({cell, &members, distance});
    };
    const Cell low{std::floor((double(center.x) - radius) / cellSize),
                   std::floor((double(center.z) - radius) / cellSize)};
    const Cell high{std::floor((double(center.x) + radius) / cellSize),
                    std::floor((double(center.z) + radius) / cellSize)};
    const double width = high.x - low.x + 1, depth = high.z - low.z + 1;
    if (std::isfinite(width * depth) && width * depth <= double(cells_.size())) {
        for (std::size_t x = 0; x < static_cast<std::size_t>(width); ++x)
            for (std::size_t z = 0; z < static_cast<std::size_t>(depth); ++z) {
                const Cell cell{low.x + double(x), low.z + double(z)};
                const auto found = cells_.find(cell);
                if (found != cells_.end()) append(cell, found->second);
            }
    } else {
        ordered.reserve(cells_.size());
        for (const auto& entry : cells_) append(entry.first, entry.second);
    }
    std::sort(ordered.begin(), ordered.end(), [](const Candidate& a, const Candidate& b) {
        if (a.distance != b.distance) return a.distance < b.distance;
        return a.cell.x != b.cell.x ? a.cell.x < b.cell.x : a.cell.z < b.cell.z;
    });
    for (const auto& cell : ordered)
        for (const auto id : *cell.members)
            if (!visitor(id)) return;
}

void ZombieSpatialIndex::queryRay(glm::vec3 origin, glm::vec3 direction, double padding,
                                 std::vector<std::size_t>& candidates) const {
    candidates.clear();
    if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z) ||
        !std::isfinite(direction.x) || !std::isfinite(direction.y) || !std::isfinite(direction.z) ||
        std::isnan(padding) || padding < 0 || glm::dot(direction, direction) == 0) return;
    if (cells_.empty()) return;
    const auto intersects = [&](const Cell& cell) {
        double near = 0, far = std::numeric_limits<double>::infinity();
        return clipRayAxis(origin.x, direction.x, cell.x * cellSize - padding,
                           (cell.x + 1) * cellSize + padding, near, far) &&
               clipRayAxis(origin.z, direction.z, cell.z * cellSize - padding,
                           (cell.z + 1) * cellSize + padding, near, far);
    };
    const auto scan = [&] {
        for (const auto& entry : cells_)
            if (intersects(entry.first))
                candidates.insert(candidates.end(), entry.second.begin(), entry.second.end());
    };
    // Walk along the dominant horizontal axis. Each strip contributes only the
    // other-axis cells reached while the ray crosses its padded bounds. This
    // visits each cell once, including corner/boundary touches and vertical rays.
    const bool xMajor = std::abs(direction.x) >= std::abs(direction.z);
    const auto& majorCounts = xMajor ? columns_ : rows_;
    const auto& minorCounts = xMajor ? rows_ : columns_;
    const double o = xMajor ? origin.x : origin.z, d = xMajor ? direction.x : direction.z;
    const double otherO = xMajor ? origin.z : origin.x, otherD = xMajor ? direction.z : direction.x;
    const double majorLow = majorCounts.begin()->first, majorHigh = majorCounts.rbegin()->first;
    const double minorLow = minorCounts.begin()->first, minorHigh = minorCounts.rbegin()->first;
    // At huge coordinates adjacent integer cell keys cease to be representable.
    // Retain the occupied-cell slab path there, and for infinite padding.
    constexpr double walkLimit = 0x1p50;
    if (!std::isfinite(padding) || std::max({std::abs(majorLow),std::abs(majorHigh),
        std::abs(minorLow),std::abs(minorHigh),std::abs(o/cellSize),std::abs(otherO/cellSize)}) > walkLimit) {
        scan();
    } else {
        double near = 0, far = std::numeric_limits<double>::infinity();
        if (!clipRayAxis(o,d,majorLow*cellSize-padding,(majorHigh+1)*cellSize+padding,near,far) ||
            !clipRayAxis(otherO,otherD,minorLow*cellSize-padding,(minorHigh+1)*cellSize+padding,near,far)) return;
        // A vertical ray is a point in X/Z; avoid 0 * infinite ray parameters.
        if (d == 0) near = far = 0;
        auto [first,last] = cellRange(std::fma(near,d,o),std::fma(far,d,o),padding,
                                     projectionError(o,d,near,far,padding));
        first = std::max(first,majorLow); last = std::min(last,majorHigh);
        if (first > last) return;
        const double strips = last-first+1;
        // Sparse worlds, very wide padding and rays along long thin populations
        // are cheaper to scan than to walk. This also bounds every integer loop.
        const double work = strips * (4 + std::ceil(4*padding/cellSize));
        if (!std::isfinite(work) || work > static_cast<double>(cells_.size())) {
            scan();
        } else {
            for (std::size_t i=0; i<static_cast<std::size_t>(strips); ++i) {
                const double major = first+static_cast<double>(i);
                double enter = near, exit = far;
                if (!clipRayAxis(o,d,major*cellSize-padding,(major+1)*cellSize+padding,enter,exit)) continue;
                auto [low,high] = cellRange(std::fma(enter,otherD,otherO),std::fma(exit,otherD,otherO),padding,
                                           projectionError(otherO,otherD,enter,exit,padding));
                low = std::max(low,minorLow); high = std::min(high,minorHigh);
                for (std::size_t j=0; j<static_cast<std::size_t>(std::max(0.0,high-low+1)); ++j) {
                    const double minor = low+static_cast<double>(j);
                    const Cell cell = xMajor ? Cell{major,minor} : Cell{minor,major};
                    const auto found = cells_.find(cell);
                    if (found != cells_.end() && intersects(cell))
                        candidates.insert(candidates.end(), found->second.begin(), found->second.end());
                }
            }
        }
    }
    std::sort(candidates.begin(), candidates.end()); // Preserve original equal-hit tie order.
}
} // namespace viewer
