#pragma once

#include "zombie_navmesh.hpp"
#include <memory>

namespace viewer::navmesh_path {

enum class Status { found, invalidInput, blockedEndpoint, noPath, searchLimit, bufferFull };
const char* statusName(Status status);
struct Stats {
    std::uint32_t storedNodes = 0, expandedNodes = 0, peakQueue = 0, corridorTriangles = 0;
    std::uint64_t neighborTests = 0, funnelTests = 0, locationTests = 0;
    double graphCost = 0; // Actual centroid-graph cost in meters, not priority.
};
struct Result {
    Status status = Status::noPath;
    const Point* points = nullptr; // Borrowed until next find() on this search.
    std::size_t size = 0;
    Stats stats{};
};
// Independent synchronous requests. The mesh must outlive this object.
// Search records, generation hash, indexed heap, corridor and funnel output
// scratch are allocated once. find() must not allocate dynamically.
// Default weighted searches expand each triangle once for fast approximate
// routes. The reference option permits reopening closed triangles on improvement.
class Search {
public:
    static constexpr std::uint32_t defaultNodeLimit = 50'000;
    explicit Search(const Mesh& mesh, std::uint32_t nodeLimit = defaultNodeLimit,
                    double heuristicWeight = 2.0, bool reopenClosed = false);
    ~Search();
    Search(const Search&) = delete;
    Search& operator=(const Search&) = delete;
    Result find(Point start, Point goal);
    std::size_t workspaceBytes() const;
    std::uint32_t nodeLimit() const;
    double heuristicWeight() const;
    bool reopenClosed() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace viewer::navmesh_path
