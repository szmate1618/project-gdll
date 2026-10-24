#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace viewer::navmesh_path {

struct Point { double x = 0, z = 0; };
inline bool operator==(Point a, Point b) { return a.x == b.x && a.z == b.z; }
inline constexpr std::uint32_t noTriangle = std::numeric_limits<std::uint32_t>::max();
inline constexpr std::uint32_t noPolygon = noTriangle;
// Legacy triangle recipe. Counterclockwise in X/Z, with neighbors across each
// vertex[e] -> vertex[(e+1)%3]. Converted to the compact polygon storage below.
struct Triangle {
    std::array<std::uint32_t, 3> vertex{};
    std::array<std::uint32_t, 3> neighbor{{noTriangle, noTriangle, noTriangle}};
};
struct Polygon { std::uint32_t firstEdge = 0, edgeCount = 0; };
// A polygon's edges are a contiguous counterclockwise ring. The next edge's
// vertex closes this edge; neighbor identifies the polygon across it.
struct Edge { std::uint32_t vertex = 0, neighbor = noPolygon; };

// Shared, immutable convex walkable polygons in X east/Z south meters.
class Mesh {
public:
    Mesh() = default;
    Mesh(std::vector<Point> vertices, std::vector<Triangle> triangles);
    Mesh(std::vector<Point> vertices, std::vector<Polygon> polygons, std::vector<Edge> edges);
    static Mesh load(const std::string& filename);
    const std::vector<Point>& vertices() const { return vertices_; }
    const std::vector<Polygon>& polygons() const { return polygons_; }
    const std::vector<Edge>& edges() const { return edges_; }
    Point centroid(std::uint32_t polygon) const { return centers_[polygon]; }
    // Centroid-to-centroid graph cost across this edge; boundary edges return 0
    // and must not be traversed. Both accessors require valid polygon/edge IDs.
    double neighborDistance(std::uint32_t polygon, unsigned edge) const {
        return neighborDistances_[polygons_[polygon].firstEdge+edge];
    }
    // Portal-graph connected components are labeled once when the mesh loads.
    // Vertex-only contact does not connect polygons. component() requires a valid ID.
    std::uint32_t component(std::uint32_t polygon) const { return components_[polygon]; }
    std::uint32_t componentCount() const { return componentCount_; }
    std::size_t connectivityBytes() const;
    std::uint32_t locate(Point point, std::uint64_t* polygonTests = nullptr) const;
    // Closed polygon coverage. Queries allocate no memory. The bake adds a
    // small numerical margin beyond body clearance, so boundary turns are safe.
    bool segmentClear(Point a, Point b, std::uint64_t* polygonTests = nullptr,
                      std::uint32_t* polygonHint = nullptr) const;
    // Exact closed convex-polygon containment. Unlike locate's tolerant edge
    // handling, this is a conservative certificate for skipping coverage work.
    bool containsPoint(std::uint32_t polygon, Point point) const;
    std::size_t meshBytes() const;
    std::size_t geometryCacheBytes() const;
    std::size_t indexBytes() const;
    double minimumX() const { return minimumX_; }
    double minimumZ() const { return minimumZ_; }
    double maximumX() const { return maximumX_; }
    double maximumZ() const { return maximumZ_; }
    double radius() const { return radius_; }
    double clearance() const { return clearance_; }
    double buildingExtra() const { return buildingExtra_; }
    double numericMargin() const { return numericMargin_; }
    const std::string& sourceSha256() const { return sourceSha256_; }
private:
    struct Node { Point minimum{}, maximum{}; std::uint32_t first = 0, count = 0, left = 0, right = 0; };
    std::vector<Point> vertices_;
    std::vector<Polygon> polygons_;
    std::vector<Edge> edges_;
    std::vector<Point> centers_;
    std::vector<double> neighborDistances_;
    std::vector<std::uint32_t> components_;
    std::uint32_t componentCount_ = 0;
    std::vector<std::uint32_t> order_;
    std::vector<Node> nodes_;
    double minimumX_ = 0, minimumZ_ = 0, maximumX_ = 0, maximumZ_ = 0;
    double radius_ = .3, clearance_ = .002, buildingExtra_ = .05, numericMargin_ = .005;
    std::string sourceSha256_;
    void deriveBounds();
    void validateAndIndex();
    void buildConnectivity();
    std::uint32_t build(std::uint32_t first, std::uint32_t count);
};

} // namespace viewer::navmesh_path
