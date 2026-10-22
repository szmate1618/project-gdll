#pragma once
#include "potential_surface.hpp"
#include <array>

namespace viewer::potential_surface {

enum class GuidanceMode { none, hierarchy, pockets, combined };
struct CoarseBump {
    Point center,radii;
    double weight=0;
    Bounds box; // Original structural node bounds; goals inside suppress the bump.
    std::uint32_t level=0;
};
struct Pocket {
    std::vector<Point> outer;
    std::vector<std::vector<Point>> holes;
    std::array<Point,2> mouth;
    Point outward;
    double depth=0,width=0;
};
struct GuidanceState {
    std::uint32_t activePocket=UINT32_MAX;
    Point target;
};
struct GuidanceStats {
    std::uint64_t queries=0,nodes=0,candidates=0,bumpContributors=0,pocketChecks=0;
    std::uint64_t escapeActivations=0,escapeUpdates=0,targetSwitches=0,escapeReleases=0,blockedTargets=0;
    std::uint64_t pocketContributors=0,virtualProbes=0;
};
struct GuidanceTarget {
    Point position;
    bool activated=false,released=false,active=false,changed=false;
};

// Immutable sparse geometry, shared by agents. The baker certifies pocket
// mouths; runtime performs no topology search and never modifies collision.
// At most two bump levels are accepted; each amplitude is divided by the
// number of present levels. Goal-in-box suppression is position independent.
class Guidance {
public:
    Guidance(std::vector<CoarseBump>,std::vector<Pocket>);
    ~Guidance();
    Guidance(const Guidance&)=delete;
    Guidance& operator=(const Guidance&)=delete;
    FieldValue field(Point position,Point goal,GuidanceStats&) const;
    FieldValue pocketField(Point position,Point goal,std::uint32_t ignorePocket,GuidanceStats&) const;
    // Virtual steering probes only: these polygons never enter World::move.
    bool pocketSegmentClear(Point start,Point end,Point goal,std::uint32_t ignorePocket,GuidanceStats&) const;
    GuidanceTarget selectTarget(Point position,Point goal,const World&,GuidanceState&,
                                GuidanceStats&,QueryStats&) const;
    std::size_t bumpCount() const;
    std::size_t pocketCount() const;
    std::size_t memoryBytes() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace viewer::potential_surface
