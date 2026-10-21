#include "zombie_navmesh_search.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
#include <new>
#include <queue>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
bool countAllocations = false;
std::size_t allocations = 0;
}

[[gnu::noinline]] void* operator new(std::size_t size) {
    if (countAllocations) ++allocations;
    if (auto* memory = std::malloc(size ? size : 1)) return memory;
    throw std::bad_alloc();
}
[[gnu::noinline]] void* operator new[](std::size_t size) { return ::operator new(size); }
[[gnu::noinline]] void operator delete(void* memory) noexcept { std::free(memory); }
[[gnu::noinline]] void operator delete[](void* memory) noexcept { std::free(memory); }
[[gnu::noinline]] void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
[[gnu::noinline]] void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

using namespace viewer::navmesh_path;

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct Fixture {
    std::vector<std::string> cells;
    Mesh mesh;
};

Mesh connectedMesh(std::vector<Point> vertices, std::vector<Triangle> triangles) {
    using Edge = std::pair<std::uint32_t, std::uint32_t>;
    std::map<Edge, std::pair<std::uint32_t, unsigned>> edges;
    for (std::uint32_t index = 0; index < triangles.size(); ++index) {
        for (unsigned edge = 0; edge < 3; ++edge) {
            const auto a = triangles[index].vertex[edge];
            const auto b = triangles[index].vertex[(edge + 1) % 3];
            const Edge key{std::min(a, b), std::max(a, b)};
            const auto old = edges.find(key);
            if (old == edges.end()) edges.emplace(key, std::make_pair(index, edge));
            else {
                triangles[index].neighbor[edge] = old->second.first;
                triangles[old->second.first].neighbor[old->second.second] = index;
                edges.erase(old);
            }
        }
    }
    return Mesh(std::move(vertices), std::move(triangles));
}

Mesh connectedPolygonMesh(std::vector<Point> vertices,
                          const std::vector<std::vector<std::uint32_t>>& rings) {
    using EdgeKey = std::pair<std::uint32_t, std::uint32_t>;
    std::map<EdgeKey, std::pair<std::uint32_t, std::uint32_t>> shared;
    std::vector<Polygon> polygons;
    std::vector<Edge> edges;
    for (std::uint32_t polygonId = 0; polygonId < rings.size(); ++polygonId) {
        const auto& ring = rings[polygonId];
        polygons.push_back({std::uint32_t(edges.size()), std::uint32_t(ring.size())});
        for (std::uint32_t edge = 0; edge < ring.size(); ++edge) {
            const auto a = ring[edge], b = ring[(edge + 1) % ring.size()];
            const auto edgeId = std::uint32_t(edges.size());
            edges.push_back({a, noTriangle});
            const EdgeKey key{std::min(a, b), std::max(a, b)};
            const auto old = shared.find(key);
            if (old == shared.end()) shared.emplace(key, std::make_pair(polygonId, edgeId));
            else {
                edges[edgeId].neighbor = old->second.first;
                edges[old->second.second].neighbor = polygonId;
                shared.erase(old);
            }
        }
    }
    return Mesh(std::move(vertices), std::move(polygons), std::move(edges));
}

Fixture gridMesh(std::vector<std::string> cells) {
    const auto height = std::uint32_t(cells.size());
    const auto width = std::uint32_t(cells.front().size());
    std::vector<Point> vertices;
    for (std::uint32_t z = 0; z <= height; ++z)
        for (std::uint32_t x = 0; x <= width; ++x) vertices.push_back({double(x), double(z)});
    std::vector<Triangle> triangles;
    for (std::uint32_t z = 0; z < height; ++z) {
        require(cells[z].size() == width, "ragged grid fixture");
        for (std::uint32_t x = 0; x < width; ++x) {
            if (cells[z][x] == '#') continue;
            const auto a = z * (width + 1) + x;
            const auto b = a + 1, d = a + width + 1, c = d + 1;
            triangles.push_back({{a, b, c}});
            triangles.push_back({{a, c, d}});
        }
    }
    return {std::move(cells), connectedMesh(std::move(vertices), std::move(triangles))};
}

