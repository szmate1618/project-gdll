#pragma once

#include <glm/glm.hpp>
#include <cstddef>
#include <functional>
#include <map>
#include <unordered_map>
#include <vector>

namespace viewer {

// Simulation-thread grid in X/Z world meters. Stable IDs are never reordered.
// Queries return conservative candidates in ID order; callers retain exact 3D
// distance/eligibility checks. Negative coordinates use floor, not truncation.
class ZombieSpatialIndex {
public:
    void reset(std::size_t population);
    // Expand stable-ID storage without changing any existing bucket membership.
    void growPopulation(std::size_t population);
    void update(std::size_t id, glm::vec3 feet);
    void erase(std::size_t id);
    std::size_t population() const { return locations_.size(); }
    void query(glm::vec3 center, double radius, std::vector<std::size_t>& candidates) const;
    // Visit occupied cells nearest first by their horizontal box distance.
    // Members retain bucket order; a false visitor result stops immediately.
    // This orders cells, never the complete actor population. Eligibility and
    // exact bounds/range tests remain the caller's responsibility.
    void visitCellsNear(glm::vec3 center, double radius,
                        const std::function<bool(std::size_t)>& visitor) const;
    // Unbounded forward ray through X/Z occupied cells, expanded by the maximum
    // horizontal hit-region reach from indexed feet. Walks intersected grid
    // strips inside occupied bounds; sparse/extreme queries scan occupied cells
    // when that costs less. No camera/range restriction.
    void queryRay(glm::vec3 origin, glm::vec3 direction, double padding,
                  std::vector<std::size_t>& candidates) const;

private:
    // Double integer-valued keys cover all finite float world coordinates.
    struct Cell {
        double x = 0, z = 0;
        bool operator==(const Cell& other) const { return x == other.x && z == other.z; }
    };
    struct Hash { std::size_t operator()(const Cell& cell) const; };
    struct Location { Cell cell; std::size_t slot = 0; bool present = false; };
    std::unordered_map<Cell, std::vector<std::size_t>, Hash> cells_;
    std::vector<Location> locations_;
    // Counts of occupied cells per axis, maintained only as buckets appear or
    // disappear. Ordered endpoints bound ray walks without scanning the crowd.
    std::map<double, std::size_t> columns_, rows_;
};
} // namespace viewer
