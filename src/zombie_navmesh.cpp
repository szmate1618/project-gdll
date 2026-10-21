#include "zombie_navmesh.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace viewer::navmesh_path {
namespace {
constexpr std::size_t headerBytes = 128;
constexpr char magic[8] = {'G', 'D', 'N', 'A', 'V', '0', '1', '\0'};
constexpr double positionTolerance = 1e-9;
bool finite(Point p) { return std::isfinite(p.x) && std::isfinite(p.z); }
double cross(Point a, Point b, Point c) {
    return (b.x-a.x)*(c.z-a.z)-(b.z-a.z)*(c.x-a.x);
}
std::uint64_t little(const unsigned char* p, unsigned count) {
    std::uint64_t result = 0;
    for (unsigned i = 0; i < count; ++i) result |= std::uint64_t(p[i]) << (8*i);
    return result;
}
double real(const unsigned char* p) {
    const auto bits = little(p, 8);
    double result;
    std::memcpy(&result, &bits, 8);
    return result;
}
bool inBounds(Point p, Point low, Point high) {
    return p.x >= low.x-positionTolerance && p.x <= high.x+positionTolerance &&
           p.z >= low.z-positionTolerance && p.z <= high.z+positionTolerance;
}
bool contains(const std::vector<Point>& vertices, const Triangle& triangle, Point p) {
    for (unsigned edge = 0; edge != 3; ++edge) {
        const auto a = vertices[triangle.vertex[edge]];
        const auto b = vertices[triangle.vertex[(edge+1)%3]];
        if (cross(a, b, p) < -positionTolerance*std::hypot(b.x-a.x, b.z-a.z)) return false;
    }
    return true;
}
// Intersection of a segment with a closed CCW triangle, as parameter interval.
bool interval(const std::vector<Point>& vertices, const Triangle& triangle,
              Point from, Point to, double& first, double& last) {
    first = 0;
    last = 1;
    for (unsigned edge = 0; edge != 3; ++edge) {
        const auto a = vertices[triangle.vertex[edge]];
        const auto b = vertices[triangle.vertex[(edge+1)%3]];
        const double initial = cross(a, b, from), final = cross(a, b, to);
        const double delta = final-initial;
        const double tolerance = positionTolerance*std::hypot(b.x-a.x, b.z-a.z);
        if (delta == 0) {
            if (initial < -tolerance) return false;
            continue;
        }
        const double boundary = -initial/delta;
        if (delta > 0) first = std::max(first, boundary);
        else last = std::min(last, boundary);
        if (first > last+1e-13) return false;
    }
    return last >= 0 && first <= 1;
}
}

Mesh::Mesh(std::vector<Point> vertices, std::vector<Triangle> triangles)
    : vertices_(std::move(vertices)), triangles_(std::move(triangles)) {
    if (!vertices_.empty()) {
        minimumX_ = maximumX_ = vertices_[0].x;
        minimumZ_ = maximumZ_ = vertices_[0].z;
        for (auto p : vertices_) {
            minimumX_ = std::min(minimumX_, p.x); minimumZ_ = std::min(minimumZ_, p.z);
            maximumX_ = std::max(maximumX_, p.x); maximumZ_ = std::max(maximumZ_, p.z);
        }
    }
    validateAndIndex();
}