// Independent coverage oracle: intersect the segment with the fixture's free
// unit squares, then require their closed parameter intervals to cover [0,1].
bool covered(const Fixture& fixture, Point a, Point b) {
    std::vector<std::pair<double, double>> intervals;
    const double deltas[2]{b.x - a.x, b.z - a.z};
    const double origins[2]{a.x, a.z};
    for (std::size_t z = 0; z < fixture.cells.size(); ++z) {
        for (std::size_t x = 0; x < fixture.cells[z].size(); ++x) {
            if (fixture.cells[z][x] == '#') continue;
            double low = 0, high = 1;
            const double minimum[2]{double(x), double(z)};
            bool hit = true;
            for (unsigned axis = 0; axis < 2; ++axis) {
                if (deltas[axis] == 0) {
                    if (origins[axis] < minimum[axis] || origins[axis] > minimum[axis] + 1)
                        hit = false;
                    continue;
                }
                auto first = (minimum[axis] - origins[axis]) / deltas[axis];
                auto last = (minimum[axis] + 1 - origins[axis]) / deltas[axis];
                if (first > last) std::swap(first, last);
                low = std::max(low, first);
                high = std::min(high, last);
                if (low > high) hit = false;
            }
            if (hit) intervals.emplace_back(low, high);
        }
    }
    std::sort(intervals.begin(), intervals.end());
    double end = 0;
    for (const auto& interval : intervals) {
        if (interval.first > end + 1e-12) return false;
        end = std::max(end, interval.second);
    }
    return end >= 1 - 1e-12;
}

void checkPath(const Fixture& fixture, const Result& result, Point start, Point goal) {
    require(result.status == Status::found, "path not found");
    require(result.size > 0 && result.points[0] == start && result.points[result.size - 1] == goal,
            "exact endpoints not preserved");
    for (std::size_t i = 1; i < result.size; ++i) {
        require(covered(fixture, result.points[i - 1], result.points[i]),
                "funnel crossed an obstacle or the map boundary");
        require(fixture.mesh.segmentClear(result.points[i - 1], result.points[i]),
                "mesh rejects funnel segment");
    }
}

double pathLength(const Result& result) {
    double length = 0;
    for (std::size_t i = 1; i < result.size; ++i)
        length += std::hypot(result.points[i].x - result.points[i - 1].x,
                             result.points[i].z - result.points[i - 1].z);
    return length;
}

double dijkstra(const Mesh& mesh, Point start, Point goal) {
    const auto first = mesh.locate(start), last = mesh.locate(goal);
    std::vector<double> distances(mesh.polygons().size(), std::numeric_limits<double>::infinity());
    using Entry = std::pair<double, std::uint32_t>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
    distances[first] = 0;
    open.emplace(0, first);
    while (!open.empty()) {
        const auto [cost, polygonId] = open.top();
        open.pop();
        if (cost != distances[polygonId]) continue;
        if (polygonId == last) return cost;
        const auto center = mesh.centroid(polygonId);
        const auto& polygon = mesh.polygons()[polygonId];
        for (unsigned edge = 0; edge < polygon.edgeCount; ++edge) {
            const auto neighbor = mesh.edges()[polygon.firstEdge + edge].neighbor;
            if (neighbor == noTriangle) continue;
            const auto next = mesh.centroid(neighbor);
            const auto distance = cost + std::hypot(center.x - next.x, center.z - next.z);
            if (distance < distances[neighbor]) {
                distances[neighbor] = distance;
                open.emplace(distance, neighbor);
            }
        }
    }
    return std::numeric_limits<double>::infinity();
}

struct GreedyReference {
    Status status = Status::noPath;
    std::uint32_t expandedNodes = 0, storedNodes = 1, openImprovements = 0;
    double graphCost = 0;
};

