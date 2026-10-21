#include "zombie_navmesh_search.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace viewer::navmesh_path {
namespace {

double distance(Point a, Point b) {
    return std::hypot(a.x - b.x, a.z - b.z);
}

// Evaluate turns with extra precision: corridors can contain very short edges
// next to kilometer-long polygons. Portal vertices themselves remain doubles.
long double turn(Point a, Point b, Point c) {
    const auto bx = static_cast<long double>(b.x) - a.x;
    const auto bz = static_cast<long double>(b.z) - a.z;
    const auto cx = static_cast<long double>(c.x) - a.x;
    const auto cz = static_cast<long double>(c.z) - a.z;
    return bx * cz - bz * cx;
}

} // namespace

const char* statusName(Status status) {
    switch (status) {
    case Status::found: return "found";
    case Status::invalidInput: return "invalid_input";
    case Status::blockedEndpoint: return "blocked_endpoint";
    case Status::noPath: return "no_path";
    case Status::searchLimit: return "search_limit";
    case Status::bufferFull: return "buffer_full";
    }
    return "invalid_status";
}

struct Search::Impl {
    struct Record {
        double g = 0, priority = 0;
        std::uint32_t polygon = noPolygon, parent = noPolygon;
        std::uint32_t heapIndex = noPolygon;
    };
    struct Slot {
        std::uint32_t record = noPolygon, generation = 0;
    };

    const Mesh& mesh;
    const std::uint32_t limit;
    const double weight;
    const bool reopen, pureGreedy;
    std::vector<Record> records;
    std::vector<Slot> slots;
    std::vector<std::uint32_t> heap, corridor;
    std::vector<Point> output;
    std::uint32_t generation = 0, stored = 0, heapSize = 0;
    std::size_t outputSize = 0;
    Stats stats{};

    Impl(const Mesh& meshIn, std::uint32_t nodeLimit, double heuristicWeight, bool reopenClosed,
         bool greedy)
        : mesh(meshIn), limit(nodeLimit), weight(heuristicWeight), reopen(reopenClosed),
          pureGreedy(greedy) {
        if (limit == 0 || limit > Search::defaultNodeLimit)
            throw std::invalid_argument("navmesh node limit must be between 1 and 50000");
        const double diameter = std::hypot(mesh.maximumX() - mesh.minimumX(),
                                           mesh.maximumZ() - mesh.minimumZ());
        if (!std::isfinite(weight) || weight < 1 ||
            !std::isfinite(diameter * (weight + double(limit))))
            throw std::invalid_argument("invalid navmesh heuristic weight");
        records.resize(limit);
        // Polygon IDs are dense. Only this lookup spans the mesh; search
        // records and queue storage remain bounded by the per-request limit.
        slots.resize(mesh.polygons().size());
        heap.resize(limit);
        corridor.resize(limit);
        output.resize(std::size_t(limit) + 2);
    }

    std::size_t workspaceBytes() const {
        return sizeof(Impl) + records.capacity() * sizeof(Record) + slots.capacity() * sizeof(Slot) +
               heap.capacity() * sizeof(std::uint32_t) +
               corridor.capacity() * sizeof(std::uint32_t) + output.capacity() * sizeof(Point);
    }

    void reset() {
        if (++generation == 0) {
            for (auto& slot : slots) slot.generation = 0;
            generation = 1;
        }
        stored = 0;
        heapSize = 0;
        outputSize = 0;
        stats = {};
    }

    bool precedes(std::uint32_t a, std::uint32_t b) const {
        const auto& first = records[a];
        const auto& second = records[b];
        if (first.priority != second.priority) return first.priority < second.priority;
        if (first.g != second.g) return first.g > second.g;
        return first.polygon < second.polygon;
    }

    void swapHeap(std::uint32_t a, std::uint32_t b) {
        std::swap(heap[a], heap[b]);
        records[heap[a]].heapIndex = a;
        records[heap[b]].heapIndex = b;
    }

    void upward(std::uint32_t index) {
        while (index) {
            const auto parent = (index - 1) / 2;
            if (!precedes(heap[index], heap[parent])) break;
            swapHeap(index, parent);
            index = parent;
        }
    }

    void downward(std::uint32_t position) {
        while (position * 2 + 1 < heapSize) {
            auto child = position * 2 + 1;
            if (child + 1 < heapSize && precedes(heap[child + 1], heap[child])) ++child;
            if (!precedes(heap[child], heap[position])) break;
            swapHeap(position, child);
            position = child;
        }
    }