Mesh Mesh::load(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("cannot open navmesh: "+filename);
    const auto length = file.tellg();
    if (length < std::streamoff(headerBytes)) throw std::runtime_error("truncated navmesh header");
    file.seekg(0);
    std::array<unsigned char, headerBytes> header{};
    if (!file.read(reinterpret_cast<char*>(header.data()), header.size()) ||
        std::memcmp(header.data(), magic, sizeof magic))
        throw std::runtime_error("invalid navmesh format");
    const auto vertexCount = little(header.data()+8, 4), triangleCount = little(header.data()+12, 4);
    const auto vertexBytes = little(header.data()+80, 8), triangleBytes = little(header.data()+88, 8);
    if (!vertexCount || !triangleCount || vertexBytes != vertexCount*16 || triangleBytes != triangleCount*24 ||
        std::uint64_t(length) != headerBytes+vertexBytes+triangleBytes)
        throw std::runtime_error("navmesh payload size mismatch");
    Mesh mesh;
    mesh.minimumX_ = real(header.data()+16); mesh.minimumZ_ = real(header.data()+24);
    mesh.maximumX_ = real(header.data()+32); mesh.maximumZ_ = real(header.data()+40);
    mesh.radius_ = real(header.data()+48); mesh.clearance_ = real(header.data()+56);
    mesh.buildingExtra_ = real(header.data()+64); mesh.numericMargin_ = real(header.data()+72);
    if (!std::isfinite(mesh.minimumX_) || !std::isfinite(mesh.minimumZ_) ||
        !std::isfinite(mesh.maximumX_) || !std::isfinite(mesh.maximumZ_) ||
        mesh.minimumX_ >= mesh.maximumX_ || mesh.minimumZ_ >= mesh.maximumZ_)
        throw std::runtime_error("invalid navmesh bounds");
    for (double padding : {mesh.radius_, mesh.clearance_, mesh.buildingExtra_, mesh.numericMargin_})
        if (!std::isfinite(padding) || padding < 0) throw std::runtime_error("invalid navmesh padding");
    mesh.vertices_.resize(std::size_t(vertexCount));
    mesh.triangles_.resize(std::size_t(triangleCount));
    // Decode in batches to avoid a second full mesh-sized payload allocation.
    std::array<unsigned char, 16*4096> buffer{};
    for (std::size_t first = 0; first < mesh.vertices_.size();) {
        const auto count = std::min(mesh.vertices_.size()-first, buffer.size()/16);
        if (!file.read(reinterpret_cast<char*>(buffer.data()), std::streamsize(count*16)))
            throw std::runtime_error("truncated navmesh vertices");
        for (std::size_t i = 0; i < count; ++i)
            mesh.vertices_[first+i] = {real(buffer.data()+16*i), real(buffer.data()+16*i+8)};
        first += count;
    }
    for (std::size_t first = 0; first < mesh.triangles_.size();) {
        const auto count = std::min(mesh.triangles_.size()-first, buffer.size()/24);
        if (!file.read(reinterpret_cast<char*>(buffer.data()), std::streamsize(count*24)))
            throw std::runtime_error("truncated navmesh triangles");
        for (std::size_t i = 0; i < count; ++i) for (unsigned k = 0; k != 3; ++k) {
            mesh.triangles_[first+i].vertex[k] = std::uint32_t(little(buffer.data()+24*i+4*k, 4));
            mesh.triangles_[first+i].neighbor[k] = std::uint32_t(little(buffer.data()+24*i+12+4*k, 4));
        }
        first += count;
    }
    std::ostringstream hash;
    hash << std::hex << std::setfill('0');
    for (unsigned i = 96; i != headerBytes; ++i) hash << std::setw(2) << unsigned(header[i]);
    mesh.sourceSha256_ = hash.str();
    mesh.validateAndIndex();
    return mesh;
}

void Mesh::validateAndIndex() {
    if (vertices_.empty() || triangles_.empty() || vertices_.size() >= noTriangle || triangles_.size() >= noTriangle/2)
        throw std::invalid_argument("navmesh must have addressable vertices and triangles");
    for (auto p : vertices_)
        if (!finite(p) || !inBounds(p, {minimumX_, minimumZ_}, {maximumX_, maximumZ_}))
            throw std::invalid_argument("invalid navmesh vertex");
    for (const auto& triangle : triangles_) {
        for (auto vertex : triangle.vertex)
            if (vertex >= vertices_.size()) throw std::invalid_argument("navmesh vertex index outside payload");
        const auto area = cross(vertices_[triangle.vertex[0]], vertices_[triangle.vertex[1]], vertices_[triangle.vertex[2]]);
        if (!std::isfinite(area) || area <= 0) throw std::invalid_argument("navmesh triangle must be nondegenerate and CCW");
        for (auto neighbor : triangle.neighbor)
            if (neighbor != noTriangle && neighbor >= triangles_.size())
                throw std::invalid_argument("navmesh neighbor index outside payload");
    }
    for (std::size_t index = 0; index < triangles_.size(); ++index) {
        const auto& triangle = triangles_[index];
        for (unsigned edge = 0; edge != 3; ++edge) {
            const auto neighbor = triangle.neighbor[edge];
            if (neighbor == noTriangle) continue;
            if (neighbor == index) throw std::invalid_argument("navmesh triangle cannot neighbor itself");
            const auto& other = triangles_[neighbor];
            bool reciprocal = false;
            for (unsigned e = 0; e != 3; ++e)
                reciprocal |= other.vertex[e] == triangle.vertex[(edge+1)%3] &&
                              other.vertex[(e+1)%3] == triangle.vertex[edge] && other.neighbor[e] == index;
            if (!reciprocal) throw std::invalid_argument("navmesh portal adjacency is not reciprocal");
        }
    }
    // Keep the original double arithmetic so graph priorities and BVH ordering
    // remain identical, including triangles much smaller than a float ULP.
    geometry_.resize(triangles_.size());
    for (std::size_t index = 0; index < triangles_.size(); ++index) {
        const auto& triangle = triangles_[index];
        const auto a = vertices_[triangle.vertex[0]], b = vertices_[triangle.vertex[1]], c = vertices_[triangle.vertex[2]];
        geometry_[index].center = {a.x+(b.x-a.x)/3+(c.x-a.x)/3,
                                   a.z+(b.z-a.z)/3+(c.z-a.z)/3};
    }
    // Every neighbor centroid must be initialized before calculating costs.
    for (std::size_t index = 0; index < triangles_.size(); ++index) {
        const auto center = geometry_[index].center;
        for (unsigned edge = 0; edge != 3; ++edge) {
            const auto neighbor = triangles_[index].neighbor[edge];
            if (neighbor == noTriangle) continue;
            const auto next = geometry_[neighbor].center;
            geometry_[index].neighborDistances[edge] = std::hypot(center.x-next.x, center.z-next.z);
        }
    }
    order_.resize(triangles_.size());
    std::iota(order_.begin(), order_.end(), 0u);
    nodes_.clear();
    // Median splitting reaches leaves of at most 16 triangles. Reserve the
    // corresponding complete tree rather than twice its actual storage.
    std::size_t leafCapacity = 1;
    while (leafCapacity < (triangles_.size()+15)/16) leafCapacity *= 2;
    nodes_.reserve(leafCapacity*2-1);
    build(0, static_cast<std::uint32_t>(triangles_.size()));
}