// Independent priority oracle: scan the entire open set for its best entry,
// rather than maintaining a heap. This catches repairs in either heap direction.
GreedyReference linearGreedy(const Mesh& mesh, Point start, Point goal) {
    const auto first = mesh.locate(start), last = mesh.locate(goal);
    const auto count = mesh.polygons().size();
    std::vector<double> costs(count, std::numeric_limits<double>::infinity());
    std::vector<double> heuristic(count);
    std::vector<unsigned char> state(count, 0); // unseen / open / closed
    const auto target = mesh.centroid(last);
    for (std::size_t index = 0; index < count; ++index) {
        const auto center = mesh.centroid(std::uint32_t(index));
        heuristic[index] = std::hypot(center.x - target.x, center.z - target.z);
    }
    costs[first] = 0;
    state[first] = 1;
    GreedyReference result;
    while (true) {
        auto current = noPolygon;
        for (std::uint32_t index = 0; index < count; ++index) {
            if (state[index] != 1) continue;
            if (current == noPolygon || heuristic[index] < heuristic[current] ||
                (heuristic[index] == heuristic[current] &&
                 (costs[index] > costs[current] ||
                  (costs[index] == costs[current] && index < current)))) current = index;
        }
        if (current == noPolygon) return result;
        state[current] = 2;
        ++result.expandedNodes;
        if (current == last) {
            result.status = Status::found;
            result.graphCost = costs[current];
            return result;
        }
        const auto center = mesh.centroid(current);
        const auto& polygon = mesh.polygons()[current];
        for (unsigned edge = 0; edge < polygon.edgeCount; ++edge) {
            const auto next = mesh.edges()[polygon.firstEdge + edge].neighbor;
            if (next == noPolygon || state[next] == 2) continue;
            const auto neighbor = mesh.centroid(next);
            const auto cost = costs[current] + std::hypot(center.x - neighbor.x,
                                                          center.z - neighbor.z);
            if (cost >= costs[next]) continue;
            if (state[next] == 1) ++result.openImprovements;
            else ++result.storedNodes;
            costs[next] = cost;
            state[next] = 1;
        }
    }
}

void straightAndCorners() {
    Fixture empty{{".....", ".....", ".....", "....."},
        connectedMesh({{0, 0}, {5, 0}, {5, 4}, {0, 4}}, {{{0, 1, 2}}, {{0, 2, 3}}})};
    Search openSearch(empty.mesh);
    const Point start{.13, .27}, goal{4.81, 3.76};
    const auto open = openSearch.find(start, goal);
    checkPath(empty, open, start, goal);
    require(open.size == 2, "open-space corridor was not pulled straight");
    require(openSearch.find(start, start).size == 1, "identical endpoints not collapsed");

    Fixture bend{{"..###", "..###", "..###", ".....", "....."},
        connectedMesh({{0, 0}, {2, 0}, {2, 3}, {5, 3}, {5, 5}, {0, 5}},
                      {{{0, 1, 2}}, {{0, 2, 5}}, {{2, 4, 5}}, {{2, 3, 4}}})};
    Search cornerSearch(bend.mesh);
    const Point a{.2, .2}, b{4.8, 4.2};
    const auto corner = cornerSearch.find(a, b);
    checkPath(bend, corner, a, b);
    require(corner.size >= 3, "concave corner was omitted");
    const auto expected = std::hypot(2 - a.x, 3 - a.z) + std::hypot(b.x - 2, b.z - 3);
    require(std::abs(pathLength(corner) - expected) < 1e-11,
            "L corridor did not tighten onto its inner corner");
    const auto reverse = cornerSearch.find(b, a);
    checkPath(bend, reverse, b, a);
    require(std::abs(pathLength(reverse) - expected) < 1e-11,
            "reverse portal orientation changed the L route");

    // This corridor has a single four-triangle graph route. Its three edge
    // lengths are sqrt(29)/3, 5*sqrt(2)/3, sqrt(29)/3 in either direction.
    const double graphLength = (2 * std::sqrt(29.0) + 5 * std::sqrt(2.0)) / 3;
    require(std::abs(corner.stats.graphCost - graphLength) < 1e-11 &&
            std::abs(reverse.stats.graphCost - graphLength) < 1e-11,
            "default search reported the wrong reconstructed graph cost");
    Search cornerReference(bend.mesh, Search::defaultNodeLimit, 2, true);
    const auto reference = cornerReference.find(a, b);
    checkPath(bend, reference, a, b);
    require(std::abs(reference.stats.graphCost - graphLength) < 1e-11,
            "reopening search reported the wrong reconstructed graph cost");
}

