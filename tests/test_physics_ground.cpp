#include "collision_world.hpp"
#include "physics_scene.hpp"
#include "physics_world.hpp"
#include "zombie_ragdoll.hpp"

#include <box3d/box3d.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr float stepSeconds = 1.f / 60;
constexpr std::size_t part(viewer::RagdollPart value) { return std::size_t(value); }

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void quad(viewer::Model& model, glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d,
          const std::string& name) {
    viewer::Primitive primitive;
    for (const auto point : {a, b, c, d}) {
        viewer::Vertex vertex;
        vertex.position = point;
        vertex.normal = glm::normalize(glm::cross(b - a, c - a));
        primitive.vertices.push_back(vertex);
    }
    primitive.indices = {0, 1, 2, 0, 2, 3};
    model.draws.push_back({model.primitives.size(), glm::mat4(1), name});
    model.primitives.push_back(std::move(primitive));
}

void updateBounds(viewer::Model& model) {
    model.boundsMin = glm::vec3(std::numeric_limits<float>::max());
    model.boundsMax = -model.boundsMin;
    for (const auto& primitive : model.primitives) for (const auto& vertex : primitive.vertices) {
        model.boundsMin = glm::min(model.boundsMin, vertex.position);
        model.boundsMax = glm::max(model.boundsMax, vertex.position);
    }
}

viewer::Model gridGround(float slope = 0) {
    viewer::Model model;
    viewer::Primitive primitive;
    // Individual triangles remain local to the next section, unlike a single
    // town-wide quad that could conceal missing preparation around a loose limb.
    constexpr int cells = 50;
    constexpr float spacing = 10;
    for (int z = 0; z <= cells; ++z) for (int x = 0; x <= cells; ++x) {
        viewer::Vertex vertex;
        const float east = x * spacing - 250;
        vertex.position = {east, east * slope, z * spacing - 250};
        vertex.normal = glm::normalize(glm::vec3(-slope, 1, 0));
        primitive.vertices.push_back(vertex);
    }
    for (int z = 0; z < cells; ++z) for (int x = 0; x < cells; ++x) {
        const std::uint32_t a = z * (cells + 1) + x, b = a + 1;
        const std::uint32_t c = a + cells + 1, d = c + 1;
        primitive.indices.insert(primitive.indices.end(), {a, c, b, b, c, d});
    }
    model.primitives.push_back(std::move(primitive));
    model.draws.push_back({0, glm::mat4(1), "terrain_grid"});
    updateBounds(model);
    return model;
}

b3BodyId sphere(viewer::PhysicsWorld& physics, glm::vec3 center, float radius = .25f) {
    auto body = b3DefaultBodyDef();
    body.type = b3_dynamicBody;
    body.position = {center.x, center.y, center.z};
    const auto id = b3CreateBody(physics.id(), &body);
    auto shape = b3DefaultShapeDef();
    shape.filter.categoryBits = viewer::interiorBoxCategory;
    const b3Sphere geometry{{0, 0, 0}, radius};
    b3CreateSphereShape(id, &shape, &geometry);
    return id;
}

b3BodyId uprightCapsule(viewer::PhysicsWorld& physics, glm::vec3 center) {
    auto body = b3DefaultBodyDef();
    body.type = b3_dynamicBody;
    body.position = {center.x, center.y, center.z};
    body.motionLocks.angularX = body.motionLocks.angularY = body.motionLocks.angularZ = true;
    const auto id = b3CreateBody(physics.id(), &body);
    auto shape = b3DefaultShapeDef();
    shape.filter.categoryBits = viewer::interiorBoxCategory;
    const b3Capsule geometry{{0, -.5f, 0}, {0, .5f, 0}, .25f};
    b3CreateCapsuleShape(id, &shape, &geometry);
    return id;
}

void removeFriction(b3BodyId body) {
    b3ShapeId shape;
    require(b3Body_GetShapes(body, &shape, 1) == 1, "Sliding fixture must own one shape");
    b3Shape_SetFriction(shape, 0);
}

