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
// Counterclockwise in X/Z. neighbor[e] crosses vertex[e] -> vertex[(e+1)%3].
struct Triangle {
    std::array<std::uint32_t, 3> vertex{};
    std::array<std::uint32_t, 3> neighbor{{noTriangle, noTriangle, noTriangle}};
};

// Shared, immutable walkable-space triangulation in X east/Z south meters.
class Mesh {
public:
    Mesh() = default;
    Mesh(std::vector<Point> vertices, std::vector<Triangle> triangles);
    static Mesh load(const std::string& filename);
    const std::vector<Point>& vertices() const { return vertices_; }
    const std::vector<Triangle>& triangles() const { return triangles_; }
    Point centroid(std::uint32_t triangle) const;
    std::uint32_t locate(Point point, std::uint64_t* triangleTests = nullptr) const;
    // Closed triangle coverage. Queries allocate no memory. The bake adds a
    // small numerical margin beyond body clearance, so boundary turns are safe.
    bool segmentClear(Point a, Point b, std::uint64_t* triangleTests = nullptr) const;
    std::size_t meshBytes() const;
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
    std::vector<Triangle> triangles_;
    std::vector<std::uint32_t> order_;
    std::vector<Node> nodes_;
    double minimumX_ = 0, minimumZ_ = 0, maximumX_ = 0, maximumZ_ = 0;
    double radius_ = .3, clearance_ = .002, buildingExtra_ = .05, numericMargin_ = .005;
    std::string sourceSha256_;
    void validateAndIndex();
    std::uint32_t build(std::uint32_t first, std::uint32_t count);
};

} // namespace viewer::navmesh_path