void convexPolygonCorridors() {
    Fixture empty{{".....", ".....", ".....", "....."},
        connectedPolygonMesh({{0, 0}, {5, 0}, {5, 4}, {0, 4}}, {{0, 1, 2, 3}})};
    Search single(empty.mesh, 1, 8);
    const Point start{.13, .27}, goal{4.81, 3.76};
    const auto direct = single.find(start, goal);
    checkPath(empty, direct, start, goal);
    require(direct.size == 2 && direct.stats.storedNodes == 1 &&
            direct.stats.corridorPolygons == 1 && direct.stats.graphCost == 0,
            "one convex polygon did not yield a direct route with one record");

    // The same L-shaped free space as the triangle regression, partitioned into
    // two convex quadrilaterals. The reverse search crosses the fourth edge.
    Fixture bend{{"..###", "..###", "..###", ".....", "....."},
        connectedPolygonMesh({{0, 0}, {2, 0}, {2, 3}, {5, 3}, {5, 5}, {0, 5}},
                             {{0, 1, 2, 5}, {2, 3, 4, 5}})};
    const Point a{.2, .2}, b{4.8, 4.2};
    const auto expected = std::hypot(2 - a.x, 3 - a.z) + std::hypot(b.x - 2, b.z - 3);
    const auto graphLength = dijkstra(bend.mesh, a, b);
    Search search(bend.mesh, 2, 8), reference(bend.mesh, 2, 8, true);
    Search capped(bend.mesh, 1, 8), cappedReference(bend.mesh, 1, 8, true);
    for (auto* solver : {&search, &reference}) {
        for (unsigned direction = 0; direction < 2; ++direction) {
            const auto from = direction ? b : a, to = direction ? a : b;
            const auto result = solver->find(from, to);
            checkPath(bend, result, from, to);
            require(result.size == 3 && result.points[1] == Point{2, 3} &&
                    result.stats.corridorPolygons == 2,
                    "quadrilateral corridor omitted its exact inner corner");
            require(std::abs(pathLength(result) - expected) < 1e-11 &&
                    std::abs(result.stats.graphCost - graphLength) < 1e-11,
                    "quadrilateral route has the wrong geometric or graph cost");
        }
    }

    // A central hexagon and four corner triangles exactly tile this rectangle.
    // Both endpoints are in corner triangles; their only graph route crosses
    // the hexagon's fifth and sixth edges, beyond the old triangle loop bound.
    Fixture hexagon{{"....", "...."},
        connectedPolygonMesh({{1, 0}, {3, 0}, {4, 1}, {3, 2}, {1, 2}, {0, 1},
                              {0, 0}, {4, 0}, {4, 2}, {0, 2}},
                             {{0, 1, 2, 3, 4, 5}, {6, 0, 5}, {1, 7, 2},
                              {2, 8, 3}, {5, 4, 9}})};
    Search hexSearch(hexagon.mesh, 5, 1);
    for (unsigned direction = 0; direction < 2; ++direction) {
        const Point low{.1, .1}, high{.1, 1.9};
        const auto from = direction ? high : low, to = direction ? low : high;
        const auto result = hexSearch.find(from, to);
        checkPath(hexagon, result, from, to);
        require(result.size == 2 && result.stats.corridorPolygons == 3,
                "hexagon's fifth or sixth edge was not traversed and pulled straight");
        require(std::abs(result.stats.graphCost - dijkstra(hexagon.mesh, from, to)) < 1e-11,
                "hexagon graph route differs from independent Dijkstra");
    }

    allocations = 0;
    countAllocations = true;
    for (unsigned request = 0; request < 100; ++request) {
        const auto from = request % 2 ? a : b, to = request % 2 ? b : a;
        for (auto* solver : {&search, &reference}) {
            const auto result = solver->find(from, to);
            require(result.status == Status::found && result.size == 3 &&
                    result.stats.storedNodes == 2,
                    "repeated polygon request retained stale state");
        }
        for (auto* solver : {&capped, &cappedReference}) {
            const auto result = solver->find(from, to);
            require(result.status == Status::searchLimit && result.stats.storedNodes == 1,
                    "polygon request bypassed the distinct-node budget");
            require(solver->find(from, from).status == Status::found,
                    "same-polygon request failed after a capped request");
        }
        require(hexSearch.find({.1, .1}, {.1, 1.9}).status == Status::found,
                "repeated hexagon request failed");
    }
    countAllocations = false;
    require(allocations == 0, "convex-polygon requests allocated heap memory");
}