void requireHeight(b3BodyId body, float expected, int frame, const char* description) {
    const auto position = b3Body_GetPosition(body);
    if (std::abs(position.y - expected) < .04f) return;
    const auto velocity = b3Body_GetLinearVelocity(body);
    std::ostringstream message;
    message << description << " at frame " << frame << ": position "
            << position.x << ',' << position.y << ',' << position.z << ", velocity "
            << velocity.x << ',' << velocity.y << ',' << velocity.z;
    throw std::runtime_error(message.str());
}

std::size_t categoryShapes(viewer::PhysicsWorld& physics, std::uint64_t category) {
    std::size_t count = 0;
    auto filter = b3DefaultQueryFilter();
    filter.maskBits = category;
    b3World_OverlapAABB(physics.id(), {{-1000, -1000, -1000}, {1000, 1000, 1000}}, filter,
        [](b3ShapeId, void* context) { ++*static_cast<std::size_t*>(context); return true; }, &count);
    return count;
}

void prewarmingSelectsTerrainRatherThanOtherSoftGeometry() {
    viewer::Model model;
    quad(model, {-25, 0, -25}, {-25, 0, 25}, {25, 0, 25}, {25, 0, -25}, "terrain");
    quad(model, {-4, 2, -4}, {-4, 2, 4}, {4, 2, 4}, {4, 2, -4}, "unclassified_platform");
    quad(model, {8, 0, -4}, {8, 4, -4}, {8, 4, 4}, {8, 0, 4}, "building_test_walls");
    updateBounds(model);
    viewer::CollisionWorld collision(model);
    const auto faces = collision.staticFaces(model.boundsMin, model.boundsMax, false);
    require(std::count_if(faces.begin(), faces.end(), [](const auto& face) { return face.terrain; }) == 2,
            "Ground classification must retain terrain separately from unclassified soft geometry");
    auto physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::PhysicsScene scene(physics, collision, model.boundsMin, model.boundsMax);
    // An airborne request must still prepare the ground below its X/Z region.
    scene.prewarmGround({1, 50, 1});
    const auto body = sphere(*physics, {1, 4, 1});
    for (int frame = 0; frame < 240; ++frame) physics->step(stepSeconds);
    const auto position = b3Body_GetPosition(body);
    require(std::abs(position.y - .25f) < .03f,
            "Ground-only prewarming must support the sphere on terrain, not an unrelated soft platform");
}

void overlappingSectionRequestsReuseSpanningTriangles() {
    viewer::Model model;
    quad(model, {-250, 0, -250}, {-250, 0, 250}, {250, 0, 250}, {250, 0, -250}, "terrain");
    updateBounds(model);
    viewer::CollisionWorld collision(model);
    auto physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::PhysicsScene scene(physics, collision, model.boundsMin, model.boundsMax);
    scene.ensureGroundNear({-20, -1, -20}, {20, 1, 20});
    const auto first = b3World_GetCounters(physics->id());
    for (const float x : {-175.f, -100.f, 0.f, 100.f, 175.f}) {
        scene.ensureGroundNear({x - 2, -1, -2}, {x + 2, 1, 2});
        scene.prewarmGround({x, 0, 0});
    }
    const auto repeated = b3World_GetCounters(physics->id());
    require(repeated.bodyCount == first.bodyCount && repeated.shapeCount == first.shapeCount,
            "A terrain triangle spanning several sections must have one shared collider copy");
    const auto body = sphere(*physics, {-100, 2, 0});
    for (int frame = 0; frame < 180; ++frame) physics->step(stepSeconds);
    require(std::abs(b3Body_GetPosition(body).y - .25f) < .03f,
            "A shared triangle must retain support on a negative-coordinate section seam");
}

