#include "binding_geometry.hpp"
#include "physics_world.hpp"
#include <box3d/box3d.h>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
template<class V> glm::vec3 vector(V p) { return {float(p.x), float(p.y), float(p.z)}; }
b3Vec3 vector(glm::vec3 p) { return {p.x, p.y, p.z}; }

struct Fixture {
    std::shared_ptr<viewer::PhysicsWorld> world = std::make_shared<viewer::PhysicsWorld>();
    std::vector<std::pair<b3BodyId, b3MeshData*>> meshes;
    ~Fixture() {
        for (const auto& [body, mesh] : meshes) { b3DestroyBody(body); b3DestroyMesh(mesh); }
    }
    b3BodyId body(glm::vec3 center = {}, glm::quat rotation = {1, 0, 0, 0}, b3BodyType type = b3_staticBody) {
        auto definition = b3DefaultBodyDef();
        definition.position = {center.x, center.y, center.z};
        definition.rotation = {{rotation.x, rotation.y, rotation.z}, rotation.w};
        definition.type = type;
        const auto result = b3CreateBody(world->id(), &definition);
        require(b3Body_IsValid(result), "Could not create binding geometry fixture body");
        return result;
    }
    void box(b3BodyId body, glm::vec3 half, glm::vec3 localCenter = {}) {
        const auto hull = b3MakeOffsetBoxHull(half.x, half.y, half.z, vector(localCenter));
        auto definition = b3DefaultShapeDef();
        require(b3Shape_IsValid(b3CreateHullShape(body, &definition, &hull.base)), "Could not create fixture hull");
    }
    void sphere(b3BodyId body, float radius, bool sensor = false) {
        const b3Sphere sphere{{0, 0, 0}, radius};
        auto definition = b3DefaultShapeDef(); definition.isSensor = sensor;
        require(b3Shape_IsValid(b3CreateSphereShape(body, &definition, &sphere)), "Could not create fixture sphere");
    }
    b3BodyId mesh(std::vector<b3Vec3> vertices, std::vector<std::int32_t> indices,
                  glm::vec3 center = {}, glm::quat rotation = {1, 0, 0, 0}, glm::vec3 scale = {1, 1, 1}) {
        b3MeshDef definition{};
        definition.vertices = vertices.data(); definition.indices = indices.data();
        definition.vertexCount = static_cast<int>(vertices.size()); definition.triangleCount = static_cast<int>(indices.size() / 3);
        auto* data = b3CreateMesh(&definition, nullptr, 0);
        require(data, "Could not create fixture triangle mesh");
        const auto result = body(center, rotation);
        meshes.emplace_back(result, data);
        const auto shape = b3DefaultShapeDef();
        require(b3Shape_IsValid(b3CreateMeshShape(result, &shape, data, vector(scale))), "Could not create fixture mesh shape");
        return result;
    }
};

void withinPlanes(const viewer::BindingGeometry& geometry, const std::array<viewer::BindingPlane, 5>& planes) {
    for (const auto& segment : geometry.segments) {
        require(segment.plane < planes.size(), "Section must retain its source plane index");
        const auto& plane = planes[segment.plane];
        const auto normal = glm::cross(plane.right, glm::vec3(0, 1, 0));
        for (const auto point : {segment.a, segment.b}) {
            const auto delta = point - plane.center;
            require(std::abs(glm::dot(delta, normal)) < 2e-6f, "Every section point must lie on its cutting plane");
            require(std::abs(glm::dot(delta, plane.right)) <= .150002f && std::abs(delta.y) <= .150002f,
                "Sections must be clipped to the 30 by 30 centimetre rectangles");
        }
        require(glm::distance(segment.a, segment.b) > 1e-7f, "Section output must not contain isolated or duplicate points");
    }
}

void fiveParallelRectangles() {
    const glm::vec3 center(1, 2, 3), direction(3, 7, -4);
    const auto normal = glm::normalize(glm::vec3(direction.x, 0, direction.z));
    const auto planes = viewer::bindingPlanes(center, direction);
    for (std::size_t index = 0; index < planes.size(); ++index) {
        require(glm::distance(planes[index].center, center + normal * ((static_cast<float>(index) - 2) * .01f)) < 1e-6f,
            "The five binding planes must be spaced one centimetre along the horizontal camera normal");
        require(glm::distance(glm::cross(planes[index].right, glm::vec3(0, 1, 0)), normal) < 1e-6f,
            "The rectangle right axis must produce the requested normal");
        require(std::abs(glm::length(planes[index].right) - 1) < 1e-6f && planes[index].right.y == 0,
            "Binding rectangles must have a unit horizontal right axis");
    }
    const auto fallback = viewer::bindingPlanes({}, {0, 1, 0});
    require(glm::distance(glm::cross(fallback[2].right, glm::vec3(0, 1, 0)), glm::vec3(0, 0, -1)) < 1e-6f,
        "A vertical view direction must retain a stable horizontal fallback");
    bool rejected = false;
    try { viewer::bindingPlanes({}, {std::numeric_limits<float>::quiet_NaN(), 0, 0}); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "Nonfinite geometry input must not reach Box3D spatial queries");
}