void splitCollinearPortals() {
    // A quadrilateral (a triangle with a split side) and a pentagon share both
    // halves of x=2. Treating only the first subedge as their portal introduces
    // a needless bend at (2,1). Rotate both rings to exercise wrapped spans.
    const std::vector<Point> vertices{{0, 0}, {2, 0}, {2, 1}, {2, 2}, {4, 0}, {4, 2}};
    const std::vector<std::uint32_t> quad{0, 1, 2, 3}, pentagon{1, 4, 5, 3, 2};
    for (unsigned quadRotation = 0; quadRotation < quad.size(); ++quadRotation) {
        for (unsigned pentagonRotation = 0; pentagonRotation < pentagon.size();
             ++pentagonRotation) {
            auto left = quad, right = pentagon;
            std::rotate(left.begin(), left.begin() + quadRotation, left.end());
            std::rotate(right.begin(), right.begin() + pentagonRotation, right.end());
            const auto mesh = connectedPolygonMesh(vertices, {left, right});
            Search search(mesh, 2, 8), reference(mesh, 2, 8, true);
            allocations = 0;
            countAllocations = true;
            for (const double z : {.4, 1.6}) {
                const Point a{1.8, z}, b{3.8, z};
                // Analytically the full horizontal line is in the left
                // triangle z<=x and the right rectangle 2<=x<=4.
                for (auto* solver : {&search, &reference}) {
                    for (unsigned direction = 0; direction < 2; ++direction) {
                        const auto from = direction ? b : a, to = direction ? a : b;
                        const auto result = solver->find(from, to);
                        require(result.status == Status::found && result.size == 2 &&
                                result.points[0] == from && result.points[1] == to &&
                                result.stats.corridorPolygons == 2,
                                "split collinear portal introduced a needless bend");
                        require(mesh.segmentClear(result.points[0], result.points[1]),
                                "full shared portal crossed the walkable boundary");
                    }
                }
            }
            countAllocations = false;
            require(allocations == 0, "split-portal requests allocated heap memory");
        }
    }
}

void shortestGraphAndWeighted() {
    const auto fixture = gridMesh({
        "..........", ".####..##.", "....#.....", "##..#.###.", "....#.....",
        ".####.###.", "..........", ".####.###.", ".........."});
    Search optimal(fixture.mesh, Search::defaultNodeLimit, 1);
    Search weighted(fixture.mesh, Search::defaultNodeLimit, 2);
    Search reopening(fixture.mesh, Search::defaultNodeLimit, 2, true);
    require(!weighted.reopenClosed() && reopening.reopenClosed(), "reopen option not retained");
    bool observedReopening = false;
    std::uint32_t random = 0xa571e962U;
    for (unsigned request = 0; request < 100; ++request) {
        random = random * 1664525U + 1013904223U;
        const auto first = random % fixture.mesh.polygons().size();
        random = random * 1664525U + 1013904223U;
        const auto last = random % fixture.mesh.polygons().size();
        const auto start = fixture.mesh.centroid(first), goal = fixture.mesh.centroid(last);
        const auto reference = dijkstra(fixture.mesh, start, goal);
        const auto result = optimal.find(start, goal);
        checkPath(fixture, result, start, goal);
        require(std::abs(result.stats.graphCost - reference) < 1e-10,
                "weight-one graph search differs from Dijkstra");
        const auto faster = weighted.find(start, goal);
        checkPath(fixture, faster, start, goal);
        require(faster.stats.expandedNodes <= faster.stats.storedNodes,
                "default approximate search expanded a closed triangle twice");
        require(faster.stats.graphCost + 1e-10 >= reference,
                "reported weighted cost below graph optimum");
        const auto reopened = reopening.find(start, goal);
        checkPath(fixture, reopened, start, goal);
        require(reopened.stats.graphCost <= 2 * reference + 1e-10,
                "reopening weighted centroid graph exceeds its graph-cost bound");
        observedReopening |= reopened.stats.expandedNodes > reopened.stats.storedNodes;
    }
    require(observedReopening, "reference fixture did not exercise closed-triangle reopening");
}