void slidingAcrossDistinctSectionMeshesKeepsSupport() {
    auto model = gridGround();
    viewer::CollisionWorld collision(model);
    for (const bool capsule : {false, true}) for (const float direction : {-1.f, 1.f}) {
        auto physics = std::make_shared<viewer::PhysicsWorld>();
        viewer::PhysicsScene scene(physics, collision, model.boundsMin, model.boundsMax);
        const float x = direction > 0 ? 95.f : 115.f;
        const float height = capsule ? .75f : .25f;
        scene.ensureGroundNear({x - 1, -1, 4}, {x + 1, 1, 6});
        const auto body = capsule ? uprightCapsule(*physics, {x, height, 5}) : sphere(*physics, {x, height, 5});
        removeFriction(body);
        b3Body_SetLinearVelocity(body, {5 * direction, 0, 0});
        const auto initialBodies = b3World_GetCounters(physics->id()).bodyCount;
        for (int frame = 0; frame < 600; ++frame) {
            const auto p = b3Body_GetPosition(body);
            scene.ensureGroundNear({p.x - 2, -1, p.z - 1}, {p.x + 2, 1, p.z + 1});
            physics->step(stepSeconds);
            requireHeight(body, height, frame, "A sphere/capsule crossed distinct ground meshes without stable support");
        }
        const auto end = b3Body_GetPosition(body);
        require(direction * (end.x - x) > 45 && b3World_GetCounters(physics->id()).bodyCount > initialBodies,
                "Sliding shapes must cross a boundary between separately constructed meshes in either direction");
    }
}

void restingAtSplitEdgesAndVerticesKeepsSupport() {
    auto model = gridGround();
    viewer::CollisionWorld collision(model);
    for (const float z : {5.f, 10.f}) {
        auto physics = std::make_shared<viewer::PhysicsWorld>();
        viewer::PhysicsScene scene(physics, collision, model.boundsMin, model.boundsMax);
        scene.ensureGroundNear({94, -1, 4}, {96, 1, 6});
        scene.ensureGroundNear({114, -1, 4}, {116, 1, 6});
        // The first section retains full triangles crossing x=100, so the
        // actual shared mesh boundary lies on x=110 for this 10 m grid.
        const auto body = sphere(*physics, {110, .25f, z});
        for (int frame = 0; frame < 240; ++frame) {
            physics->step(stepSeconds);
            requireHeight(body, .25f, frame, "A sphere resting on a split edge/vertex lost support");
            const auto p = b3Body_GetPosition(body);
            require(std::abs(p.x - 110) < .03f && std::abs(p.z - z) < .03f,
                    "A resting sphere must not receive a sideways impulse at a flat mesh boundary");
        }
    }
}

void crossingSplitGridVerticesKeepsSupport() {
    auto model = gridGround();
    viewer::CollisionWorld collision(model);
    auto physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::PhysicsScene scene(physics, collision, model.boundsMin, model.boundsMax);
    scene.ensureGroundNear({94, -1, 94}, {96, 1, 96});
    const auto body = sphere(*physics, {95, .25f, 95});
    removeFriction(body);
    b3Body_SetLinearVelocity(body, {3, 0, 3});
    for (int frame = 0; frame < 540; ++frame) {
        const auto p = b3Body_GetPosition(body);
        scene.ensureGroundNear({p.x - 1, -1, p.z - 1}, {p.x + 2, 1, p.z + 2});
        physics->step(stepSeconds);
        requireHeight(body, .25f, frame, "A sphere crossing a split grid vertex lost support");
    }
    const auto p = b3Body_GetPosition(body);
    require(p.x > 120 && p.z > 120, "The diagonal slider must travel across the split vertex");
}

void crossingSmoothVerticesNearTrueBoundaryKeepsSupport() {
    auto model = gridGround();
    viewer::CollisionWorld collision(model);
    auto physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::PhysicsScene scene(physics, collision, model.boundsMin, model.boundsMax);
    scene.ensureGroundNear({94, -1, 244}, {96, 1, 246});
    scene.ensureGroundNear({114, -1, 234}, {116, 1, 236});
    // Vertex (110,240) is smooth, but triangles meeting it also contain real
    // boundary vertices on z=250. Smoothness must be associated with the
    // contacted vertex rather than rejected for the entire triangle.
    const auto body = sphere(*physics, {105, .25f, 245});
    removeFriction(body);
    b3Body_SetLinearVelocity(body, {5, 0, -5});
    for (int frame = 0; frame < 240; ++frame) {
        physics->step(stepSeconds);
        requireHeight(body, .25f, frame,
                      "A sphere crossing a smooth vertex beside a real boundary lost support");
    }
    const auto p = b3Body_GetPosition(body);
    require(p.x > 120 && p.z < 230, "The mixed-boundary fixture must cross its smooth vertex");
}

