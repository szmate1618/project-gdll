#include "flock_neighbors.hpp"
#include "simulation.hpp"
#include <algorithm>

namespace viewer::boid {
namespace {
constexpr std::int32_t neighborRadius = 1500; // mm; separation acts within 1 m.
// A diameter plus one millimetre keeps a query to at most eight cells, even
// when a position lies exactly on a cell boundary.
constexpr std::int32_t cellSize = neighborRadius*2+1;
Vec3 cellOf(Vec3 position) {
    const auto axis=[](std::int32_t v) { return v/cellSize-(v%cellSize<0?1:0); };
    return {axis(position.x),axis(position.y),axis(position.z)};
}
}
void FlockNeighbors::reserve(std::size_t birdCount) {
    std::size_t buckets=1;
    while (buckets<birdCount*2) buckets*=2;
    heads_.resize(buckets);
    next_.resize(birdCount);
    cells_.resize(birdCount);
}
std::size_t FlockNeighbors::bucket(Vec3 cell) const {
    return hash(static_cast<std::uint32_t>(cell.x)*0x8da6b343u ^
                static_cast<std::uint32_t>(cell.y)*0xd8163841u ^
                static_cast<std::uint32_t>(cell.z)*0xcb1ab31fu) & (heads_.size()-1);
}
void FlockNeighbors::rebuild(const std::vector<Bird>& birds, std::size_t first,
                             std::size_t count, std::uint64_t tick) {
    first_=first;
    std::fill(heads_.begin(),heads_.end(),-1);
    for (std::size_t j=0;j<count;++j) {
        const auto local=(j+tick%count)%count;
        cells_[local]=cellOf(birds[first+local].position);
        auto& head=heads_[bucket(cells_[local])];
        next_[local]=head;
        head=static_cast<std::int32_t>(local);
    }
}
Vec3 FlockNeighbors::steering(const std::vector<Bird>& birds, std::size_t index) const {
    const auto& bird=birds[index];
    const auto low=cellOf(bird.position-Vec3{neighborRadius,neighborRadius,neighborRadius});
    const auto high=cellOf(bird.position+Vec3{neighborRadius,neighborRadius,neighborRadius});
    Vec3 separation{},alignment{};
    std::int32_t neighbors=0;
    for (int x=low.x;x<=high.x;++x) for (int y=low.y;y<=high.y;++y) for (int z=low.z;z<=high.z;++z) {
        const Vec3 adjacent{x,y,z};
        auto local=heads_[bucket(adjacent)];
        // Hash collisions also consume the budget, bounding even worst-case
        // input. Rotation on rebuild gives crowded cells changing samples.
        for (int visit=0;local>=0 && visit<8;++visit,local=next_[local]) {
            const auto other=first_+static_cast<std::size_t>(local);
            if (other==index || cells_[local]!=adjacent) continue;
            auto away=bird.position-birds[other].position;
            const auto distance=lengthSquared(away);
            if (distance>std::uint64_t(neighborRadius)*neighborRadius) continue;
            alignment+=birds[other].velocity-bird.velocity;
            ++neighbors;
            if (distance>=1000000) continue;
            if (!distance) {
                // Coincident birds need opposite escape directions, including
                // checkpoints that restore several birds at one position.
                const auto pair=hash(static_cast<std::uint32_t>(std::min(index,other))) ^
                    static_cast<std::uint32_t>(std::max(index,other));
                away=direction(birdOffset(0x73657061u,pair),1000);
                if (index<other) away=ratio(away,-1,1);
            }
            separation+=ratio(away,1800000,static_cast<std::int64_t>(distance)+100000);
        }
    }
    if (neighbors) alignment=ratio(alignment,3,neighbors*4);
    return limited(separation,14000)+alignment;
}
} // namespace viewer::boid
