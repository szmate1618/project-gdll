#include "binding_geometry.hpp"
#include "physics_world.hpp"
#include <box3d/box3d.h>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace viewer {
namespace {
constexpr double epsilon = 1e-7;
constexpr double pi = 3.14159265358979323846;
constexpr std::size_t maxShapes = 512, maxTriangles = 16384;
const glm::dvec3 up(0, 1, 0);
bool finite(glm::vec3 value) { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }
template<class V> glm::dvec3 vector(V value) { return {double(value.x), double(value.y), double(value.z)}; }
b3Vec3 vector(glm::dvec3 value) { return {float(value.x), float(value.y), float(value.z)}; }

struct Plane {
    glm::dvec3 center, right, normal;
    double distance(glm::dvec3 point) const { return glm::dot(point - center, normal); }
    glm::dvec2 coordinates(glm::dvec3 point) const { return {glm::dot(point - center, right), point.y - center.y}; }
    bool inside(glm::dvec3 point) const {
        const auto p = coordinates(point);
        return std::abs(p.x) <= BindingPlane::halfSize + epsilon && std::abs(p.y) <= BindingPlane::halfSize + epsilon;
    }
};
Plane normalized(const BindingPlane& plane) {
    if (!finite(plane.center) || !finite(plane.right)) throw std::invalid_argument("Binding planes must be finite");
    auto right = glm::dvec3(plane.right.x, 0, plane.right.z);
    if (glm::length(right) < epsilon) throw std::invalid_argument("Binding plane needs a horizontal right axis");
    right = glm::normalize(right);
    return {glm::dvec3(plane.center), right, glm::cross(right, up)};
}

struct Frame {
    glm::dvec3 position;
    glm::dquat rotation;
    glm::dvec3 world(glm::dvec3 point) const { return position + rotation * point; }
    glm::dvec3 local(glm::dvec3 point) const { return glm::conjugate(rotation) * (point - position); }
};
Frame bodyFrame(b3BodyId body) {
    const auto transform = b3Body_GetTransform(body);
    const auto q = transform.q;
    return {vector(transform.p), {double(q.s), double(q.v.x), double(q.v.y), double(q.v.z)}};
}

bool clip(const Plane& plane, glm::dvec3& a, glm::dvec3& b) {
    const auto p = plane.coordinates(a), delta = plane.coordinates(b) - p;
    double lower = 0, upper = 1;
    for (int axis = 0; axis < 2; ++axis) {
        if (std::abs(delta[axis]) < epsilon) {
            if (std::abs(p[axis]) > BindingPlane::halfSize + epsilon) return false;
            continue;
        }
        auto enter = (-BindingPlane::halfSize - p[axis]) / delta[axis];
        auto leave = (BindingPlane::halfSize - p[axis]) / delta[axis];
        if (enter > leave) std::swap(enter, leave);
        lower = std::max(lower, enter); upper = std::min(upper, leave);
        if (lower > upper) return false;
    }
    const auto origin = a, direction = b - a;
    a = origin + lower * direction; b = origin + upper * direction;
    return glm::length(b - a) > epsilon;
}

struct Key {
    std::uint64_t body;
    std::uint8_t plane;
    std::array<long long, 4> coordinates;
    bool operator<(const Key& other) const { return std::tie(body, plane, coordinates) < std::tie(other.body, other.plane, other.coordinates); }
};
struct StoredSegment { BindingSegment segment; std::uint64_t coplanarShape = 0; };

struct Sections {
    BindingGeometry result;
    std::array<Plane, 5> planes;
    std::map<Key, StoredSegment> segments;
    std::size_t triangles = 0;
    bool full = false;