void crossingSmoothCornerWithBoundaryNeighborsKeepsSupport() {
    auto model = gridGround();
    auto& primitive = model.primitives[0];
    std::vector<std::uint32_t> bounded;
    for (std::size_t i = 0; i < primitive.indices.size(); i += 3) {
        bool inside = true;
        for (std::size_t j = 0; j < 3; ++j) {
            const auto p = primitive.vertices[primitive.indices[i + j]].position;
            inside = inside && p.x <= 120 && p.z <= 120;
        }
        if (inside) bounded.insert(bounded.end(), primitive.indices.begin() + i, primitive.indices.begin() + i + 3);
    }
    primitive.indices = std::move(bounded);
    model.boundsMax.x = model.boundsMax.z = 120;
    viewer::CollisionWorld collision(model);
    auto physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::PhysicsScene scene(physics, collision, model.boundsMin, model.boundsMax);
    scene.ensureGroundNear({94, -1, 94}, {96, 1, 96});
    scene.ensureGroundNear({114, -1, 94}, {116, 1, 96});
    scene.ensureGroundNear({94, -1, 114}, {96, 1, 116});
    scene.ensureGroundNear({114, -1, 114}, {116, 1, 116});
    // The remaining northeast mesh has a smooth source corner at (110,110),
    // but its other vertices lie on the genuine x/z=120 outer boundaries.
    const auto body = sphere(*physics, {105, .25f, 105});
    removeFriction(body);
    b3Body_SetLinearVelocity(body, {5, 0, 5});
    for (int frame = 0; frame < 144; ++frame) {
        physics->step(stepSeconds);
        requireHeight(body, .25f, frame,
                      "A sphere crossing a smooth corner with true-boundary neighbor vertices lost support");
    }
    const auto p = b3Body_GetPosition(body);
    require(p.x > 115 && p.z > 115, "The bounded corner fixture must cross its smooth vertex before the true edge");
}

void crossingSlopedSplitMeshesKeepsTheSurface() {
    constexpr float slope = .03f;
    auto model = gridGround(slope);
    viewer::CollisionWorld collision(model);
    auto physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::PhysicsScene scene(physics, collision, model.boundsMin, model.boundsMax);
    scene.ensureGroundNear({94, -20, 4}, {96, 20, 6});
    const auto body = sphere(*physics, {95, slope * 95 + .25f * std::sqrt(1 + slope * slope), 5});
    removeFriction(body);
    b3Body_SetLinearVelocity(body, {5, 5 * slope, 0});
    for (int frame = 0; frame < 360; ++frame) {
        const auto p = b3Body_GetPosition(body);
        scene.ensureGroundNear({p.x - 1, -20, p.z - 1}, {p.x + 2, 20, p.z + 1});
        physics->step(stepSeconds);
        const auto now = b3Body_GetPosition(body);
        const float distance = (now.y - slope * now.x) / std::sqrt(1 + slope * slope);
        require(std::abs(distance - .25f) < .04f,
                "A sloped section seam must retain surface-normal support instead of a flat height correction");
    }
    require(b3Body_GetPosition(body).x > 115, "The uphill sphere must actually pass the split seam");
}

void realTerrainEdgesAndRidgesRemainPhysical() {
    auto model = gridGround();
    viewer::CollisionWorld collision(model);
    auto physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::PhysicsScene scene(physics, collision, model.boundsMin, model.boundsMax);
    scene.ensureGroundNear({244, -1, 4}, {246, 1, 6});
    const auto body = sphere(*physics, {245, .25f, 5});
    removeFriction(body);
    b3Body_SetLinearVelocity(body, {5, 0, 0});
    for (int frame = 0; frame < 240; ++frame) physics->step(stepSeconds);
    const auto beyond = b3Body_GetPosition(body);
    require(beyond.x > 255 && beyond.y < -2,
            "A genuine outer terrain edge must let the body fall instead of extending an imaginary support plane");

    auto ridge = gridGround();
    for (auto& vertex : ridge.primitives[0].vertices)
        vertex.position.y = 5 - 2 * std::abs(vertex.position.x - 100);
    updateBounds(ridge);
    viewer::CollisionWorld ridgeCollision(ridge);
    auto ridgePhysics = std::make_shared<viewer::PhysicsWorld>();
    viewer::PhysicsScene ridgeScene(ridgePhysics, ridgeCollision, ridge.boundsMin, ridge.boundsMax);
    ridgeScene.ensureGroundNear({94, -500, 4}, {96, 10, 6});
    ridgeScene.ensureGroundNear({114, -500, 4}, {116, 10, 6});
    const auto onRidge = sphere(*ridgePhysics, {100, 5.25f, 5});
    b3MotionLocks locks{};
    locks.linearX = locks.linearZ = true;
    b3Body_SetMotionLocks(onRidge, locks);
    for (int frame = 0; frame < 240; ++frame) {
        ridgePhysics->step(stepSeconds);
        requireHeight(onRidge, 5.25f, frame,
                      "A genuine sharp ridge must keep edge contact rather than project onto extended facet planes");
    }
}

