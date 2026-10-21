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
constexpr char triangleMagic[8] = {'G', 'D', 'N', 'A', 'V', '0', '1', '\0'};
constexpr char polygonMagic[8] = {'G', 'D', 'N', 'A', 'V', '0', '2', '\0'};
constexpr double positionTolerance = 1e-9;
bool finite(Point p) { return std::isfinite(p.x) && std::isfinite(p.z); }
double cross(Point a, Point b, Point c) {
    return (b.x-a.x)*(c.z-a.z)-(b.z-a.z)*(c.x-a.x);
}
long double preciseCross(Point a, Point b, Point c) {
    return (static_cast<long double>(b.x)-a.x)*(static_cast<long double>(c.z)-a.z)-
           (static_cast<long double>(b.z)-a.z)*(static_cast<long double>(c.x)-a.x);
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
bool contains(const std::vector<Point>& vertices, const std::vector<Edge>& edges,
              Polygon polygon, Point p) {
    for (unsigned edge = 0; edge != polygon.edgeCount; ++edge) {
        const auto a = vertices[edges[polygon.firstEdge+edge].vertex];
        const auto b = vertices[edges[polygon.firstEdge+(edge+1)%polygon.edgeCount].vertex];
        if (cross(a, b, p) < -positionTolerance*std::hypot(b.x-a.x, b.z-a.z)) return false;
    }
    return true;
}
// Intersection of a segment with a closed convex CCW polygon, as an interval.
bool interval(const std::vector<Point>& vertices, const std::vector<Edge>& edges,
              Polygon polygon, Point from, Point to, double& first, double& last) {
    first = 0;
    last = 1;
    for (unsigned edge = 0; edge != polygon.edgeCount; ++edge) {
        const auto a = vertices[edges[polygon.firstEdge+edge].vertex];
        const auto b = vertices[edges[polygon.firstEdge+(edge+1)%polygon.edgeCount].vertex];
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
    : vertices_(std::move(vertices)) {
    if (triangles.size() > std::size_t(noPolygon)/3)
        throw std::invalid_argument("navmesh has too many edges");
    polygons_.resize(triangles.size());
    edges_.resize(triangles.size()*3);
    for (std::size_t index = 0; index < triangles.size(); ++index) {
        polygons_[index] = {static_cast<std::uint32_t>(index*3), 3};
        for (unsigned edge = 0; edge != 3; ++edge)
            edges_[index*3+edge] = {triangles[index].vertex[edge], triangles[index].neighbor[edge]};
    }
    deriveBounds();
    validateAndIndex();
}

Mesh::Mesh(std::vector<Point> vertices, std::vector<Polygon> polygons, std::vector<Edge> edges)
    : vertices_(std::move(vertices)), polygons_(std::move(polygons)), edges_(std::move(edges)) {
    deriveBounds();
    validateAndIndex();
}

void Mesh::deriveBounds() {
    if (vertices_.empty()) return;
    minimumX_ = maximumX_ = vertices_[0].x;
    minimumZ_ = maximumZ_ = vertices_[0].z;
    for (auto p : vertices_) {
        minimumX_ = std::min(minimumX_, p.x); minimumZ_ = std::min(minimumZ_, p.z);
        maximumX_ = std::max(maximumX_, p.x); maximumZ_ = std::max(maximumZ_, p.z);
    }
}

Mesh Mesh::load(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("cannot open navmesh: "+filename);
    const auto length = file.tellg();
    if (length < std::streamoff(headerBytes)) throw std::runtime_error("truncated navmesh header");
    file.seekg(0);
    std::array<unsigned char, headerBytes> header{};
    if (!file.read(reinterpret_cast<char*>(header.data()), header.size()))
        throw std::runtime_error("truncated navmesh header");
    const bool legacy = std::memcmp(header.data(), triangleMagic, sizeof triangleMagic) == 0;
    if (!legacy && std::memcmp(header.data(), polygonMagic, sizeof polygonMagic))
        throw std::runtime_error("invalid navmesh format");
    const auto vertexCount = little(header.data()+8, 4), polygonCount = little(header.data()+12, 4);
    const auto vertexBytes = little(header.data()+80, 8), polygonBytes = little(header.data()+88, 8);
    if (!vertexCount || !polygonCount || vertexCount >= noPolygon || polygonCount >= noPolygon/2 ||
        vertexBytes != vertexCount*16 || polygonBytes > std::uint64_t(length)-headerBytes ||
        std::uint64_t(length)-headerBytes-polygonBytes != vertexBytes)
        throw std::runtime_error("navmesh payload size mismatch");
    std::uint64_t edgeCount = polygonCount*3;
    if (legacy) {
        if (polygonBytes != polygonCount*24) throw std::runtime_error("navmesh triangle payload size mismatch");
    } else {
        if (polygonBytes < polygonCount*28 || polygonBytes > polygonCount*52 ||
            (polygonBytes-polygonCount*4)%8)
            throw std::runtime_error("navmesh polygon payload size mismatch");
        edgeCount = (polygonBytes-polygonCount*4)/8;
    }
    if (edgeCount >= noPolygon) throw std::runtime_error("navmesh has too many edges");
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
    mesh.polygons_.resize(std::size_t(polygonCount));
    mesh.edges_.resize(std::size_t(edgeCount));
    // Decode vertices in batches without a second full mesh-sized allocation.
    std::array<unsigned char, 16*4096> buffer{};
    for (std::size_t first = 0; first < mesh.vertices_.size();) {
        const auto count = std::min(mesh.vertices_.size()-first, buffer.size()/16);
        if (!file.read(reinterpret_cast<char*>(buffer.data()), std::streamsize(count*16)))
            throw std::runtime_error("truncated navmesh vertices");
        for (std::size_t i = 0; i < count; ++i)
            mesh.vertices_[first+i] = {real(buffer.data()+16*i), real(buffer.data()+16*i+8)};
        first += count;
    }
    // At most six edges per polygon, so each record fits on the stack. Verify
    // the advertised aggregate edge count before indexing any decoded record.
    std::size_t firstEdge = 0;
    for (auto& polygon : mesh.polygons_) {
        unsigned count = 3;
        if (!legacy) {
            if (!file.read(reinterpret_cast<char*>(buffer.data()), 4))
                throw std::runtime_error("truncated navmesh polygon count");
            count = static_cast<unsigned>(little(buffer.data(), 4));
        }
        if (count < 3 || count > 6 || count > mesh.edges_.size()-firstEdge)
            throw std::runtime_error("invalid navmesh polygon edge count");
        if (!file.read(reinterpret_cast<char*>(buffer.data()), std::streamsize(count*8)))
            throw std::runtime_error("truncated navmesh polygons");
        polygon = {static_cast<std::uint32_t>(firstEdge), count};
        for (unsigned edge = 0; edge < count; ++edge)
            mesh.edges_[firstEdge+edge] = {static_cast<std::uint32_t>(little(buffer.data()+4*edge, 4)),
                static_cast<std::uint32_t>(little(buffer.data()+4*count+4*edge, 4))};
        firstEdge += count;
    }
    if (firstEdge != mesh.edges_.size()) throw std::runtime_error("navmesh polygon edge total mismatch");
    std::ostringstream hash;
    hash << std::hex << std::setfill('0');
    for (unsigned i = 96; i != headerBytes; ++i) hash << std::setw(2) << unsigned(header[i]);
    mesh.sourceSha256_ = hash.str();
    mesh.validateAndIndex();
    return mesh;
}

void Mesh::validateAndIndex() {
    if (vertices_.empty() || polygons_.empty() || vertices_.size() >= noPolygon ||
        polygons_.size() >= noPolygon/2 || edges_.size() >= noPolygon)
        throw std::invalid_argument("navmesh must have addressable vertices, polygons and edges");
    for (auto p : vertices_)
        if (!finite(p) || !inBounds(p, {minimumX_, minimumZ_}, {maximumX_, maximumZ_}))
            throw std::invalid_argument("invalid navmesh vertex");
    std::size_t edgeTotal = 0;
    for (const auto polygon : polygons_) {
        if (polygon.edgeCount < 3 || polygon.edgeCount > 6 || polygon.firstEdge != edgeTotal ||
            polygon.edgeCount > edges_.size()-edgeTotal)
            throw std::invalid_argument("invalid navmesh polygon edge range");
        edgeTotal += polygon.edgeCount;
        for (unsigned edge = 0; edge != polygon.edgeCount; ++edge) {
            const auto entry = edges_[polygon.firstEdge+edge];
            if (entry.vertex >= vertices_.size()) throw std::invalid_argument("navmesh vertex index outside payload");
            if (entry.neighbor != noPolygon && entry.neighbor >= polygons_.size())
                throw std::invalid_argument("navmesh neighbor index outside payload");
            for (unsigned previous = 0; previous < edge; ++previous)
                if (entry.vertex == edges_[polygon.firstEdge+previous].vertex ||
                    vertices_[entry.vertex] == vertices_[edges_[polygon.firstEdge+previous].vertex])
                    throw std::invalid_argument("navmesh polygon has duplicate vertices");
        }
        const auto a = vertices_[edges_[polygon.firstEdge].vertex];
        if (polygon.edgeCount == 3) {
            // Preserve v1's strict double-area validation and arithmetic.
            const auto area = cross(a, vertices_[edges_[polygon.firstEdge+1].vertex],
                                       vertices_[edges_[polygon.firstEdge+2].vertex]);
            if (!std::isfinite(area) || area <= 0)
                throw std::invalid_argument("navmesh triangle must be nondegenerate and CCW");
        } else {
            bool positiveArea = false;
            // Every vertex must lie on or to the left of every oriented edge.
            // Unlike turn-only checks, this also rejects self-crossing stars.
            for (unsigned edge = 0; edge != polygon.edgeCount; ++edge) {
                const auto from = vertices_[edges_[polygon.firstEdge+edge].vertex];
                const auto to = vertices_[edges_[polygon.firstEdge+(edge+1)%polygon.edgeCount].vertex];
                for (unsigned vertex = 0; vertex != polygon.edgeCount; ++vertex) {
                    const auto area = preciseCross(from, to, vertices_[edges_[polygon.firstEdge+vertex].vertex]);
                    if (!std::isfinite(area) || area < 0)
                        throw std::invalid_argument("navmesh polygon must be convex and CCW");
                    positiveArea |= area > 0;
                }
            }
            if (!positiveArea) throw std::invalid_argument("navmesh polygon is degenerate");
        }
    }
    if (edgeTotal != edges_.size()) throw std::invalid_argument("navmesh contains unused edges");
    for (std::size_t index = 0; index < polygons_.size(); ++index) {
        const auto polygon = polygons_[index];
        for (unsigned edge = 0; edge != polygon.edgeCount; ++edge) {
            const auto entry = edges_[polygon.firstEdge+edge];
            if (entry.neighbor == noPolygon) continue;
            if (entry.neighbor == index) throw std::invalid_argument("navmesh polygon cannot neighbor itself");
            const auto other = polygons_[entry.neighbor];
            bool reciprocal = false;
            const auto nextVertex = edges_[polygon.firstEdge+(edge+1)%polygon.edgeCount].vertex;
            for (unsigned e = 0; e != other.edgeCount; ++e)
                reciprocal |= edges_[other.firstEdge+e].vertex == nextVertex &&
                    edges_[other.firstEdge+(e+1)%other.edgeCount].vertex == entry.vertex &&
                    edges_[other.firstEdge+e].neighbor == index;
            if (!reciprocal) throw std::invalid_argument("navmesh portal adjacency is not reciprocal");
        }
    }
    centers_.resize(polygons_.size());
    neighborDistances_.assign(edges_.size(), 0);
    for (std::size_t index = 0; index < polygons_.size(); ++index) {
        const auto polygon = polygons_[index];
        const auto a = vertices_[edges_[polygon.firstEdge].vertex];
        if (polygon.edgeCount == 3) {
            const auto b = vertices_[edges_[polygon.firstEdge+1].vertex];
            const auto c = vertices_[edges_[polygon.firstEdge+2].vertex];
            // Exact legacy arithmetic preserves triangle A* priorities/ties.
            centers_[index] = {a.x+(b.x-a.x)/3+(c.x-a.x)/3, a.z+(b.z-a.z)/3+(c.z-a.z)/3};
        } else {
            Point center = a;
            for (unsigned edge = 1; edge != polygon.edgeCount; ++edge) {
                const auto p = vertices_[edges_[polygon.firstEdge+edge].vertex];
                center.x += (p.x-a.x)/polygon.edgeCount;
                center.z += (p.z-a.z)/polygon.edgeCount;
            }
            centers_[index] = center;
        }
    }
    // All centers are initialized before calculating graph costs.
    for (std::size_t index = 0; index < polygons_.size(); ++index) {
        const auto polygon = polygons_[index];
        const auto center = centers_[index];
        for (unsigned edge = 0; edge != polygon.edgeCount; ++edge) {
            const auto flatEdge = polygon.firstEdge+edge;
            const auto neighbor = edges_[flatEdge].neighbor;
            if (neighbor == noPolygon) continue;
            const auto next = centers_[neighbor];
            neighborDistances_[flatEdge] = std::hypot(center.x-next.x, center.z-next.z);
        }
    }
    order_.resize(polygons_.size());
    std::iota(order_.begin(), order_.end(), 0u);
    nodes_.clear();
    // Median splitting reaches leaves of at most 16 polygons.
    std::size_t leafCapacity = 1;
    while (leafCapacity < (polygons_.size()+15)/16) leafCapacity *= 2;
    nodes_.reserve(leafCapacity*2-1);
    build(0, static_cast<std::uint32_t>(polygons_.size()));
}

std::uint32_t Mesh::build(std::uint32_t first, std::uint32_t count) {
    Node node;
    node.minimum = {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
    node.maximum = {-node.minimum.x, -node.minimum.z};
    for (auto i = first; i < first+count; ++i) {
        const auto polygon = polygons_[order_[i]];
        for (unsigned edge = 0; edge != polygon.edgeCount; ++edge) {
            const auto p = vertices_[edges_[polygon.firstEdge+edge].vertex];
            node.minimum.x = std::min(node.minimum.x, p.x); node.minimum.z = std::min(node.minimum.z, p.z);
            node.maximum.x = std::max(node.maximum.x, p.x); node.maximum.z = std::max(node.maximum.z, p.z);
        }
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
    if (!finite(point) || nodes_.empty()) return noPolygon;
    std::array<std::uint32_t, 64> stack{};
    std::size_t size = 1;
    while (size) {
        const auto& node = nodes_[stack[--size]];
        if (!inBounds(point, node.minimum, node.maximum)) continue;
        if (!node.count) { stack[size++] = node.right; stack[size++] = node.left; continue; }
        for (auto i = node.first; i < node.first+node.count; ++i) {
            const auto polygon = order_[i];
            if (tests) ++*tests;
            if (contains(vertices_, edges_, polygons_[polygon], point)) return polygon;
        }
    }
    return noPolygon;
}

bool Mesh::segmentClear(Point a, Point b, std::uint64_t* tests) const {
    if (locate(a, tests) == noPolygon || locate(b, tests) == noPolygon) return false;
    if (a == b) return true;
    double progress = 0;
    // Each positive advance reaches a new interval endpoint; at most two exist
    // per convex polygon. The bound also catches numerical cycling.
    for (std::size_t step = 0; step <= polygons_.size()*2; ++step) {
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
                if (interval(vertices_, edges_, polygons_[order_[i]], a, b, first, last) && first <= progress+1e-12)
                    furthest = std::max(furthest, last);
            }
        }
        if (furthest >= 1-1e-13) return true;
        if (furthest <= progress+1e-14) return false;
        progress = furthest;
    }
    return false;
}

std::size_t Mesh::meshBytes() const {
    return vertices_.capacity()*sizeof(Point)+polygons_.capacity()*sizeof(Polygon)+edges_.capacity()*sizeof(Edge);
}
std::size_t Mesh::geometryCacheBytes() const {
    return centers_.capacity()*sizeof(Point)+neighborDistances_.capacity()*sizeof(double);
}
std::size_t Mesh::indexBytes() const { return order_.capacity()*sizeof(std::uint32_t)+nodes_.capacity()*sizeof(Node); }

} // namespace viewer::navmesh_path
