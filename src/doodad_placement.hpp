#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace viewer {
class CollisionWorld;
struct ForestLayer;
struct ForestPolygon;

enum class DoodadKind { Branch, Stone, BeerBottle };
inline constexpr std::size_t doodadKindCount = 3;
inline constexpr float doodadCellSize = 16;
inline constexpr std::uint32_t doodadGenerationSeed = 20261027;

struct DoodadCell {
    int x = 0, z = 0;
    bool operator<(const DoodadCell& other) const {
        return x < other.x || (x == other.x && z < other.z);
    }
    bool operator==(const DoodadCell& other) const { return x == other.x && z == other.z; }
};
DoodadCell doodadCellAt(glm::vec3 position);

// Asset-local origin in world X-east/Y-up/Z-south meters; scale is uniform.
// Identity belongs to the original candidate, never its current cell or position.
struct DoodadPlacement {
    std::uint64_t id = 0;
    DoodadKind kind = DoodadKind::Branch;
    glm::vec3 position{0};
    glm::quat rotation{1,0,0,0};
    float scale = 1;
};

// Forest polygons and collision geometry must outlive this generator. The
// bounds index is prepared once; generating a cell visits only nearby patches.
class DoodadPlacementGenerator {
public:
    DoodadPlacementGenerator(const ForestLayer& forests, const CollisionWorld& world,
                            std::uint32_t seed = doodadGenerationSeed);
    bool intersectsForest(DoodadCell cell) const;
    std::vector<DoodadPlacement> generate(DoodadCell cell) const;
private:
    const CollisionWorld* world_;
    std::uint32_t seed_;
    std::map<DoodadCell, std::vector<const ForestPolygon*>> patches_;
};
} // namespace viewer
