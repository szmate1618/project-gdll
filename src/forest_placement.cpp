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
std::uint32_t hashId(const std::string& id) {
    std::uint32_t hash=2166136261u;
    for (const unsigned char c:id) { hash^=c; hash*=16777619u; }
    return hash;
}
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
    const auto idHash=hashId(patchId);
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

std::vector<ForestTreeCandidate> oldForestTreeCandidates(const ForestPolygon& polygon,
    const ForestPolygon& oldForest, const std::string& patchId, std::uint32_t seed,
    float spacing, float minimumHeight, float maximumHeight) {
    auto result=forestTreeCandidates(polygon,patchId,seed,spacing,minimumHeight,maximumHeight);
    for (auto& tree:result) {
        tree.oldForest=forestContains(oldForest,tree.eastNorth);
        if (tree.oldForest) tree.height*=1.2f;
    }
    // Density scales with inverse spacing squared. A second lattice with
    // sqrt(5) times the spacing adds 1/5 of the original density in expectation.
    const auto extra=forestTreeCandidates(oldForest,patchId+":old-density",seed,
        spacing*std::sqrt(5.f),minimumHeight,maximumHeight);
    for (auto tree:extra) if (forestContains(polygon,tree.eastNorth)) {
        tree.height*=1.2f; tree.oldForest=true;
        result.push_back(tree);
    }
    return result;
}

std::vector<ForestBushCandidate> forestBushCandidates(glm::dvec2 tree,
    const std::string& treeId, std::uint32_t seed) {
    const auto hash=mix(hashId(treeId)^seed^0xe37c492du);
    if (unit(hash)>=.6f) return {};
    const int count=unit(hash^0x36b8e27fu)<.2f?3:1;
    std::vector<ForestBushCandidate> result;
    for (int i=0;i<count;++i) {
        const auto key=mix(hash^mix(static_cast<std::uint32_t>(i)+0x721da3b5u));
        auto position=tree;
        if (i) {
            const auto angle=unit(key^0x3a276d91u)*6.28318530718;
            const auto radius=5.0*std::sqrt(unit(key^0x745a8c23u));
            position+=glm::dvec2(std::cos(angle),std::sin(angle))*radius;
        }
        // Quarter-height cards retain the enlarged 6..14 m width.
        result.push_back({position,1.5f+2.f*unit(key^0x0b2136e9u),
            unit(key^0x967a889bu)*6.28318530718f, unit(key^0x19ae456bu)<.25f});
    }
    return result;
}

std::vector<ForestDryTreeCandidate> forestDryTreeCandidates(const ForestPolygon& polygon,
    const ForestPolygon& oldForest, const std::string& patchId, std::uint32_t seed,
    std::size_t oldTreeCount,
    const std::function<bool(const ForestDryTreeCandidate&)>& usable) {
    // Integer rounding avoids platform-dependent floating point at half counts.
    const auto count=(oldTreeCount/100)*9+((oldTreeCount%100)*9+50)/100;
    if (!count) return {};
    auto lo=oldForest.exterior.front(),hi=lo;
    for (const auto p:oldForest.exterior) {lo=glm::min(lo,p);hi=glm::max(hi,p);}
    const auto base=mix(hashId(patchId)^seed^0xb9d41e37u);
    const auto inside=[&](const ForestDryTreeCandidate& tree) {
        return forestContains(polygon,tree.eastNorth)&&forestContains(oldForest,tree.eastNorth)&&usable(tree);
    };
    std::vector<ForestDryTreeCandidate> result;
    result.reserve(count+count/10);
    for (std::size_t i=0;i<count;++i) {
        const auto key=mix(base^mix(static_cast<std::uint32_t>(i)));
        ForestDryTreeCandidate primary;
        primary.primaryIndex=i;
        primary.height=3.5f+2.5f*unit(key^0x653e93a9u);
        primary.yaw=unit(key^0x467cf03du)*6.28318530718f;
        bool placed=false;
        for (std::uint32_t attempt=0;attempt<4096&&!placed;++attempt) {
            const auto sample=mix(key^mix(attempt^0xe8d1386fu));
            primary.eastNorth=lo+(hi-lo)*glm::dvec2(unit(sample),unit(sample^0xaec3c02bu));
            placed=inside(primary);
        }
        if (!placed) throw std::runtime_error("Cannot place requested dry trees in old forest: "+patchId);
        result.push_back(primary);
        if (unit(key^0x32b6a417u)>=.1f) continue;
        ForestDryTreeCandidate companion;
        companion.primaryIndex=i; companion.companion=true;
        companion.height=3.5f+2.5f*unit(key^0x68236f21u);
        companion.yaw=unit(key^0xf18c43d9u)*6.28318530718f;
        for (std::uint32_t attempt=0;attempt<64;++attempt) {
            const auto sample=mix(key^mix(attempt^0x7cd1b835u));
            const auto angle=unit(sample)*6.28318530718;
            const auto radius=std::sqrt(1.5*1.5+(5*5-1.5*1.5)*unit(sample^0xd97a652bu));
            companion.eastNorth=primary.eastNorth+radius*glm::dvec2(std::cos(angle),std::sin(angle));
            if (inside(companion)) {result.push_back(companion);break;}
        }
    }
    return result;
}
} // namespace viewer
