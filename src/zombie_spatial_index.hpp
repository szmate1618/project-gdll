#pragma once

#include <glm/glm.hpp>
#include <cstddef>
#include <unordered_map>
#include <vector>

namespace viewer {

// Simulation-thread grid in X/Z world meters. Stable IDs are never reordered.
// Queries return conservative candidates in ID order; callers retain exact 3D
// distance/eligibility checks. Negative coordinates use floor, not truncation.
class ZombieSpatialIndex {
public:
    void reset(std::size_t population);
    void update(std::size_t id, glm::vec3 feet);
    void erase(std::size_t id);
    std::size_t population() const { return locations_.size(); }
    void query(glm::vec3 center, double radius, std::vector<std::size_t>& candidates) const;

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
};
} // namespace viewer