void greedyQueriesAndHeapRepair() {
    // A shorter route to an already-open record leaves h unchanged but lowers
    // g, which loses its larger-g tie against another equal-h frontier entry.
    // Sifting only upward expands an extra polygon here. A linear open-set scan
    // supplies the expected ordering without reproducing the indexed heap.
    const auto fixture = gridMesh({
        ".##...##", "#...#...", "..#.....", ".....#..",
        ".#......", ".#.#....", ".##.##..", ".#......"});
    const auto start = fixture.mesh.centroid(22), goal = fixture.mesh.centroid(87);
    const auto oracle = linearGreedy(fixture.mesh, start, goal);
    require(oracle.status == Status::found && oracle.openImprovements > 0,
            "greedy fixture did not exercise an open-record parent improvement");
    Search greedy(fixture.mesh, Search::defaultNodeLimit, 2, false, true);
    Search greedyOtherWeight(fixture.mesh, Search::defaultNodeLimit, 32, false, true);
    Search reopening(fixture.mesh, Search::defaultNodeLimit, 2, true, true);
    Search weighted(fixture.mesh);
    require(greedy.greedy() && greedyOtherWeight.greedy() && reopening.greedy() &&
            !weighted.greedy(), "greedy option was not retained");
    for (auto* solver : {&greedy, &greedyOtherWeight}) {
        const auto result = solver->find(start, goal);
        checkPath(fixture, result, start, goal);
        require(result.stats.expandedNodes == oracle.expandedNodes &&
                result.stats.storedNodes == oracle.storedNodes,
                "greedy heap ordering differs from the linear-frontier oracle");
        require(std::abs(result.stats.graphCost - oracle.graphCost) < 1e-11,
                "greedy search lost its improved parent or graph cost");
        require(result.stats.expandedNodes <= result.stats.storedNodes,
                "default greedy search reopened a closed record");
    }
    checkPath(fixture, reopening.find(start, goal), start, goal);

    const auto disconnected = gridMesh({"..#..", "..#..", "..#.."});
    Search failed(disconnected.mesh, Search::defaultNodeLimit, 2, false, true);
    Search capped(fixture.mesh, 1, 2, false, true);
    allocations = 0;
    countAllocations = true;
    for (unsigned request = 0; request < 50; ++request) {
        for (auto* solver : {&greedy, &reopening}) {
            const auto result = solver->find(start, goal);
            require(result.status == Status::found && result.size >= 2 &&
                    result.points[0] == start && result.points[result.size - 1] == goal,
                    "repeated greedy route failed or retained stale endpoints");
        }
        require(failed.find({.2, .2}, {4.8, 2.8}).status == Status::noPath,
                "greedy search joined disconnected components");
        require(failed.find({2.5, 1.5}, {.2, .2}).status == Status::blockedEndpoint,
                "greedy search accepted a blocked endpoint");
        require(capped.find(start, goal).status == Status::searchLimit,
                "greedy search bypassed the distinct-record limit");
        require(capped.find(start, start).status == Status::found,
                "greedy search did not reset after a limited request");
    }
    countAllocations = false;
    require(allocations == 0, "greedy queries allocated heap memory");
}