void meshEdgeFlagUpdatesValidateAndRefreshTheirHash() {
    b3Vec3 vertices[]{{0, 0, 0}, {0, 0, 1}, {1, 0, 1}, {1, 0, 0}};
    int32_t indices[]{0, 1, 2, 0, 2, 3};
    b3MeshDef definition{};
    definition.vertices = vertices;
    definition.indices = indices;
    definition.vertexCount = 4;
    definition.triangleCount = 2;
    std::unique_ptr<b3MeshData, decltype(&b3DestroyMesh)> mesh(b3CreateMesh(&definition, nullptr, 0), b3DestroyMesh);
    require(bool(mesh), "The flag-update fixture mesh must build");
    const auto originalHash = mesh->hash;
    std::vector<uint8_t> original(b3GetMeshFlags(mesh.get()), b3GetMeshFlags(mesh.get()) + mesh->triangleCount);
    auto flags = original;
    require(!b3SetMeshEdgeFlags(mesh.get(), flags.data(), mesh->triangleCount + 1),
            "The edge-flag API must reject a mismatched triangle count");
    flags[0] = 0x80;
    require(!b3SetMeshEdgeFlags(mesh.get(), flags.data(), mesh->triangleCount) && mesh->hash == originalHash &&
                std::equal(original.begin(), original.end(), b3GetMeshFlags(mesh.get())),
            "Rejected edge flags must preserve both the owned data and geometry hash");
    flags = original;
    flags[0] = b3_flatEdge1;
    require(b3SetMeshEdgeFlags(mesh.get(), flags.data(), mesh->triangleCount) && mesh->hash != originalHash,
            "A valid adjacency update must refresh the geometry hash used by cached collider data");
    flags[0] = 0;
    require(b3GetMeshFlags(mesh.get())[0] == b3_flatEdge1,
            "The mesh must own the updated edge flags independently of the caller's input buffer");
}

void preparedSlopesMatchTheirRenderedTriangles() {
    constexpr float slope = .08f;
    auto model = gridGround(slope);
    viewer::CollisionWorld collision(model);
    auto physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::PhysicsScene scene(physics, collision, model.boundsMin, model.boundsMax);
    scene.ensureGroundNear({-105, -30, -3}, {-95, 30, 3});
    const auto body = sphere(*physics, {-100, -5, 0});
    for (int frame = 0; frame < 180; ++frame) physics->step(stepSeconds);
    const auto position = b3Body_GetPosition(body);
    const float normalDistance = (position.y - slope * position.x) / std::sqrt(1 + slope * slope);
    require(std::abs(normalDistance - .25f) < .03f,
            "Prepared terrain must retain the actual slope rather than a flat section-height approximation");
}

