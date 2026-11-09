#include "physics_mesh_adjacency.hpp"
#include "collision_world.hpp"

#include <box3d/box3d.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <stdexcept>

namespace viewer {
namespace {
using Point = std::array<float, 3>;
using EdgeKey = std::array<Point, 2>;
constexpr float flatCosine = .9962f; // Match Box3D's five-degree edge classification.

Point point(glm::vec3 p) { return {p.x, p.y, p.z}; }
Point point(b3Vec3 p) { return {p.x, p.y, p.z}; }
glm::vec3 vector(const Point& p) { return {p[0], p[1], p[2]}; }
EdgeKey edgeKey(Point a, Point b) {
    if (b < a) std::swap(a, b);
    return {a, b};
}
struct Side {
    Point opposite;
    glm::vec3 normal;
};
struct Edge {
    std::array<Side, 2> sides{};
    unsigned count = 0;
};
struct Adjacency {
    std::map<EdgeKey, Edge> edges;
    std::map<Point, bool> smoothVertices;
};

Adjacency sourceAdjacency(const std::vector<StaticCollisionFace>& faces, glm::vec3 origin) {
    Adjacency result;
    for (const auto& face : faces) {
        std::array<glm::vec3, 3> positions;
        for (std::size_t i = 0; i < positions.size(); ++i) positions[i] = face.vertices[i] - origin;
        const auto cross = glm::cross(positions[1] - positions[0], positions[2] - positions[0]);
        const float squareLength = glm::dot(cross, cross);
        if (!std::isfinite(squareLength) || squareLength <= 0) continue;
        const auto normal = cross / std::sqrt(squareLength);
        for (std::size_t i = 0; i < positions.size(); ++i) {
            auto& edge = result.edges[edgeKey(point(positions[i]), point(positions[(i + 1) % 3]))];
            if (edge.count < edge.sides.size()) edge.sides[edge.count] = {point(positions[(i + 2) % 3]), normal};
            ++edge.count;
        }
    }
    for (const auto& [key, edge] : result.edges) {
        const bool smooth = edge.count == 2 && glm::dot(edge.sides[0].normal, edge.sides[1].normal) > flatCosine;
        for (const auto& vertex : key) {
            const auto [found, inserted] = result.smoothVertices.emplace(vertex, smooth);
            if (!inserted) found->second = found->second && smooth;
        }
    }
    return result;
}
} // namespace

void applyMeshSourceAdjacency(b3MeshData* mesh, const std::vector<StaticCollisionFace>& context,
                              glm::vec3 origin) {
    const auto adjacency = sourceAdjacency(context, origin);
    const auto* positions = b3GetMeshVertices(mesh);
    const auto* triangles = b3GetMeshTriangles(mesh);
    const auto* oldFlags = b3GetMeshFlags(mesh);
    std::vector<std::uint8_t> flags(oldFlags, oldFlags + mesh->triangleCount);
    constexpr std::array<int, 3> concave{b3_concaveEdge1, b3_concaveEdge2, b3_concaveEdge3};
    constexpr std::array<int, 3> inverse{b3_inverseConcaveEdge1, b3_inverseConcaveEdge2, b3_inverseConcaveEdge3};
    for (int index = 0; index < mesh->triangleCount; ++index) {
        const auto& triangle = triangles[index];
        const std::array<Point, 3> vertices{point(positions[triangle.index1]), point(positions[triangle.index2]),
                                           point(positions[triangle.index3])};
        const auto first = vector(vertices[0]);
        const auto cross = glm::cross(vector(vertices[1]) - first, vector(vertices[2]) - first);
        const float squareLength = glm::dot(cross, cross);
        if (squareLength <= 0) continue;
        const auto normal = cross / std::sqrt(squareLength);
        bool smoothFan = true;
        for (const auto& vertex : vertices) {
            const auto found = adjacency.smoothVertices.find(vertex);
            smoothFan = smoothFan && found != adjacency.smoothVertices.end() && found->second;
        }
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            const auto found = adjacency.edges.find(edgeKey(vertices[i], vertices[(i + 1) % 3]));
            if (found == adjacency.edges.end() || found->second.count != 2) {
                smoothFan = false;
                continue;
            }
            const auto& edge = found->second;
            const auto& opposite = vertices[(i + 2) % 3];
            const int self = edge.sides[0].opposite == opposite ? 0 : edge.sides[1].opposite == opposite ? 1 : -1;
            if (self < 0) {
                smoothFan = false;
                continue;
            }
            const auto& other = edge.sides[1 - self];
            // The underside mesh reverses both adjacent normals. Match its
            // stored winding instead of assuming input/BVH triangle order.
            const auto otherNormal = glm::dot(normal, edge.sides[self].normal) < 0 ? -other.normal : other.normal;
            const float volume = glm::dot(cross, vector(other.opposite) - first);
            const float cosine = glm::dot(normal, otherNormal);
            if (volume > 0 || cosine > flatCosine) flags[index] |= concave[i];
            if (volume < 0 || cosine > flatCosine) flags[index] |= inverse[i];
            const int flat = concave[i] | inverse[i];
            if (cosine > flatCosine && (oldFlags[index] & flat) != flat)
                flags[index] |= b3_externalAdjacency;
        }
        // Two incident flat edges alone do not prove that a vertex has support
        // all around it. Require complete smooth source fans at every vertex.
        if (smoothFan) flags[index] |= b3_smoothVertexFans;
    }
    if (!b3SetMeshEdgeFlags(mesh, flags.data(), mesh->triangleCount))
        throw std::runtime_error("Cannot restore scene mesh adjacency");
}
} // namespace viewer