void componentRejectionBeforeSearch() {
    const auto fixture = gridMesh({"..#..", "..#..", "..#.."});
    Search weighted(fixture.mesh), reopening(fixture.mesh, Search::defaultNodeLimit, 8, true);
    Search greedy(fixture.mesh, Search::defaultNodeLimit, 2, false, true);
    Search greedyReopening(fixture.mesh, Search::defaultNodeLimit, 2, true, true);
    Search capped(fixture.mesh, 1, 8), cappedGreedy(fixture.mesh, 1, 2, true, true);
    auto checkRejected = [](const Result& result) {
        require(result.status == Status::noPath && !result.points && result.size == 0,
                "disconnected components did not reject with an empty no-path result");
        const auto& stats = result.stats;
        require(stats.storedNodes == 0 && stats.expandedNodes == 0 && stats.peakQueue == 0 &&
                stats.corridorPolygons == 0 && stats.neighborTests == 0 && stats.funnelTests == 0 &&
                stats.graphCost == 0 && stats.locationTests > 0,
                "disconnected query entered search or retained previous-query statistics");
    };
    const Point left{.2, .1}, samePolygon{.3, .1}, right{4.8, 2.8}, blocked{2.5, 1.5};
    const auto nonfinite = std::numeric_limits<double>::quiet_NaN();
    allocations = 0;
    countAllocations = true;
    for (unsigned request = 0; request < 25; ++request) {
        for (auto* solver : {&weighted, &reopening, &greedy, &greedyReopening, &capped, &cappedGreedy}) {
            const auto prime = solver->find(left, samePolygon);
            require(prime.status == Status::found && prime.stats.storedNodes > 0 &&
                    prime.stats.expandedNodes > 0 && prime.stats.funnelTests > 0,
                    "same-polygon query failed before component rejection");
            checkRejected(solver->find(left, right));
            checkRejected(solver->find(right, left));
            require(solver->find(blocked, right).status == Status::blockedEndpoint &&
                    solver->find(left, blocked).status == Status::blockedEndpoint,
                    "component rejection overrode blocked-endpoint status");
            require(solver->find({nonfinite, 0}, blocked).status == Status::invalidInput,
                    "component rejection overrode nonfinite-input status");
            require(solver->find(left, samePolygon).status == Status::found,
                    "component rejection left stale search state");
        }
        for (auto* solver : {&capped, &cappedGreedy}) {
            const auto limited = solver->find(left, {1.8, 2.8});
            require(limited.status == Status::searchLimit && limited.stats.storedNodes == 1,
                    "connectivity precheck bypassed the same-component node budget");
            checkRejected(solver->find(left, right));
        }
    }
    countAllocations = false;
    require(allocations == 0, "component precheck or recovery queries allocated heap memory");
}

void failuresAndReset() {
    const auto disconnected = gridMesh({"..#..", "..#..", "..#.."});
    Search search(disconnected.mesh);
    require(search.find({.2, .2}, {4.8, 2.8}).status == Status::noPath,
            "disconnected components were reported reachable");
    require(search.find({2.5, 1.5}, {.2, .2}).status == Status::blockedEndpoint,
            "obstacle endpoint accepted");
    require(search.find({-1, 1}, {.2, .2}).status == Status::blockedEndpoint,
            "outside endpoint accepted");
    require(search.find({std::numeric_limits<double>::quiet_NaN(), 0}, {.2, .2}).status ==
                Status::invalidInput, "nonfinite input accepted");
    const auto recovered = search.find({.2, .2}, {1.8, 2.8});
    checkPath(disconnected, recovered, {.2, .2}, {1.8, 2.8});

    Search limited(disconnected.mesh, 1);
    const auto capped = limited.find({.2, .2}, {1.8, 2.8});
    require(capped.status == Status::searchLimit && capped.stats.storedNodes == 1,
            "distinct-triangle storage limit not enforced");
    require(limited.find({.2, .1}, {.3, .1}).status == Status::found,
            "one-record same-triangle request failed after limit");
    require(limited.workspaceBytes() > 0 && limited.nodeLimit() == 1,
            "workspace accounting missing");

    for (const auto bad : {0.0, .5, std::numeric_limits<double>::infinity(),
                           std::numeric_limits<double>::quiet_NaN()}) {
        bool rejected = false;
        try { Search invalid(disconnected.mesh, 100, bad); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "invalid heuristic weight accepted");
    }
    for (const auto bad : {0U, Search::defaultNodeLimit + 1}) {
        bool rejected = false;
        try { Search invalid(disconnected.mesh, bad); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "invalid node limit accepted");
    }
}

