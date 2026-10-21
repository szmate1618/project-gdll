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
    std::vector<double> distances(mesh.triangles().size(), std::numeric_limits<double>::infinity());
    using Entry = std::pair<double, std::uint32_t>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
    distances[first] = 0;
    open.emplace(0, first);
    while (!open.empty()) {
        const auto [cost, triangle] = open.top();
        open.pop();
        if (cost != distances[triangle]) continue;
        if (triangle == last) return cost;
        const auto center = mesh.centroid(triangle);
        for (const auto neighbor : mesh.triangles()[triangle].neighbor) {
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
        const auto first = random % fixture.mesh.triangles().size();
        random = random * 1664525U + 1013904223U;
        const auto last = random % fixture.mesh.triangles().size();
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
        shortestGraphAndWeighted();
        failuresAndReset();
        allocationFreeRequests();
    } catch (const std::exception& error) {
        countAllocations = false;
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "navmesh search tests passed\n";
    return 0;
}