    explicit Sections(const std::array<BindingPlane, 5>& source) {
        for (std::size_t index = 0; index < planes.size(); ++index) planes[index] = normalized(source[index]);
    }
    void append(std::size_t planeIndex, b3BodyId body, glm::dvec3 a, glm::dvec3 b, std::uint64_t coplanarShape = 0) {
        if (full || !clip(planes[planeIndex], a, b)) return;
        auto p = planes[planeIndex].coordinates(a), q = planes[planeIndex].coordinates(b);
        if (std::tie(p.x, p.y) > std::tie(q.x, q.y)) { std::swap(a, b); std::swap(p, q); }
        Key key{b3StoreBodyId(body), static_cast<std::uint8_t>(planeIndex),
            {std::llround(p.x * 1e6), std::llround(p.y * 1e6), std::llround(q.x * 1e6), std::llround(q.y * 1e6)}};
        const auto found = segments.find(key);
        if (found != segments.end()) {
            // Two coplanar triangles share an internal edge; it is not an outline.
            if (coplanarShape && found->second.coplanarShape == coplanarShape) segments.erase(found);
            else found->second.coplanarShape = 0;
            return;
        }
        if (segments.size() >= BindingGeometry::maxSegments) { result.truncated = full = true; return; }
        segments.emplace(key, StoredSegment{{glm::vec3(a), glm::vec3(b), body, static_cast<std::uint8_t>(planeIndex)}, coplanarShape});
    }
    void polygon(std::size_t planeIndex, b3BodyId body, const glm::dvec3* points, std::size_t count,
                 std::uint64_t coplanarShape = 0) {
        if (full || count < 3) return;
        const auto& plane = planes[planeIndex];
        std::array<glm::dvec3, 256> hits{};
        std::size_t hitCount = 0;
        bool coplanar = true;
        for (std::size_t index = 0; index < count; ++index)
            coplanar = coplanar && std::abs(plane.distance(points[index])) < epsilon;
        if (coplanar) {
            for (std::size_t index = 0; index < count; ++index)
                append(planeIndex, body, points[index], points[(index + 1) % count], coplanarShape);
            return;
        }
        const auto add = [&](glm::dvec3 point) {
            for (std::size_t index = 0; index < hitCount; ++index)
                if (glm::length(hits[index] - point) < epsilon) return;
            if (hitCount < hits.size()) hits[hitCount++] = point;
        };
        for (std::size_t index = 0; index < count; ++index) {
            const auto a = points[index], b = points[(index + 1) % count];
            const double da = plane.distance(a), db = plane.distance(b);
            if (std::abs(da) < epsilon) add(a);
            if ((da < -epsilon && db > epsilon) || (da > epsilon && db < -epsilon)) add(a + (b - a) * (da / (da - db)));
        }
        double longest = 0;
        glm::dvec3 a{}, b{};
        for (std::size_t first = 0; first < hitCount; ++first)
            for (std::size_t second = first + 1; second < hitCount; ++second) {
                const double length = glm::length(hits[first] - hits[second]);
                if (length > longest) { longest = length; a = hits[first]; b = hits[second]; }
            }
        if (longest > epsilon) append(planeIndex, body, a, b);
    }
};

void roots(std::vector<double>& angles, double cosine, double sine, double value) {
    const double radius = std::hypot(cosine, sine);
    if (radius < epsilon || std::abs(value) > radius + epsilon) return;
    const double phase = std::atan2(sine, cosine), offset = std::acos(std::clamp(value / radius, -1.0, 1.0));
    for (double angle : {phase - offset, phase + offset}) {
        angle = std::fmod(angle + 2 * pi, 2 * pi);
        if (angle < 0) angle += 2 * pi;
        angles.push_back(angle);
    }
}

template<class Accept>
void curve(Sections& sections, std::size_t planeIndex, b3BodyId body,
    glm::dvec3 center, glm::dvec3 cosine, glm::dvec3 sine, std::vector<double> angles, Accept accept) {
    if (sections.full) return;
    const auto& plane = sections.planes[planeIndex];
    const auto p = plane.coordinates(center);
    const glm::dvec2 c(glm::dot(cosine, plane.right), cosine.y), s(glm::dot(sine, plane.right), sine.y);
    angles.reserve(80);
    for (int index = 0; index <= 64; ++index) angles.push_back(2 * pi * index / 64);
    for (int axis = 0; axis < 2; ++axis)
        for (double edge : {-double(BindingPlane::halfSize), double(BindingPlane::halfSize)}) roots(angles, c[axis], s[axis], edge - p[axis]);
    std::sort(angles.begin(), angles.end());
    angles.erase(std::unique(angles.begin(), angles.end(), [](double a, double b) { return std::abs(a - b) < 1e-10; }), angles.end());
    const auto point = [&](double angle) { return center + cosine * std::cos(angle) + sine * std::sin(angle); };
    for (std::size_t index = 1; index < angles.size() && !sections.full; ++index) {
        const auto midpoint = point((angles[index - 1] + angles[index]) * .5);
        if (plane.inside(midpoint) && accept(midpoint)) sections.append(planeIndex, body, point(angles[index - 1]), point(angles[index]));
    }
}

void sphere(Sections& sections, std::size_t planeIndex, b3BodyId body, glm::dvec3 center, double radius,
            glm::dvec3 hemisphereAxis = {}, int hemisphere = 0) {
    const auto& plane = sections.planes[planeIndex];
    const double distance = plane.distance(center);
    if (std::abs(distance) >= radius) return;
    const double circleRadius = std::sqrt(std::max(0.0, radius * radius - distance * distance));
    const auto circleCenter = center - plane.normal * distance;
    const auto cosine = plane.right * circleRadius, sine = up * circleRadius;
    std::vector<double> angles;
    if (hemisphere) roots(angles, glm::dot(hemisphereAxis, cosine), glm::dot(hemisphereAxis, sine), -glm::dot(hemisphereAxis, circleCenter - center));
    curve(sections, planeIndex, body, circleCenter, cosine, sine, std::move(angles), [&](glm::dvec3 point) {
        return !hemisphere || hemisphere * glm::dot(point - center, hemisphereAxis) >= -epsilon;
    });
}

void capsule(Sections& sections, std::size_t planeIndex, b3BodyId body, glm::dvec3 a, glm::dvec3 b, double radius) {
    const double length = glm::length(b - a);
    if (length < epsilon) { sphere(sections, planeIndex, body, a, radius); return; }
    const auto axis = (b - a) / length;
    const auto first = glm::normalize(glm::cross(axis, std::abs(axis.y) < .9 ? up : glm::dvec3(1, 0, 0)));
    const auto second = glm::cross(axis, first);
    const auto& plane = sections.planes[planeIndex];
    const double along = glm::dot(plane.normal, axis), distance = plane.distance(a);
    if (std::abs(along) > 1e-8) {
        const double start = -distance / along, c = -radius * glm::dot(plane.normal, first) / along,
                     s = -radius * glm::dot(plane.normal, second) / along;
        const auto center = a + axis * start;
        const auto cosine = radius * first + axis * c, sine = radius * second + axis * s;
        std::vector<double> angles;
        roots(angles, c, s, -start); roots(angles, c, s, length - start);
        curve(sections, planeIndex, body, center, cosine, sine, std::move(angles), [&](glm::dvec3 point) {
            const double t = glm::dot(point - a, axis);
            return t >= -epsilon && t <= length + epsilon;
        });
    } else if (std::abs(distance) <= radius) {
        std::vector<double> angles;
        roots(angles, glm::dot(plane.normal, first), glm::dot(plane.normal, second), -distance / radius);
        for (const auto angle : angles) {
            const auto offset = radius * (first * std::cos(angle) + second * std::sin(angle));
            sections.append(planeIndex, body, a + offset, b + offset);
        }
    }
    sphere(sections, planeIndex, body, a, radius, axis, -1);
    sphere(sections, planeIndex, body, b, radius, axis, 1);
}

void hull(Sections& sections, std::size_t planeIndex, b3BodyId body, const Frame& frame, const b3HullData* shape) {
    const auto* points = b3GetHullPoints(shape);
    const auto* edges = b3GetHullEdges(shape);
    const auto* faces = b3GetHullFaces(shape);
    for (int face = 0; face < shape->faceCount && !sections.full; ++face) {
        std::array<glm::dvec3, 256> polygon;
        std::size_t count = 0;
        const auto start = faces[face].edge;
        auto edge = start;
        do {
            if (count == polygon.size()) { sections.result.truncated = true; break; }
            polygon[count++] = frame.world(vector(points[edges[edge].origin]));
            edge = edges[edge].next;
        } while (edge != start);
        sections.polygon(planeIndex, body, polygon.data(), count);
    }
}

b3AABB localBounds(const Plane& plane, const Frame& frame) {
    glm::dvec3 minimum(1e100), maximum(-1e100);
    for (const double x : {-1.0, 1.0}) for (const double y : {-1.0, 1.0}) {
        const auto point = frame.local(plane.center + double(BindingPlane::halfSize) * (plane.right * x + up * y));
        minimum = glm::min(minimum, point); maximum = glm::max(maximum, point);
    }
    return {vector(minimum - glm::dvec3(1e-4)), vector(maximum + glm::dvec3(1e-4))};
}

void mesh(Sections& sections, std::size_t planeIndex, b3BodyId body, const Frame& frame, b3ShapeId shape, bool heightField) {
    struct Query {
        std::vector<std::array<b3Vec3, 3>> triangles;
        std::size_t remaining;
        bool truncated = false;
    } query{{}, maxTriangles - sections.triangles};
    query.triangles.reserve(std::min<std::size_t>(query.remaining, 256));
    const auto callback = [](b3Vec3 a, b3Vec3 b, b3Vec3 c, int, void* context) {
        auto& query = *static_cast<Query*>(context);
        if (query.triangles.size() == query.remaining) { query.truncated = true; return false; }
        query.triangles.push_back({a, b, c});
        return true;
    };
    const auto bounds = localBounds(sections.planes[planeIndex], frame);
    if (heightField) b3QueryHeightField(b3Shape_GetHeightField(shape), bounds, callback, &query);
    else {
        const auto data = b3Shape_GetMesh(shape);
        b3QueryMesh(&data, bounds, callback, &query);
    }
    sections.triangles += query.triangles.size();
    for (const auto& triangle : query.triangles) {
        if (sections.full) break;
        const std::array<glm::dvec3, 3> points{frame.world(vector(triangle[0])), frame.world(vector(triangle[1])), frame.world(vector(triangle[2]))};
        sections.polygon(planeIndex, body, points.data(), points.size(), b3StoreShapeId(shape));
    }
    if (query.truncated) sections.result.truncated = sections.full = true;
}
}

