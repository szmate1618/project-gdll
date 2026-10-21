#pragma once

#include "zombie_navmesh_search.hpp"

namespace viewer::navmesh_path {

using PathId = std::uint64_t;
using ZombieId = std::uint64_t;
inline constexpr PathId invalidPath = 0;
struct PathView {
    const Point* points = nullptr;
    std::size_t size = 0;
    const Point* begin() const { return points; }
    const Point* end() const { return size ? points + size : points; }
    const Point& operator[](std::size_t index) const { return points[index]; }
};
// Persistent paths and zombie-ID mapping. The mesh must outlive this manager.
// All metadata and exactly 10 MB of waypoints are allocated at construction.
// Requests are synchronous. Views remain valid until their path is released,
// successfully replaced, or clear() is called; other requests do not move them.
class PathManager {
public:
    static constexpr std::size_t bufferBytes = 10'000'000;
    explicit PathManager(const Mesh& mesh, std::uint32_t maxPaths = 4096,
                         std::uint32_t nodeLimit = Search::defaultNodeLimit,
                         double heuristicWeight = 2.0, bool reopenClosed = false, bool greedy = false);
    ~PathManager();
    PathManager(const PathManager&) = delete;
    PathManager& operator=(const PathManager&) = delete;
    PathId request(ZombieId zombie, Point start, Point goal);
    PathId pathFor(ZombieId zombie) const;
    PathView view(PathId id) const;
    bool release(PathId id);
    bool releaseZombie(ZombieId zombie);
    void clear();
    Status lastStatus() const;
    const Stats& lastStats() const;
    std::size_t usedBytes() const;
    std::size_t capacityBytes() const { return bufferBytes; }
    std::size_t workspaceBytes() const;
    std::size_t metadataBytes() const;
    std::uint32_t nodeLimit() const;
    double heuristicWeight() const;
    bool reopenClosed() const;
    bool greedy() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace viewer::navmesh_path