void sceneRagdollsUseGroundAcrossSectionsAndAfterTranslation() {
    auto model = gridGround();
    viewer::CollisionWorld collision(model);
    auto physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::PhysicsScene scene(physics, collision, model.boundsMin, model.boundsMax);
    viewer::ZombieRagdollWorld ragdolls(physics);
    const auto handle = ragdolls.create({95, 3, 0}, 0, {});
    require(categoryShapes(*physics, viewer::ragdollFloorCategory) == 1,
            "Standalone ragdolls must retain their fallback until scene support is supplied");
    ragdolls.prepareScenePhysics(handle, scene);
    require(categoryShapes(*physics, viewer::ragdollFloorCategory) == 0,
            "Scene-supported ragdolls must remove their private floor rather than duplicate terrain contacts");
    for (int frame = 0; frame < 3600 && !ragdolls.sleeping(handle); ++frame) {
        ragdolls.prepareScenePhysics(handle, scene);
        physics->step(stepSeconds);
    }
    require(ragdolls.bodyTransforms(handle)[part(viewer::RagdollPart::pelvis)][3].y < 1,
            "A corpse created above terrain must land on the actual ground instead of its death-height floor");
    require(ragdolls.sleeping(handle), "A scene-supported isolated corpse must be able to sleep naturally");
    const auto settledRevision = ragdolls.poseRevision(handle);
    scene.prewarmGround({-150, 0, -150});
    for (int frame = 0; frame < 30; ++frame) {
        ragdolls.prepareScenePhysics(handle, scene);
        physics->step(stepSeconds);
    }
    require(ragdolls.sleeping(handle) && ragdolls.poseRevision(handle) == settledRevision,
            "Preparing unrelated ground sections must not wake or dirty a settled corpse");

    const auto head = part(viewer::RagdollPart::head);
    const auto initialHead = ragdolls.bodyTransforms(handle)[head][3];
    ragdolls.sever(handle, viewer::RagdollPart::head);
    // Severed roots retain their existing drag. Give this throw enough range
    // to cross the section seam even after the corpse fell toward negative X.
    ragdolls.addLinearVelocity(handle, viewer::RagdollPart::head, {20, 6, 0});
    for (int frame = 0; frame < 300; ++frame) {
        ragdolls.prepareScenePhysics(handle, scene);
        physics->step(stepSeconds);
    }
    const auto detached = ragdolls.bodyTransforms(handle)[head][3];
    require(detached.x > 100.5f && detached.x > initialHead.x + 3 && detached.y > -.03f && detached.y < .5f,
            "A detached part crossing a section and the old local-floor edge must land on shared ground");

    ragdolls.translate(handle, {120, 2, 0});
    for (int frame = 0; frame < 300; ++frame) {
        ragdolls.prepareScenePhysics(handle, scene);
        physics->step(stepSeconds);
    }
    const auto translated = ragdolls.bodyTransforms(handle)[part(viewer::RagdollPart::pelvis)][3];
    require(translated.x > 200 && translated.y < 1 && !ragdolls.hasBodyBelow(handle, -.5f),
            "Translation without a private floor must prepare and use the destination section's actual ground");
}

void unrelatedInteriorChangesPreserveSleepingSupport() {
    auto model = gridGround();
    model.boundsMax.y = 10;
    viewer::CollisionWorld collision(model);
    viewer::Model interior;
    const auto addSupport = [](viewer::Model& target) {
        quad(target, {-4, 3, -4}, {-4, 3, 4}, {4, 3, 4}, {4, 3, -4}, "building_test_floor");
    };
    addSupport(interior);
    updateBounds(interior);
    collision.setRuntimeGeometry(interior);
    auto physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::PhysicsScene scene(physics, collision, model.boundsMin, model.boundsMax);
    scene.ensureNear({-5, 0, -5}, {5, 5, 5});
    const auto body = sphere(*physics, {0, 4, 0});
    for (int frame = 0; frame < 300; ++frame) physics->step(stepSeconds);
    require(!b3Body_IsAwake(body) && std::abs(b3Body_GetPosition(body).y - 3.25f) < .03f,
            "The interior fixture sphere must rest on the elevated runtime floor");

    std::vector<b3ShapeId> support;
    auto filter = b3DefaultQueryFilter();
    filter.maskBits = viewer::ordinaryBodyCategory;
    b3World_OverlapAABB(physics->id(), {{-5, 2.99f, -5}, {5, 3.01f, 5}}, filter,
        [](b3ShapeId shape, void* context) {
            static_cast<std::vector<b3ShapeId>*>(context)->push_back(shape);
            return true;
        }, &support);
    require(!support.empty(), "The runtime support shapes must be queryable");

    viewer::Model replacement;
    // Insert the unrelated floor first: runtime face indices can change even
    // though the supporting floor's geometry has remained identical.
    quad(replacement, {145, 5, -4}, {145, 5, 4}, {155, 5, 4}, {155, 5, -4}, "building_other_floor");
    addSupport(replacement);
    updateBounds(replacement);
    collision.setRuntimeGeometry(replacement);
    scene.ensureNear({-5, 0, -5}, {5, 5, 5});
    for (const auto shape : support)
        require(b3Shape_IsValid(shape), "An unchanged supporting runtime floor must retain its original physics shapes");
    require(!b3Body_IsAwake(body), "An unrelated streamed interior change must not wake a resting physics body");
    for (int frame = 0; frame < 60; ++frame) physics->step(stepSeconds);
    require(!b3Body_IsAwake(body) && std::abs(b3Body_GetPosition(body).y - 3.25f) < .03f,
            "Unchanged interior support must remain stable after runtime collision refresh");
}

