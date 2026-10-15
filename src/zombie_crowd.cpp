#include "zombie_crowd.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>

namespace viewer {
namespace {
constexpr double cellSize = 1.0;
constexpr double diameter = ZombieCrowd::bodyRadius * 2.0;
constexpr double skin = 0.001; // Numerical clearance, not extra arm spacing.
bool finite(glm::vec3 p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}
}

std::size_t ZombieCrowd::Hash::operator()(const Cell& cell) const {
    const auto x = std::hash<double>{}(cell.x), z = std::hash<double>{}(cell.z);
    return x ^ (z + 0x9e3779b9u + (x << 6) + (x >> 2));
}

void ZombieCrowd::reset(std::size_t population) {
    cells_.clear();
    bodies_.assign(population, {});
    beginStep();
}

void ZombieCrowd::erase(std::size_t id) {
    if (id >= bodies_.size() || !bodies_[id].present) return;
    auto& body = bodies_[id];
    auto found = cells_.find(body.cell);
    auto& bucket = found->second;
    bucket[body.slot] = bucket.back();
    bodies_[bucket.back()].slot = body.slot;
    bucket.pop_back();
    if (bucket.empty()) cells_.erase(found);
    body.present = false;
}

void ZombieCrowd::update(std::size_t id, glm::vec3 feet) {
    if (id >= bodies_.size()) throw std::out_of_range("Zombie crowd ID exceeds population");
    if (!finite(feet)) { erase(id); return; }
    const Cell cell{std::floor(double(feet.x) / cellSize), std::floor(double(feet.z) / cellSize)};
    auto& body = bodies_[id];
    if (!body.present || !(body.cell == cell)) {
        auto& destination = cells_[cell];
        destination.push_back(id);
        erase(id);
        body.cell = cell;
        body.slot = destination.size() - 1;
        body.present = true;
    }
    body.feet = feet;
}

glm::vec3 ZombieCrowd::constrain(std::size_t id, glm::vec3 desired) {
    if (id >= bodies_.size() || !bodies_[id].present || !finite(desired)) return desired;
    std::size_t checks = 0;
    return constrainFrom(id, bodies_[id].feet, desired, checks);
}

glm::vec3 ZombieCrowd::constrainFrom(std::size_t id, glm::vec3 start, glm::vec3 desired,
                                     std::size_t& checks) {
    if (id >= bodies_.size() || !bodies_[id].present || !finite(start) || !finite(desired)) return start;
    glm::dvec2 position(start.x, start.z);
    glm::dvec2 remaining = glm::dvec2(desired.x, desired.z) - position;
    if (glm::dot(remaining, remaining) < 1e-16) return desired;
    ++stats_.moves;
    // Three contacts suffice for a slide, then a corner/packed crowd stops.
    for (int slide = 0; slide < 3 && glm::dot(remaining, remaining) > 1e-16; ++slide) {
        double fraction = 1;
        glm::dvec2 normal(0);
        std::size_t contact = bodies_.size();
        bool exhausted = false;
        const auto end = position + remaining;
        const Cell low{std::floor((std::min(position.x, end.x) - diameter - skin) / cellSize),
                       std::floor((std::min(position.y, end.y) - diameter - skin) / cellSize)};
        const Cell high{std::floor((std::max(position.x, end.x) + diameter + skin) / cellSize),
                        std::floor((std::max(position.y, end.y) + diameter + skin) / cellSize)};
        const auto visit = [&](const std::vector<std::size_t>& bucket) {
            for (const auto other : bucket) {
                if (other == id) continue;
                if (checks >= neighborBudget) { exhausted = true; return; }
                ++checks;
                ++stats_.neighborChecks;
                const auto feet = bodies_[other].feet;
                // Conservative vertical sweep allows separate floors while
                // retaining contacts on slopes and across terrain height changes.
                if (feet.y >= std::max(start.y, desired.y) + bodyHeight ||
                    feet.y + bodyHeight <= std::min(start.y, desired.y)) continue;
                const auto offset = position - glm::dvec2(feet.x, feet.z);
                const double along = glm::dot(offset, remaining);
                if (along >= 0) continue; // Tangent/outward motion can escape an old overlap.
                const double a = glm::dot(remaining, remaining);
                const double c = glm::dot(offset, offset) - (diameter + skin) * (diameter + skin);
                const double discriminant = along * along - a * c;
                if (discriminant <= 0) continue;
                const double hit = std::max(0.0, (-along - std::sqrt(discriminant)) / a);
                if (hit < fraction || (hit == fraction && other < contact)) {
                    fraction = hit;
                    contact = other;
                    const auto separation = offset + remaining * hit;
                    const auto length = glm::length(separation);
                    if (length > 0) normal = separation / length;
                }
            }
        };
        const double width = high.x - low.x + 1, depth = high.z - low.z + 1;
        if (width * depth <= double(cells_.size())) {
            for (std::size_t x = 0; x < static_cast<std::size_t>(width) && !exhausted; ++x)
                for (std::size_t z = 0; z < static_cast<std::size_t>(depth) && !exhausted; ++z) {
                    const auto found = cells_.find({low.x + double(x), low.z + double(z)});
                    if (found != cells_.end()) visit(found->second);
                }
        } else {
            // Long synthetic steps/sparse fixtures avoid traversing empty space.
            // Contact ties use IDs, so unordered-map iteration cannot affect motion.
            for (const auto& entry : cells_) {
                if (entry.first.x >= low.x && entry.first.x <= high.x &&
                    entry.first.z >= low.z && entry.first.z <= high.z) visit(entry.second);
                if (exhausted) break;
            }
        }
        if (exhausted) {
            ++stats_.budgetStops;
            break; // Keep the last verified position; never ignore unchecked bodies.
        }
        position += remaining * fraction;
        if (contact == bodies_.size()) break;
        remaining *= 1 - fraction;
        remaining -= normal * std::min(0.0, glm::dot(remaining, normal));
    }
    return {float(position.x), desired.y, float(position.y)};
}

} // namespace viewer