void rotatedHullSurfaceAndReadOnlyQuery() {
    Fixture fixture;
    const auto rotation = glm::angleAxis(.61f, glm::normalize(glm::vec3(1, 2, .3f)));
    const glm::vec3 center(.02f, .01f, 0), half(.09f, .055f, .035f);
    const auto body = fixture.body(center, rotation, b3_dynamicBody);
    fixture.box(body, half);
    b3Body_SetLinearVelocity(body, {.3f, .4f, .5f});
    const auto before = b3World_GetCounters(fixture.world->id());
    const auto pose = b3Body_GetTransform(body);
    const auto velocity = b3Body_GetLinearVelocity(body);
    const auto planes = viewer::bindingPlanes({}, {0, 0, 1});
    const auto result = viewer::queryBindingGeometry(fixture.world, planes);
    require(!result.truncated && !result.segments.empty(), "A rotated convex hull must produce surface sections");
    withinPlanes(result, planes);
    std::set<std::uint8_t> hitPlanes;
    for (const auto& segment : result.segments) {
        require(B3_ID_EQUALS(segment.body, body), "The source body must accompany every surface section");
        hitPlanes.insert(segment.plane);
        for (const auto point : {segment.a, segment.b, (segment.a + segment.b) * .5f}) {
            const auto local = glm::abs(glm::conjugate(rotation) * (point - center)) / half;
            const float boundary = std::max(local.x, std::max(local.y, local.z));
            require(std::abs(boundary - 1) < 3e-5f,
                "Rotated hull sections must follow actual faces rather than the body's axis-aligned envelope");
        }
    }
    require(hitPlanes.size() == 5, "A crossing hull must be sampled independently by all five planes");
    const auto after = b3World_GetCounters(fixture.world->id());
    const auto afterPose = b3Body_GetTransform(body);
    require(before.bodyCount == after.bodyCount && before.shapeCount == after.shapeCount && before.jointCount == after.jointCount &&
            glm::distance(vector(pose.p), vector(afterPose.p)) == 0 &&
            glm::distance(vector(velocity), vector(b3Body_GetLinearVelocity(body))) == 0,
        "Section queries must not move physics bodies or allocate constraints");
    require(viewer::queryBindingGeometry(fixture.world, planes, body).segments.empty(),
        "The held binding tool's excluded body must not contribute its own sections");
}

void finiteClippingAndCompoundGaps() {
    const auto planes = viewer::bindingPlanes({}, {0, 0, 1});
    {
        Fixture fixture;
        fixture.box(fixture.body(), {1, .05f, .1f});
        const auto result = viewer::queryBindingGeometry(fixture.world, planes);
        require(result.segments.size() == 10 && !result.truncated,
            "A long thin box must expose only its two crossing faces per rectangle");
        withinPlanes(result, planes);
        for (const auto& segment : result.segments)
            require(std::abs(std::abs(segment.a.y) - .05f) < 1e-6f && glm::distance(segment.a, segment.b) > .29999f,
                "Actual surface lines must clip exactly at the finite rectangle edges");
    }
    {
        Fixture fixture;
        fixture.box(fixture.body(), {1, 1, 1});
        require(viewer::queryBindingGeometry(fixture.world, planes).segments.empty(),
            "A rectangle completely inside a large solid must not invent a rectangle or AABB outline");
    }
    {
        Fixture fixture;
        const auto body = fixture.body();
        fixture.box(body, {.04f, .07f, .05f}, {-.24f, 0, 0});
        fixture.box(body, {.04f, .07f, .05f}, {.24f, 0, 0});
        require(viewer::queryBindingGeometry(fixture.world, planes).segments.empty(),
            "An empty gap between compound parts must remain empty despite the encompassing body bounds");
        const auto shifted = viewer::bindingPlanes({-.24f, 0, 0}, {0, 0, 1});
        const auto result = viewer::queryBindingGeometry(fixture.world, shifted);
        require(result.segments.size() == 20, "A runtime compound part must be sliced as its own real hull");
        withinPlanes(result, shifted);
    }
}

