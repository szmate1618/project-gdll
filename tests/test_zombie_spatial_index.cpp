#include "zombie_spatial_index.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void candidatesAndMovement() {
    viewer::ZombieSpatialIndex index;
    std::vector<glm::vec3> positions;
    for (int z = -50; z < 50; ++z)
        for (int x = -50; x < 50; ++x) positions.push_back({x * 20.0f + 0.25f, (x + z) * 0.5f, z * 20.0f - 0.25f});
    index.reset(positions.size());
    for (std::size_t id = 0; id < positions.size(); ++id) index.update(id, positions[id]);
    std::vector<std::size_t> candidates;
    for (const auto center : {glm::vec3(0), glm::vec3(-20, 5, -20), glm::vec3(19.99f, 0, 20.01f)}) {
        index.query(center, 100.0001, candidates);
        require(candidates.size() < 200, "Local query must avoid scanning a 10,000-zombie population");
        require(std::is_sorted(candidates.begin(), candidates.end()), "Candidates retain stable original ID order");
        std::vector<std::size_t> indexed, brute;
        for (const auto id : candidates)
            if (glm::length(positions[id] - center) <= 100) indexed.push_back(id);
        for (std::size_t id = 0; id < positions.size(); ++id)
            if (glm::length(positions[id] - center) <= 100) brute.push_back(id);
        require(indexed == brute, "Grid plus exact 3D filtering must match a full scan at cell boundaries");
    }
    index.update(0, {-0.1f, 0, -0.1f});
    index.update(1, {-0.2f, 0, -0.2f});
    index.update(2, {-0.3f, 0, -0.3f});
    index.erase(1); // Swap removal must repair the third ID's bucket slot.
    index.update(2, {200, 0, 200});
    index.query({-0.1f, 0, -0.1f}, 0, candidates);
    require(std::find(candidates.begin(), candidates.end(), 0) != candidates.end(), "Negative fractions belong to the negative cell");
    require(std::find(candidates.begin(), candidates.end(), 1) == candidates.end() &&
            std::find(candidates.begin(), candidates.end(), 2) == candidates.end(), "Removed and moved IDs leave no stale membership");
    index.update(0, {std::numeric_limits<float>::quiet_NaN(), 0, 0});
    index.query({}, std::numeric_limits<double>::infinity(), candidates);
    require(candidates.size() == positions.size() - 2, "Unbounded hearing queries visit occupied cells and exclude invalid positions");
    require(std::adjacent_find(candidates.begin(), candidates.end()) == candidates.end(), "Updating membership must not duplicate IDs");
    index.reset(1);
    index.update(0, {std::numeric_limits<float>::max(), 0, std::numeric_limits<float>::max()});
    index.query({std::numeric_limits<float>::max(), 0, std::numeric_limits<float>::max()}, 100, candidates);
    require(candidates == std::vector<std::size_t>{0}, "Extreme finite float coordinates must not overflow cell indexing");
}
void populationGrowthPreservesMembership() {
    viewer::ZombieSpatialIndex index;
    index.reset(3);
    index.update(0, {1, 0, 1});
    index.update(2, {2, 0, 2});
    index.growPopulation(64);
    std::vector<std::size_t> candidates;
    index.query({}, std::numeric_limits<double>::infinity(), candidates);
    require(index.population() == 64 && candidates == std::vector<std::size_t>({0, 2}),
            "Growing capacity must preserve occupied buckets and leave new IDs absent");
    index.update(63, {400, 0, 400});
    index.query({400, 0, 400}, 1, candidates);
    require(candidates == std::vector<std::size_t>{63}, "A newly grown slot must support normal insertion");
    index.erase(0); // The old bucket's swapped-in member must retain its repaired slot after growth.
    index.update(2, {400, 0, 400});
    index.query({}, 1, candidates);
    require(candidates.empty(), "Moving an old member after growth must leave no stale source bucket");
    index.erase(63);
    index.query({400, 0, 400}, 1, candidates);
    require(candidates == std::vector<std::size_t>{2}, "Growth must preserve subsequent bucket swap removal");
}
void rayCandidates() {
    viewer::ZombieSpatialIndex index;
    index.reset(8);
    for (std::size_t i = 0; i < 8; ++i)
        index.update(i, {i == 6 ? -20.01f : 0.1f, 10.0f * i, 20.0f * static_cast<float>(i) - 40.1f});
    index.update(7, {10000, 0, -10000});
    std::vector<std::size_t> candidates;
    index.queryRay({0, 0, -1}, {0, 0, -1}, 0.2, candidates);
    require(candidates == std::vector<std::size_t>({0, 1, 2}), "Forward ray must include the origin cell and exclude cells behind it");
    index.queryRay({-20, 0, 80}, {0, -1, 0}, 0.02, candidates);
    require(candidates == std::vector<std::size_t>({6}), "Vertical rays must include expanded cells across negative boundaries");
    index.queryRay({0, 0, 0}, {1, 0, -1}, 0, candidates);
    require(std::find(candidates.begin(), candidates.end(), 7) != candidates.end(), "Ray queries must not impose a shooting distance cap");
    index.erase(0);
    index.update(1, {400, 0, 400});
    index.queryRay({0, 0, -1}, {0, 0, -1}, 0.2, candidates);
    require(candidates == std::vector<std::size_t>({2}), "Ray queries must track movement and swap removal");
    index.queryRay({}, {}, 1, candidates);
    require(candidates.empty(), "Zero ray direction must produce no candidates");
    index.queryRay({}, {1, 0, 0}, -1, candidates);
    require(candidates.empty(), "Invalid padding must produce no candidates");
}
// Independent full-population slab oracle for the conservative cell candidates.
std::vector<std::size_t> rayReference(const std::vector<glm::vec3>& positions,
                                     glm::vec3 origin, glm::vec3 direction, double padding) {
    std::vector<std::size_t> result;
    for (std::size_t id=0; id<positions.size(); ++id) {
        if (!std::isfinite(positions[id].x)) continue;
        double enter = 0, leave = std::numeric_limits<double>::infinity();
        for (const int axis : {0,2}) {
            const double cell = std::floor(double(positions[id][axis])/20);
            const double lo = cell*20-padding, hi = (cell+1)*20+padding;
            if (direction[axis] == 0) {
                if (origin[axis]<lo || origin[axis]>hi) leave=-1;
            } else {
                const double a = (lo-origin[axis])/direction[axis], b = (hi-origin[axis])/direction[axis];
                enter = std::max(enter,std::min(a,b)); leave = std::min(leave,std::max(a,b));
            }
        }
        if (enter<=leave) result.push_back(id);
    }
    return result;
}
void rayWalkMatchesFullScan() {
    std::vector<glm::vec3> positions;
    for (int x=-32; x<32; ++x)
        for (int z=-32; z<32; ++z) positions.push_back({x*20.f+.25f,0,z*20.f+.25f});
    viewer::ZombieSpatialIndex index; index.reset(positions.size());
    for (std::size_t id=0; id<positions.size(); ++id) index.update(id,positions[id]);
    std::vector<std::size_t> candidates;
    const auto check = [&](glm::vec3 origin, glm::vec3 direction, double padding) {
        index.queryRay(origin,direction,padding,candidates);
        require(candidates == rayReference(positions,origin,direction,padding),
                "Ray walk must match all occupied-cell candidates, with no misses or duplicates");
    };
    for (const auto origin : {glm::vec3(0),{20,0,-20},{-640,0,640},{1e12f,0,-1e12f}})
        for (const auto direction : {glm::vec3(1,0,0),{-1,0,0},{0,0,1},{0,0,-1},
                                    {1,0,1},{-1,0,1},{1,0,-1},{-1,0,-1},{0,1,0},{1e-12f,1,1}})
            for (const double padding : {0.,.02,5.,20.,60.,1e6,std::numeric_limits<double>::infinity()})
                check(origin,direction,padding);
    std::mt19937 random(38191);
    std::uniform_real_distribution<float> coordinate(-2000,2000), component(-1,1);
    for (int ray=0; ray<1000; ++ray)
        check({coordinate(random),coordinate(random),coordinate(random)},
              {component(random),component(random),component(random)},(ray%5)*.75);
    // Exercise endpoint bookkeeping after removal, movement and population growth.
    for (std::size_t id=0; id<64; ++id) {
        positions[id] = {2000+float(id)*20,0,-2000}; index.update(id,positions[id]);
    }
    check({-1000,0,0},{1,0,-1},2);
    for (std::size_t id=0; id<64; ++id) {
        index.erase(id); positions[id].x = std::numeric_limits<float>::quiet_NaN();
    }
    check({2000,0,-2000},{-1,0,1},.02);
    index.reset(1); index.update(0,{1,0,1}); index.erase(0);
    index.queryRay({}, {1,0,1}, 1,candidates);
    require(candidates.empty(), "Removing the last cell must clear occupied bounds");
    index.growPopulation(positions.size());
    for (std::size_t id=0; id<positions.size(); ++id) index.update(id,positions[id]);
    check({0,0,0},{1,0,1},0);
    index.reset(2);
    index.update(0,{std::numeric_limits<float>::max(),0,0});
    index.update(1,{-std::numeric_limits<float>::max(),0,0});
    index.queryRay({}, {1,0,0},0,candidates);
    require(candidates == std::vector<std::size_t>{0}, "Extreme sparse rays must remain unbounded without grid-loop overflow");
}

}

int main() {
    try { candidatesAndMovement(); populationGrowthPreservesMembership(); rayCandidates(); rayWalkMatchesFullScan(); std::cout << "Zombie spatial index tests passed\n"; return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
