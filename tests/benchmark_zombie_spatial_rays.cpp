// CPU-only shooting broadphase benchmark. No assets, rendering or simulation time.
#include "zombie_spatial_index.hpp"
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
struct Case { const char* name; std::size_t population; float spacing; };
void run(const Case& test) {
    viewer::ZombieSpatialIndex index;
    index.reset(test.population);
    std::vector<glm::vec3> positions;
    const auto side = static_cast<std::size_t>(std::ceil(std::sqrt(double(test.population))));
    for (std::size_t id=0; id<test.population; ++id) {
        positions.push_back({(float(id%side)-float(side)*.5f)*test.spacing+.25f,0,
                             (float(id/side)-float(side)*.5f)*test.spacing+.25f});
        index.update(id,positions.back());
    }
    std::vector<std::size_t> candidates;
    for (int round=-1; round<5; ++round) {
        std::uint64_t checksum = 0;
        double nanoseconds = 0;
        constexpr int rays = 1024;
        for (int ray=0; ray<rays; ++ray) {
            const float angle = float(ray%128)*.049087385f;
            const glm::vec3 direction = ray%16 == 0 ? glm::vec3(0,1,0) : glm::vec3(std::cos(angle),.1f,std::sin(angle));
            const auto start = Clock::now();
            index.queryRay({.125f,1,.125f},direction,1.5,candidates);
            nanoseconds += std::chrono::duration<double,std::nano>(Clock::now()-start).count();
            // Outside the query timer: consume IDs, not just the output size.
            for (const auto id : candidates) checksum = checksum*1099511628211ull+id+1;
        }
        if (round>=0) std::cout << test.name << ",ray," << round << ',' << nanoseconds/rays << ',' << checksum << '\n';
    }
    // Full-population 20 m crossings alternate between the original and shifted
    // positions. Includes bucket allocation/removal and occupied-bound upkeep.
    for (int round=0; round<5; ++round) {
        const float shift = round%2 == 0 ? 20.f : 0.f;
        const auto start = Clock::now();
        for (std::size_t id=0; id<positions.size(); ++id) index.update(id,positions[id]+glm::vec3(shift,0,0));
        const auto elapsed = std::chrono::duration<double,std::nano>(Clock::now()-start).count();
        std::cout << test.name << ",crossing," << round << ',' << elapsed/test.population << ",0\n";
    }
}
}
int main() {
    std::cout << "case,operation,round,nanoseconds_per_call,checksum\n" << std::fixed << std::setprecision(3);
    for (const auto& test : {Case{"dense-100k",100000,4},Case{"spread-100k",100000,20},Case{"sparse-1k",1000,1000000}}) run(test);
}
