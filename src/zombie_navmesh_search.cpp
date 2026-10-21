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
// next to kilometer-long triangles. Portal vertices themselves remain doubles.
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
        std::uint32_t triangle = noTriangle, parent = noTriangle;
        std::uint32_t heapIndex = noTriangle;
    };
    struct Slot {
        std::uint32_t record = noTriangle, generation = 0;
    };

    const Mesh& mesh;
    const std::uint32_t limit;
    const double weight;
    const bool reopen;
    std::vector<Record> records;
    std::vector<Slot> slots;
    std::vector<std::uint32_t> heap, corridor;
    std::vector<Point> output;
    std::uint32_t generation = 0, stored = 0, heapSize = 0;
    std::size_t outputSize = 0;
    Stats stats{};

    Impl(const Mesh& meshIn, std::uint32_t nodeLimit, double heuristicWeight, bool reopenClosed)
        : mesh(meshIn), limit(nodeLimit), weight(heuristicWeight), reopen(reopenClosed) {
        if (limit == 0 || limit > Search::defaultNodeLimit)
            throw std::invalid_argument("navmesh node limit must be between 1 and 50000");
        const double diameter = std::hypot(mesh.maximumX() - mesh.minimumX(),
                                           mesh.maximumZ() - mesh.minimumZ());
        if (!std::isfinite(weight) || weight < 1 ||
            !std::isfinite(diameter * (weight + double(limit))))
            throw std::invalid_argument("invalid navmesh heuristic weight");
        records.resize(limit);
        // Triangle IDs are dense. Only this lookup spans the mesh; search
        // records and queue storage remain bounded by the per-request limit.
        slots.resize(mesh.triangles().size());
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
        return first.triangle < second.triangle;
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

    void enqueue(std::uint32_t index) {
        auto& record = records[index];
        if (record.heapIndex == noTriangle) {
            record.heapIndex = heapSize;
            heap[heapSize++] = index;
            stats.peakQueue = std::max(stats.peakQueue, heapSize);
        }
        upward(record.heapIndex);
    }

    std::uint32_t pop() {
        const auto index = heap[0];
        records[index].heapIndex = noTriangle;
        if (--heapSize) {
            heap[0] = heap[heapSize];
            records[heap[0]].heapIndex = 0;
            std::uint32_t position = 0;
            while (position * 2 + 1 < heapSize) {
                auto child = position * 2 + 1;
                if (child + 1 < heapSize && precedes(heap[child + 1], heap[child])) ++child;
                if (!precedes(heap[child], heap[position])) break;
                swapHeap(position, child);
                position = child;
            }
        }
        return index;
    }

    bool append(Point point) {
        if (outputSize && output[outputSize - 1] == point) return true;
        if (outputSize == output.size()) return false;
        output[outputSize++] = point;
        return true;
    }

    bool portal(std::uint32_t from, std::uint32_t to, Point& left, Point& right) const {
        const auto& triangle = mesh.triangles()[from];
        for (unsigned edge = 0; edge < 3; ++edge) {
            if (triangle.neighbor[edge] != to) continue;
            // A CCW triangle lies left of its edge. Crossing that edge puts its
            // destination vertex on the traveler's left, its origin on right.
            left = mesh.vertices()[triangle.vertex[(edge + 1) % 3]];
            right = mesh.vertices()[triangle.vertex[edge]];
            return true;
        }
        return false;
    }

    bool funnel(std::uint32_t goalRecord, Point start, Point goal) {
        std::uint32_t corridorSize = 0;
        for (auto index = goalRecord; index != noTriangle; index = records[index].parent) {
            if (corridorSize == limit) return false;
            corridor[corridorSize++] = records[index].triangle;
        }
        std::reverse(corridor.begin(), corridor.begin() + corridorSize);
        stats.corridorTriangles = corridorSize;
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
        const auto firstTriangle = mesh.locate(start, &stats.locationTests);
        const auto goalTriangle = mesh.locate(goal, &stats.locationTests);
        if (firstTriangle == noTriangle || goalTriangle == noTriangle)
            return result(Status::blockedEndpoint);
        const auto goalCenter = mesh.centroid(goalTriangle);
        auto& firstSlot = slots[firstTriangle];
        firstSlot = {0, generation};
        records[0] = {0, weight * distance(mesh.centroid(firstTriangle), goalCenter),
                      firstTriangle, noTriangle, noTriangle};
        stored = 1;
        enqueue(0);

        while (heapSize) {
            const auto currentIndex = pop();
            const auto current = records[currentIndex];
            ++stats.expandedNodes;
            if (current.triangle == goalTriangle) {
                if (!funnel(currentIndex, start, goal)) return result(Status::noPath);
                return result(Status::found);
            }
            const auto& neighbors = mesh.triangles()[current.triangle].neighbor;
            for (unsigned edge = 0; edge < 3; ++edge) {
                ++stats.neighborTests;
                const auto neighbor = neighbors[edge];
                if (neighbor == noTriangle) continue;
                auto& slot = slots[neighbor];
                if (!reopen && slot.generation == generation &&
                    records[slot.record].heapIndex == noTriangle) continue;
                const auto nextCenter = mesh.centroid(neighbor);
                const double nextG = current.g + mesh.neighborDistance(current.triangle, edge);
                std::uint32_t nextIndex;
                if (slot.generation != generation) {
                    if (stored == limit) return result(Status::searchLimit);
                    nextIndex = stored++;
                    slot = {nextIndex, generation};
                    records[nextIndex] = {nextG,
                        nextG + weight * distance(nextCenter, goalCenter),
                        neighbor, currentIndex, noTriangle};
                } else {
                    nextIndex = slot.record;
                    auto& next = records[nextIndex];
                    if (nextG >= next.g) continue;
                    next.g = nextG;
                    next.priority = nextG + weight * distance(nextCenter, goalCenter);
                    next.parent = currentIndex;
                }
                // Default approximate searches expand each triangle once. The
                // optional reference mode reopens a closed record on improvement.
                // Open records decrease their key without duplicate heap entries.
                enqueue(nextIndex);
            }
        }
        return result(Status::noPath);
    }
};

Search::Search(const Mesh& mesh, std::uint32_t nodeLimit, double heuristicWeight, bool reopenClosed)
    : impl_(std::make_unique<Impl>(mesh, nodeLimit, heuristicWeight, reopenClosed)) {}
Search::~Search() = default;
Result Search::find(Point start, Point goal) { return impl_->find(start, goal); }
std::size_t Search::workspaceBytes() const { return impl_->workspaceBytes(); }
std::uint32_t Search::nodeLimit() const { return impl_->limit; }
double Search::heuristicWeight() const { return impl_->weight; }
bool Search::reopenClosed() const { return impl_->reopen; }

} // namespace viewer::navmesh_path