void addRuntimeFloorGrid(viewer::Model& model, float left, float right) {
    constexpr float spacing = 4;
    constexpr int zCells = 4;
    const int xCells = int((right - left) / spacing);
    viewer::Primitive primitive;
    for (int z = 0; z <= zCells; ++z) for (int x = 0; x <= xCells; ++x) {
        viewer::Vertex vertex;
        vertex.position = {left + x * spacing, 3, z * spacing};
        vertex.normal = {0, 1, 0};
        primitive.vertices.push_back(vertex);
    }
    for (int z = 0; z < zCells; ++z) for (int x = 0; x < xCells; ++x) {
        const std::uint32_t a = z * (xCells + 1) + x, b = a + 1;
        const std::uint32_t c = a + xCells + 1, d = c + 1;
        primitive.indices.insert(primitive.indices.end(), {a, c, b, b, c, d});
    }
    model.draws.push_back({model.primitives.size(), glm::mat4(1), "building_runtime_floor"});
    model.primitives.push_back(std::move(primitive));
    updateBounds(model);
}

void splitRuntimeFloorsKeepSlidingAndRestingSupport() {
    auto model = gridGround();
    model.boundsMax.y = 10;
    viewer::CollisionWorld collision(model);
    viewer::Model interior;
    addRuntimeFloorGrid(interior, 0, 64);
    collision.setRuntimeGeometry(interior);
    for (const float direction : {-1.f, 1.f}) {
        auto physics = std::make_shared<viewer::PhysicsWorld>();
        viewer::PhysicsScene scene(physics, collision, model.boundsMin, model.boundsMax);
        scene.ensureNear({0, 0, 0}, {64, 5, 16});
        const float start = direction > 0 ? 4.f : 60.f;
        const auto body = sphere(*physics, {start, 3.25f, 6});
        removeFriction(body);
        b3Body_SetLinearVelocity(body, {5 * direction, 0, 0});
        for (int frame = 0; frame < 600; ++frame) {
            physics->step(stepSeconds);
            requireHeight(body, 3.25f, frame,
                          "A sliding sphere lost elevated support at a runtime-floor chunk seam");
        }
        require(direction * (b3Body_GetPosition(body).x - start) > 45,
                "The runtime-floor slider must pass several distinct 16 m chunks");
    }

    auto physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::PhysicsScene scene(physics, collision, model.boundsMin, model.boundsMax);
    scene.ensureNear({0, 0, 0}, {64, 5, 16});
    const auto body = sphere(*physics, {16, 3.25f, 6});
    for (int frame = 0; frame < 240; ++frame) {
        physics->step(stepSeconds);
        requireHeight(body, 3.25f, frame, "A sphere resting on a runtime-floor split lost elevated support");
        require(std::abs(b3Body_GetPosition(body).x - 16) < .03f,
                "A flat runtime-floor seam must not push a resting sphere sideways");
    }
}