    void enqueue(std::uint32_t index, bool keyWorsened = false) {
        auto& record = records[index];
        if (record.heapIndex == noPolygon) {
            record.heapIndex = heapSize;
            heap[heapSize++] = index;
            stats.peakQueue = std::max(stats.peakQueue, heapSize);
            upward(record.heapIndex);
        } else if (keyWorsened) {
            // At equal priority, lower g loses the larger-g tie break. This is
            // normal for pure greedy and possible when weighted f rounds equal.
            downward(record.heapIndex);
        } else {
            upward(record.heapIndex);
        }
    }

    std::uint32_t pop() {
        const auto index = heap[0];
        records[index].heapIndex = noPolygon;
        if (--heapSize) {
            heap[0] = heap[heapSize];
            records[heap[0]].heapIndex = 0;
            downward(0);
        }
        return index;
    }

    double priority(double g, Point center, Point goalCenter) const {
        const auto h = distance(center, goalCenter);
        return pureGreedy ? h : g + weight * h;
    }

    bool append(Point point) {
        if (outputSize && output[outputSize - 1] == point) return true;
        if (outputSize == output.size()) return false;
        output[outputSize++] = point;
        return true;
    }

    bool portal(std::uint32_t from, std::uint32_t to, Point& left, Point& right) const {
        const auto& polygon = mesh.polygons()[from];
        for (unsigned edge = 0; edge < polygon.edgeCount; ++edge) {
            const auto& current = mesh.edges()[polygon.firstEdge + edge];
            if (current.neighbor != to) continue;
            // A CCW polygon lies left of its edge. Crossing that edge puts its
            // destination vertex on the traveler's left, its origin on right.
            const auto next = polygon.firstEdge + (edge + 1) % polygon.edgeCount;
            left = mesh.vertices()[mesh.edges()[next].vertex];
            right = mesh.vertices()[current.vertex];
            if (polygon.edgeCount == 3) return true;

            // Merging retains collinear boundary vertices for conforming
            // adjacency. Several consecutive edges can therefore cross to the
            // same polygon: the funnel needs their complete shared span.
            unsigned first = edge, last = edge;
            for (unsigned count = 1; count < polygon.edgeCount; ++count) {
                const auto following = (last + 1) % polygon.edgeCount;
                const auto& entry = mesh.edges()[polygon.firstEdge + following];
                if (following == first || entry.neighbor != to) break;
                const auto endpoint = polygon.firstEdge + (following + 1) % polygon.edgeCount;
                const auto candidate = mesh.vertices()[mesh.edges()[endpoint].vertex];
                // A non-collinear chain cannot be replaced by a straight
                // portal without changing the walkable boundary.
                if (turn(right, left, candidate) != 0) break;
                left = candidate;
                last = following;
            }
            for (unsigned count = 1; count < polygon.edgeCount; ++count) {
                const auto preceding = (first + polygon.edgeCount - 1) % polygon.edgeCount;
                const auto& entry = mesh.edges()[polygon.firstEdge + preceding];
                if (preceding == last || entry.neighbor != to) break;
                const auto candidate = mesh.vertices()[entry.vertex];
                if (turn(right, left, candidate) != 0) break;
                right = candidate;
                first = preceding;
            }
            return true;
        }
        return false;
    }

    bool funnel(std::uint32_t goalRecord, Point start, Point goal) {
        std::uint32_t corridorSize = 0;
        for (auto index = goalRecord; index != noPolygon; index = records[index].parent) {
            if (corridorSize == limit) return false;
            corridor[corridorSize++] = records[index].polygon;
        }
        std::reverse(corridor.begin(), corridor.begin() + corridorSize);
        stats.corridorPolygons = corridorSize;
        // Closed ancestors stay fixed in the default search, so the goal's g
        // already measures this corridor. Reopening can improve an ancestor
        // after a descendant's g was updated; then recompute the actual route.
        stats.graphCost = records[goalRecord].g;
        if (reopen) {
            stats.graphCost = 0;
            for (std::uint32_t index = 1; index < corridorSize; ++index)
                stats.graphCost += distance(mesh.centroid(corridor[index - 1]),
                                            mesh.centroid(corridor[index]));
        }
        if (!append(start)) return false;

        Point apex = start, left = start, right = start;
        std::uint32_t leftIndex = 0, rightIndex = 0;
        // Portal zero is the start. The final degenerate portal is the goal.
        for (std::uint32_t index = 1; index <= corridorSize; ++index) {
            Point nextLeft = goal, nextRight = goal;
            if (index < corridorSize &&
                !portal(corridor[index - 1], corridor[index], nextLeft, nextRight))
                return false;
            ++stats.funnelTests;

            if (turn(apex, right, nextRight) >= 0) {
                if (apex == right || turn(apex, left, nextRight) < 0) {
                    right = nextRight;
                    rightIndex = index;
                } else {
                    if (!append(left)) return false;
                    apex = left;
                    index = leftIndex;
                    left = right = apex;
                    leftIndex = rightIndex = index;
                    continue;
                }
            }
            if (turn(apex, left, nextLeft) <= 0) {
                if (apex == left || turn(apex, right, nextLeft) > 0) {
                    left = nextLeft;
                    leftIndex = index;
                } else {
                    if (!append(right)) return false;
                    apex = right;
                    index = rightIndex;
                    left = right = apex;
                    leftIndex = rightIndex = index;
                }
            }
        }
        return append(goal);
    }

