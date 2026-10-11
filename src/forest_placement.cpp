#include "forest_placement.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace viewer {
namespace {
std::uint32_t mix(std::uint32_t x) {
    x ^= x>>16; x *= 0x7feb352du; x ^= x>>15; x *= 0x846ca68bu; return x^(x>>16);
}
float unit(std::uint32_t x) { return static_cast<float>(mix(x)>>8)*(1.f/16777216.f); }
}
std::vector<ForestTreeCandidate> forestTreeCandidates(const ForestPolygon& polygon,
    const std::string& patchId, std::uint32_t seed, float spacing,
    float minimumHeight, float maximumHeight) {
    validateForestPolygon(polygon);
    if (patchId.empty() || !std::isfinite(spacing)||spacing<2 ||
        !std::isfinite(minimumHeight)||!std::isfinite(maximumHeight)||
        minimumHeight<=0||maximumHeight<minimumHeight||maximumHeight>100)
        throw std::runtime_error("Invalid forest placement settings");
    glm::dvec2 lo=polygon.exterior.front(),hi=lo;
    for (const auto p:polygon.exterior) { lo=glm::min(lo,p); hi=glm::max(hi,p); }
    const auto first=glm::floor(lo/static_cast<double>(spacing));
    const auto last=glm::floor(hi/static_cast<double>(spacing));
    if ((last.x-first.x+1)*(last.y-first.y+1)>1000000)
        throw std::runtime_error("Forest candidate lattice exceeds its load-time limit");
    std::uint32_t idHash=2166136261u;
    for (const unsigned char c:patchId) { idHash ^= c; idHash *= 16777619u; }
    std::vector<ForestTreeCandidate> result;
    for (int x=static_cast<int>(first.x);x<=static_cast<int>(last.x);++x)
        for (int n=static_cast<int>(first.y);n<=static_cast<int>(last.y);++n) {
            const auto cell=mix(idHash^seed^mix(static_cast<std::uint32_t>(x))^
                mix(static_cast<std::uint32_t>(n)^0x9e3779b9u));
            const glm::dvec2 p((x+.5+.7*(unit(cell)-.5))*spacing,
                              (n+.5+.7*(unit(cell^0x68bc21ebu)-.5))*spacing);
            if (forestContains(polygon,p)) result.push_back({p,
                minimumHeight+(maximumHeight-minimumHeight)*unit(cell^0x02e5be93u),
                unit(cell^0x967a889bu)*6.28318530718f});
        }
    return result;
}
} // namespace viewer