void neighboringRuntimeFloorChangesRefreshAdjacency() {
    for (const bool adding : {true, false}) {
        auto model = gridGround();
        model.boundsMax.y = 10;
        viewer::CollisionWorld collision(model);
        viewer::Model interior;
        addRuntimeFloorGrid(interior, 0, 32);
        if (!adding) addRuntimeFloorGrid(interior, 32, 64);
        collision.setRuntimeGeometry(interior);
        auto physics = std::make_shared<viewer::PhysicsWorld>();
        viewer::PhysicsScene scene(physics, collision, model.boundsMin, model.boundsMax);
        scene.ensureNear({0, 0, 0}, {64, 5, 16});
        const auto body = sphere(*physics, {4, 3.25f, 6});
        removeFriction(body);
        b3Body_SetLinearVelocity(body, {5, 0, 0});
        for (int frame = 0; frame < 200; ++frame) {
            physics->step(stepSeconds);
            requireHeight(body, 3.25f, frame, "The sphere must reach the unchanged owner chunk before its neighbor changes");
        }

        viewer::Model replacement;
        addRuntimeFloorGrid(replacement, 0, 32);
        if (adding) addRuntimeFloorGrid(replacement, 32, 64);
        collision.setRuntimeGeometry(replacement);
        scene.ensureNear({0, 0, 0}, {64, 5, 16});
        for (int frame = 0; frame < 300; ++frame) {
            physics->step(stepSeconds);
            if (adding)
                requireHeight(body, 3.25f, frame,
                              "An added neighboring runtime floor must update the old chunk's shared-edge support");
        }
        const auto position = b3Body_GetPosition(body);
        require(position.x > 40, "The sphere must travel past the edited neighboring floor boundary");
        if (!adding)
            require(position.y < 1 && position.y > -.03f,
                    "Removing a runtime neighbor must restore a real ledge instead of retaining imaginary plane support");
    }
}
} // namespace

void staticFloorTileSeamsKeepSupport() {
    auto model = gridGround();
    model.draws[0].name = "static_platform_floor";
    for (auto& vertex : model.primitives[0].vertices) vertex.position.y = 3;
    updateBounds(model);
    viewer::CollisionWorld collision(model);
    auto physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::PhysicsScene scene(physics, collision, model.boundsMin, model.boundsMax);
    scene.ensureNear({94, 2, 4}, {96, 4, 6});
    const auto body = sphere(*physics, {95, 3.25f, 5});
    b3ShapeId shape;
    require(b3Body_GetShapes(body, &shape, 1) == 1, "Static floor fixture must own one sphere");
    b3Shape_SetFriction(shape, 0);
    b3Body_SetLinearVelocity(body, {5, 0, 0});
    for (int frame = 0; frame < 600; ++frame) {
        const auto p = b3Body_GetPosition(body);
        scene.ensureNear({p.x - 1, 2, p.z - 1}, {p.x + 2, 4, p.z + 1});
        physics->step(stepSeconds);
        require(std::abs(b3Body_GetPosition(body).y - 3.25f) < .04f,
                "Static building-floor tile seams must not launch or drop a sliding sphere");
    }
    require(b3Body_GetPosition(body).x > 140, "The static-floor sphere must cross tile seams");
}

int main() {
    try {
        prewarmingSelectsTerrainRatherThanOtherSoftGeometry();
        staticFloorTileSeamsKeepSupport();
        overlappingSectionRequestsReuseSpanningTriangles();
        slidingAcrossDistinctSectionMeshesKeepsSupport();
        restingAtSplitEdgesAndVerticesKeepsSupport();
        crossingSplitGridVerticesKeepsSupport();
        crossingSmoothVerticesNearTrueBoundaryKeepsSupport();
        crossingSmoothCornerWithBoundaryNeighborsKeepsSupport();
        crossingSlopedSplitMeshesKeepsTheSurface();
        realTerrainEdgesAndRidgesRemainPhysical();
        meshEdgeFlagUpdatesValidateAndRefreshTheirHash();
        preparedSlopesMatchTheirRenderedTriangles();
        sceneRagdollsUseGroundAcrossSectionsAndAfterTranslation();
        unrelatedInteriorChangesPreserveSleepingSupport();
        splitRuntimeFloorsKeepSlidingAndRestingSupport();
        neighboringRuntimeFloorChangesRefreshAdjacency();
        std::cout << "Ground physics tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << "Ground physics test failed: " << error.what() << '\n';
        return 1;
    }
}