std::array<BindingPlane, 5> bindingPlanes(glm::vec3 center, glm::vec3 horizontalNormal) {
    if (!finite(center) || !finite(horizontalNormal)) throw std::invalid_argument("Binding plane center and normal must be finite");
    horizontalNormal.y = 0;
    if (glm::length(horizontalNormal) < 1e-6f) horizontalNormal = {0, 0, -1};
    horizontalNormal = glm::normalize(horizontalNormal);
    const auto right = glm::cross(glm::vec3(0, 1, 0), horizontalNormal);
    std::array<BindingPlane, 5> result;
    for (std::size_t index = 0; index < result.size(); ++index)
        result[index] = {center + horizontalNormal * ((static_cast<float>(index) - 2) * .01f), right};
    return result;
}

BindingGeometry queryBindingGeometry(std::shared_ptr<PhysicsWorld> world,
    const std::array<BindingPlane, 5>& planes, b3BodyId excluded) {
    Sections sections(planes);
    if (!world) return sections.result;
    glm::dvec3 minimum(1e100), maximum(-1e100);
    for (const auto& plane : sections.planes)
        for (const double x : {-1.0, 1.0}) for (const double y : {-1.0, 1.0}) {
            const auto point = plane.center + double(BindingPlane::halfSize) * (plane.right * x + up * y);
            minimum = glm::min(minimum, point); maximum = glm::max(maximum, point);
        }
    struct Candidates { std::vector<b3ShapeId> shapes; b3BodyId excluded; bool full = false; } candidates{{}, excluded};
    auto filter = b3DefaultQueryFilter();
    filter.maskBits &= ~ragdollFloorCategory;
    const b3AABB bounds{vector(minimum - glm::dvec3(.001)), vector(maximum + glm::dvec3(.001))};
    b3World_OverlapAABB(world->id(), bounds, filter, [](b3ShapeId shape, void* context) {
        auto& candidates = *static_cast<Candidates*>(context);
        if (B3_ID_EQUALS(b3Shape_GetBody(shape), candidates.excluded) || b3Shape_IsSensor(shape)) return true;
        if (candidates.shapes.size() == maxShapes) { candidates.full = true; return false; }
        candidates.shapes.push_back(shape);
        return true;
    }, &candidates);
    sections.result.truncated = candidates.full;
    std::sort(candidates.shapes.begin(), candidates.shapes.end(), [](b3ShapeId a, b3ShapeId b) { return b3StoreShapeId(a) < b3StoreShapeId(b); });
    for (const auto shape : candidates.shapes) {
        if (sections.full) break;
        const auto body = b3Shape_GetBody(shape);
        const auto frame = bodyFrame(body);
        for (std::size_t index = 0; index < planes.size() && !sections.full; ++index) {
            switch (b3Shape_GetType(shape)) {
            case b3_hullShape: hull(sections, index, body, frame, b3Shape_GetHull(shape)); break;
            case b3_meshShape: mesh(sections, index, body, frame, shape, false); break;
            case b3_heightShape: mesh(sections, index, body, frame, shape, true); break;
            case b3_sphereShape: {
                const auto data = b3Shape_GetSphere(shape);
                sphere(sections, index, body, frame.world(vector(data.center)), data.radius);
                break;
            }
            case b3_capsuleShape: {
                const auto data = b3Shape_GetCapsule(shape);
                capsule(sections, index, body, frame.world(vector(data.center1)), frame.world(vector(data.center2)), data.radius);
                break;
            }
            default: sections.result.truncated = true; break; // Baked compounds have no public shape-data accessor.
            }
        }
    }
    sections.result.segments.reserve(sections.segments.size());
    for (const auto& [key, value] : sections.segments) { (void)key; sections.result.segments.push_back(value.segment); }
    return sections.result;
}
}
