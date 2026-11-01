#pragma once

#include "fixed.hpp"
#include <cstddef>
#include <vector>

namespace viewer::boid {
struct Bird;

// Scratch index rebuilt from the flock's pre-tick positions. Buckets and visits
// are bounded; dense flocks rotate candidates rather than keeping fixed peers.
class FlockNeighbors {
public:
    void reserve(std::size_t birdCount);
    void rebuild(const std::vector<Bird>& birds, std::size_t first,
                 std::size_t count, std::uint64_t tick);
    Vec3 steering(const std::vector<Bird>& birds, std::size_t index) const;
private:
    std::size_t bucket(Vec3 cell) const;
    std::vector<std::int32_t> heads_, next_;
    std::vector<Vec3> cells_;
    std::size_t first_ = 0;
};
} // namespace viewer::boid
