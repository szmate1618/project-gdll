#include "zombie_spatial_index.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>

namespace viewer {
namespace { constexpr double cellSize = 20.0; }

std::size_t ZombieSpatialIndex::Hash::operator()(const Cell& cell) const {
    const auto x = std::hash<double>{}(cell.x), z = std::hash<double>{}(cell.z);
    return x ^ (z + 0x9e3779b9u + (x << 6) + (x >> 2));
}

void ZombieSpatialIndex::reset(std::size_t population) {
    cells_.clear();
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
    if (bucket.empty()) cells_.erase(found);
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
    try {
        destination->second.push_back(id);
    } catch (...) {
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
void ZombieSpatialIndex::queryRay(glm::vec3 origin, glm::vec3 direction, double padding,
                                 std::vector<std::size_t>& candidates) const {
    candidates.clear();
    if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z) ||
        !std::isfinite(direction.x) || !std::isfinite(direction.y) || !std::isfinite(direction.z) ||
        std::isnan(padding) || padding < 0 || glm::dot(direction, direction) == 0) return;
    // Test occupied cells instead of marching arbitrarily far through empty
    // space. This handles vertical rays and sparse/extreme coordinates too.
    for (const auto& entry : cells_) {
        double near = 0, far = std::numeric_limits<double>::infinity();
        for (const int axis : {0, 2}) {
            const double coordinate = axis == 0 ? entry.first.x : entry.first.z;
            const double low = coordinate * cellSize - padding;
            const double high = (coordinate + 1) * cellSize + padding;
            if (direction[axis] == 0) {
                if (origin[axis] < low || origin[axis] > high) { far = -1; break; }
            } else {
                const double a = (low - origin[axis]) / direction[axis];
                const double b = (high - origin[axis]) / direction[axis];
                near = std::max(near, std::min(a, b));
                far = std::min(far, std::max(a, b));
            }
        }
        if (near <= far) candidates.insert(candidates.end(), entry.second.begin(), entry.second.end());
    }
    std::sort(candidates.begin(), candidates.end()); // Preserve original equal-hit tie order.
}
} // namespace viewer
