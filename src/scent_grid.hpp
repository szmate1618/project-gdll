#pragma once

#include <glm/vec3.hpp>
#include <cstdint>
#include <map>
#include <optional>
#include <tuple>
#include <vector>

namespace viewer {
struct ScentEmitter { glm::vec3 feet; bool bleeding = false, outside = true; };

// Simulation-owned X/Z concentrations, independent of actor collision grids.
// Integer cell coordinates use floor (including negative world coordinates).
class ScentGrid {
public:
    static constexpr float cellSize = 20.f, minimumScent = 1e-6f;
    struct Cell {
        std::int64_t x = 0, z = 0;
        bool operator<(const Cell& b) const { return std::tie(x,z) < std::tie(b.x,b.z); }
        bool operator==(const Cell& b) const { return x == b.x && z == b.z; }
    };
    static std::optional<Cell> cellAt(glm::vec3 feet);
    static glm::vec3 center(Cell cell, float y = 0);
    float at(Cell cell) const;
    float at(glm::vec3 feet) const;
    // First call establishes the clock. Whole subsequent simulation seconds
    // diffuse/decay synchronously, then deposit one unit per eligible emitter.
    void update(double seconds, const std::vector<ScentEmitter>& emitters);
    std::vector<Cell> ascent(glm::vec3 feet) const; // Start through peak, strictly uphill.
    std::size_t activeCells() const { return cells_.size(); }
private:
    std::map<Cell, float> cells_;
    std::optional<double> tick_;
};
} // namespace viewer