    Result result(Status status) {
        stats.storedNodes = stored;
        if (status != Status::found) return {status, nullptr, 0, stats};
        return {status, output.data(), outputSize, stats};
    }

    Result find(Point start, Point goal) {
        reset();
        if (!std::isfinite(start.x) || !std::isfinite(start.z) ||
            !std::isfinite(goal.x) || !std::isfinite(goal.z))
            return result(Status::invalidInput);
        const auto firstPolygon = mesh.locate(start, &stats.locationTests);
        const auto goalPolygon = mesh.locate(goal, &stats.locationTests);
        if (firstPolygon == noPolygon || goalPolygon == noPolygon)
            return result(Status::blockedEndpoint);
        const auto goalCenter = mesh.centroid(goalPolygon);
        auto& firstSlot = slots[firstPolygon];
        firstSlot = {0, generation};
        records[0] = {0, priority(0, mesh.centroid(firstPolygon), goalCenter),
                      firstPolygon, noPolygon, noPolygon};
        stored = 1;
        enqueue(0);

        while (heapSize) {
            const auto currentIndex = pop();
            const auto current = records[currentIndex];
            ++stats.expandedNodes;
            if (current.polygon == goalPolygon) {
                if (!funnel(currentIndex, start, goal)) return result(Status::noPath);
                return result(Status::found);
            }
            const auto& polygon = mesh.polygons()[current.polygon];
            for (unsigned edge = 0; edge < polygon.edgeCount; ++edge) {
                ++stats.neighborTests;
                const auto neighbor = mesh.edges()[polygon.firstEdge + edge].neighbor;
                if (neighbor == noPolygon) continue;
                auto& slot = slots[neighbor];
                if (!reopen && slot.generation == generation &&
                    records[slot.record].heapIndex == noPolygon) continue;
                const auto nextCenter = mesh.centroid(neighbor);
                const double nextG = current.g + mesh.neighborDistance(current.polygon, edge);
                std::uint32_t nextIndex;
                bool keyWorsened = false;
                if (slot.generation != generation) {
                    if (stored == limit) return result(Status::searchLimit);
                    nextIndex = stored++;
                    slot = {nextIndex, generation};
                    records[nextIndex] = {nextG,
                        priority(nextG, nextCenter, goalCenter),
                        neighbor, currentIndex, noPolygon};
                } else {
                    nextIndex = slot.record;
                    auto& next = records[nextIndex];
                    if (nextG >= next.g) continue;
                    const auto nextPriority = priority(nextG, nextCenter, goalCenter);
                    keyWorsened = nextPriority == next.priority;
                    next.g = nextG;
                    next.priority = nextPriority;
                    next.parent = currentIndex;
                }
                // Default approximate searches expand each polygon once. The
                // optional reference mode reopens a closed record on improvement.
                // Open records repair their key without duplicate heap entries.
                enqueue(nextIndex, keyWorsened);
            }
        }
        return result(Status::noPath);
    }
};

Search::Search(const Mesh& mesh, std::uint32_t nodeLimit, double heuristicWeight, bool reopenClosed,
               bool greedy)
    : impl_(std::make_unique<Impl>(mesh, nodeLimit, heuristicWeight, reopenClosed, greedy)) {}
Search::~Search() = default;
Result Search::find(Point start, Point goal) { return impl_->find(start, goal); }
std::size_t Search::workspaceBytes() const { return impl_->workspaceBytes(); }
std::uint32_t Search::nodeLimit() const { return impl_->limit; }
double Search::heuristicWeight() const { return impl_->weight; }
bool Search::reopenClosed() const { return impl_->reopen; }
bool Search::greedy() const { return impl_->pureGreedy; }

} // namespace viewer::navmesh_path