std::uint32_t Mesh::build(std::uint32_t first, std::uint32_t count) {
    Node node;
    node.minimum = {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
    node.maximum = {-node.minimum.x, -node.minimum.z};
    for (auto i = first; i < first+count; ++i) for (auto vertex : triangles_[order_[i]].vertex) {
        const auto p = vertices_[vertex];
        node.minimum.x = std::min(node.minimum.x, p.x); node.minimum.z = std::min(node.minimum.z, p.z);
        node.maximum.x = std::max(node.maximum.x, p.x); node.maximum.z = std::max(node.maximum.z, p.z);
    }
    const auto index = static_cast<std::uint32_t>(nodes_.size());
    nodes_.push_back(node);
    if (count <= 16) { nodes_[index].first = first; nodes_[index].count = count; return index; }
    const bool axisX = node.maximum.x-node.minimum.x > node.maximum.z-node.minimum.z;
    const auto middle = first+count/2;
    std::nth_element(order_.begin()+first, order_.begin()+middle, order_.begin()+first+count,
        [&](auto a, auto b) {
            const auto pa = centroid(a), pb = centroid(b);
            const double va = axisX ? pa.x : pa.z, vb = axisX ? pb.x : pb.z;
            return va == vb ? a < b : va < vb;
        });
    const auto left = build(first, middle-first), right = build(middle, first+count-middle);
    nodes_[index].left = left; nodes_[index].right = right;
    return index;
}

std::uint32_t Mesh::locate(Point point, std::uint64_t* tests) const {
    if (!finite(point) || nodes_.empty()) return noTriangle;
    std::array<std::uint32_t, 64> stack{};
    std::size_t size = 1;
    while (size) {
        const auto& node = nodes_[stack[--size]];
        if (!inBounds(point, node.minimum, node.maximum)) continue;
        if (!node.count) { stack[size++] = node.right; stack[size++] = node.left; continue; }
        for (auto i = node.first; i < node.first+node.count; ++i) {
            const auto triangle = order_[i];
            if (tests) ++*tests;
            if (contains(vertices_, triangles_[triangle], point)) return triangle;
        }
    }
    return noTriangle;
}

bool Mesh::segmentClear(Point a, Point b, std::uint64_t* tests) const {
    if (locate(a, tests) == noTriangle || locate(b, tests) == noTriangle) return false;
    if (a == b) return true;
    double progress = 0;
    // Each positive advance reaches at least one new interval endpoint. There
    // are at most two per triangle; the bound also catches numerical cycling.
    for (std::size_t step = 0; step <= triangles_.size()*2; ++step) {
        const Point current{a.x+(b.x-a.x)*progress, a.z+(b.z-a.z)*progress};
        double furthest = progress;
        std::array<std::uint32_t, 64> stack{};
        std::size_t size = 1;
        while (size) {
            const auto& node = nodes_[stack[--size]];
            if (!inBounds(current, node.minimum, node.maximum)) continue;
            if (!node.count) { stack[size++] = node.right; stack[size++] = node.left; continue; }
            for (auto i = node.first; i < node.first+node.count; ++i) {
                if (tests) ++*tests;
                double first = 0, last = 0;
                if (interval(vertices_, triangles_[order_[i]], a, b, first, last) && first <= progress+1e-12)
                    furthest = std::max(furthest, last);
            }
        }
        if (furthest >= 1-1e-13) return true;
        if (furthest <= progress+1e-14) return false;
        progress = furthest;
    }
    return false;
}

std::size_t Mesh::meshBytes() const { return vertices_.capacity()*sizeof(Point)+triangles_.capacity()*sizeof(Triangle); }
std::size_t Mesh::geometryCacheBytes() const { return geometry_.capacity()*sizeof(Geometry); }
std::size_t Mesh::indexBytes() const { return order_.capacity()*sizeof(std::uint32_t)+nodes_.capacity()*sizeof(Node); }

} // namespace viewer::navmesh_path