void largeMeshWithSmallBudget() {
    // The last square's triangle IDs exceed the largest permitted record
    // budget. IDs identify mesh geometry; they are not search-record indices.
    const auto fixture = gridMesh({std::string(25'002, '.')});
    const auto triangleCount = std::uint32_t(fixture.mesh.polygons().size());
    require(triangleCount > Search::defaultNodeLimit, "large mesh fixture is too small");
    const Point first = fixture.mesh.centroid(0), second = fixture.mesh.centroid(1);
    const Point last = fixture.mesh.centroid(triangleCount - 1);
    const Point beforeLast = fixture.mesh.centroid(triangleCount - 2);
    Search search(fixture.mesh, 4);
    auto checkLocal = [&](Point start, Point goal) {
        const auto result = search.find(start, goal);
        require(result.status == Status::found && result.size == 2 &&
                result.points[0] == start && result.points[1] == goal,
                "local route failed with mesh IDs larger than the record budget");
        require(result.stats.storedNodes <= search.nodeLimit(),
                "local route exceeded its record budget");
        require(std::abs(result.stats.graphCost -
                         std::hypot(goal.x - start.x, goal.z - start.z)) < 1e-10,
                "local route retained stale graph cost");
    };

    allocations = 0;
    countAllocations = true;
    for (unsigned request = 0; request < 16; ++request) {
        checkLocal(beforeLast, last);
        const auto capped = search.find(first, last);
        require(capped.status == Status::searchLimit && capped.stats.storedNodes == 4,
                "large mesh bypassed the per-request record limit");
        require(search.find({-1, 0}, last).status == Status::blockedEndpoint,
                "large mesh accepted an outside endpoint");
        checkLocal(second, first);
        checkLocal(last, beforeLast);
    }
    countAllocations = false;
    require(allocations == 0, "large-mesh requests allocated heap memory");
}

void allocationFreeRequests() {
    const auto fixture = gridMesh({"......", ".###..", "...#..", "......"});
    Search search(fixture.mesh);
    Search reopening(fixture.mesh, Search::defaultNodeLimit, 2, true);
    (void)search.find({.2, .2}, {5.8, 3.8});
    (void)reopening.find({.2, .2}, {5.8, 3.8});
    allocations = 0;
    countAllocations = true;
    for (unsigned request = 0; request < 100; ++request) {
        const auto result = request % 2 ? search.find({.2, .2}, {5.8, 3.8})
                                        : search.find({5.8, 3.8}, {.2, .2});
        const auto reference = reopening.find({.2, .2}, {5.8, 3.8});
        if (result.status != Status::found || reference.status != Status::found) {
            countAllocations = false;
            require(false, "allocation test route failed");
        }
    }
    countAllocations = false;
    require(allocations == 0, "find allocated heap memory");
}

} // namespace

int main() {
    try {
        straightAndCorners();
        convexPolygonCorridors();
        splitCollinearPortals();
        shortestGraphAndWeighted();
        greedyQueriesAndHeapRepair();
        componentRejectionBeforeSearch();
        failuresAndReset();
        largeMeshWithSmallBudget();
        allocationFreeRequests();
    } catch (const std::exception& error) {
        countAllocations = false;
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "navmesh search tests passed\n";
    return 0;
}