void staticTrianglesAndCoplanarEdges() {
    const auto planes = viewer::bindingPlanes({}, {0, 0, 1});
    {
        Fixture fixture;
        const auto body = fixture.mesh({{-1, 0, -.1f}, {1, 0, -.1f}, {0, 0, .1f}}, {0, 1, 2}, {}, {1, 0, 0, 0}, {-1, 1, 1});
        const auto result = viewer::queryBindingGeometry(fixture.world, planes);
        require(result.segments.size() == 5, "Static triangle geometry must contribute one exact crossing line per plane");
        withinPlanes(result, planes);
        for (const auto& segment : result.segments)
            require(B3_ID_EQUALS(segment.body, body) && segment.a.y == 0 && segment.b.y == 0 && glm::distance(segment.a, segment.b) > .29999f,
                "Reflected mesh triangles must retain their static body and clip to the rectangle");
    }
    {
        Fixture fixture;
        fixture.mesh({{-.1f, -.1f, 0}, {.1f, -.1f, 0}, {.1f, .1f, 0}, {-.1f, .1f, 0}}, {0, 1, 2, 0, 2, 3});
        const auto result = viewer::queryBindingGeometry(fixture.world, planes);
        require(result.segments.size() == 4, "Coplanar mesh triangles must suppress their shared interior diagonal");
        for (const auto& segment : result.segments)
            require(segment.plane == 2 && std::abs(glm::distance(segment.a, segment.b) - .2f) < 1e-6f,
                "A coplanar square must retain only its four real outside edges");
    }
}

void curvedSectionsAndBoundedOutput() {
    const auto planes = viewer::bindingPlanes({}, {0, 0, 1});
    {
        Fixture fixture;
        const auto body = fixture.body(); fixture.sphere(body, .1f);
        const auto result = viewer::queryBindingGeometry(fixture.world, planes);
        require(!result.truncated && result.segments.size() >= 300, "Spheres must produce curved surface sections across all five planes");
        withinPlanes(result, planes);
        for (const auto& segment : result.segments)
            for (const auto point : {segment.a, segment.b})
                require(std::abs(glm::length(point) - .1f) < 2e-6f,
                    "Sphere section vertices must lie on the sphere rather than a bounding box");
    }
    for (const glm::vec3 axis : {glm::vec3(0, 1, 0), glm::normalize(glm::vec3(.3f, 1, .8f))}) {
        Fixture fixture;
        const auto body = fixture.body();
        const auto a = axis * -.06f, b = axis * .06f;
        const b3Capsule capsule{vector(a), vector(b), .04f};
        const auto shape = b3DefaultShapeDef();
        b3CreateCapsuleShape(body, &shape, &capsule);
        const auto result = viewer::queryBindingGeometry(fixture.world, planes);
        require(!result.truncated && !result.segments.empty(), "Parallel and oblique capsules must both produce real surface sections");
        withinPlanes(result, planes);
        for (const auto& segment : result.segments)
            for (const auto point : {segment.a, segment.b}) {
                const auto closest = a + axis * std::clamp(glm::dot(point - a, axis), 0.f, .12f);
                require(std::abs(glm::distance(point, closest) - .04f) < 3e-6f,
                    "Capsule section vertices must stay on the cylindrical shaft or its outward hemispheres");
            }
    }
    {
        Fixture fixture;
        for (int index = 0; index < 5; ++index) fixture.sphere(fixture.body(), .1f);
        const auto result = viewer::queryBindingGeometry(fixture.world, planes);
        require(result.truncated && result.segments.size() == viewer::BindingGeometry::maxSegments,
            "Dense intersecting geometry must stop at the shared 1024 segment output budget");
        withinPlanes(result, planes);
    }
    {
        Fixture fixture;
        fixture.sphere(fixture.body(), .1f, true);
        require(viewer::queryBindingGeometry(fixture.world, planes).segments.empty(), "Nonphysical sensor volumes must not draw binding intersections");
    }
}
}

int main() {
    try {
        fiveParallelRectangles(); rotatedHullSurfaceAndReadOnlyQuery(); finiteClippingAndCompoundGaps();
        staticTrianglesAndCoplanarEdges(); curvedSectionsAndBoundedOutput();
        std::cout << "Binding geometry tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
