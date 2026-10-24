#include "vehicle_traction.hpp"
#include "physics_world.hpp"
#include <box3d/box3d.h>
#include <cmath>
#include <array>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void contacts(bool wheelFirst) {
    viewer::PhysicsWorld world;
    b3World_SetGravity(world.id(), {0, 0, 0});
    b3BodyId wheel{}, floor{};
    auto* cylinder = b3CreateCylinder(.24f, .36f, -.12f, 24);
    const auto makeWheel = [&] {
        auto body = b3DefaultBodyDef(); body.type = b3_dynamicBody;
        body.position = {0, .355f, 0}; body.rotation = b3ComputeQuatBetweenUnitVectors(b3Vec3_axisY, b3Vec3_axisX);
        wheel = b3CreateBody(world.id(), &body);
        auto shape = b3DefaultShapeDef(); b3CreateHullShape(wheel, &shape, cylinder);
    };
    const auto makeFloor = [&] {
        auto body = b3DefaultBodyDef(); body.type = b3_kinematicBody; body.position = {0, -.25f, 0};
        floor = b3CreateBody(world.id(), &body);
        auto shape = b3DefaultShapeDef(); const auto hull = b3MakeBoxHull(10, .25f, 10);
        b3CreateHullShape(floor, &shape, &hull.base);
    };
    if (wheelFirst) { makeWheel(); makeFloor(); } else { makeFloor(); makeWheel(); }
    world.step(1.f / 60);
    auto state = viewer::sampleWheelTraction(wheel, viewer::WheelMotion::Airborne);
    require(state.motion == viewer::WheelMotion::Rolling, "Supported stationary tires must use rolling grip");
    // Wheel moves at 5 m/s relative to a platform moving at 3 m/s.
    b3Body_SetLinearVelocity(floor, {0, 0, -3});
    b3Body_SetLinearVelocity(wheel, {0, 0, -8});
    b3Body_SetAngularVelocity(wheel, {-5.f / .36f, 0, 0});
    state = viewer::sampleWheelTraction(wheel, viewer::WheelMotion::Rolling);
    require(state.motion == viewer::WheelMotion::Rolling && std::abs(state.forwardSpeed - 5) < .01f &&
            std::abs(state.longitudinalSlip) < .15f && std::abs(state.lateralSlip) < .01f,
            "A rolling wheel must cancel contact velocity relative to moving ground");
    b3Body_SetAngularVelocity(wheel, {0, 0, 0});
    state = viewer::sampleWheelTraction(wheel, state.motion);
    require(state.motion == viewer::WheelMotion::Sliding && std::abs(state.longitudinalSlip) > 4.9f,
            "A locked wheel moving along the ground must be detected as sliding");
    const float skidFriction = state.friction;
    b3Body_SetAngularVelocity(wheel, {-5.f / .36f, 0, 0});
    b3Body_SetLinearVelocity(wheel, {3, 0, -8});
    state = viewer::sampleWheelTraction(wheel, viewer::WheelMotion::Rolling);
    require(state.motion == viewer::WheelMotion::Sliding && std::abs(state.lateralSlip) > 2.9f &&
            !state.longitudinalSliding && state.friction == viewer::rollingTireFriction &&
            state.lateralFriction == viewer::lateralTireFriction,
            "A sideways skid must retain independent forward rolling grip");
    b3Body_SetLinearVelocity(wheel, {0, 0, -8});
    state = viewer::sampleWheelTraction(wheel, state.motion);
    require(state.motion == viewer::WheelMotion::Rolling && state.friction > skidFriction && skidFriction > .85f,
            "Regaining traction must restore higher rolling friction; both grip levels must exceed the old tire friction");
    b3Body_SetTransform(wheel, {0, 10, 0}, b3Body_GetRotation(wheel));
    world.step(1.f / 60);
    state = viewer::sampleWheelTraction(wheel, viewer::WheelMotion::Sliding);
    require(state.motion == viewer::WheelMotion::Airborne && state.longitudinalSlip == 0 && state.lateralSlip == 0,
            "Airborne wheels must not retain a stale ground-slip classification");
    b3DestroyBody(wheel); b3DestroyHull(cylinder);
}
// A rotation-locked contact body measures the solver's actual drag independently
// of wheel motor/suspension behavior. Local X denotes the tire axle.
glm::vec3 drag(bool meshGround, bool triangleMaterial, bool overflow, float steering, glm::vec3 localVelocity,
               bool directional = true) {
    viewer::PhysicsWorld world;
    std::vector<b3BodyId> floors;
    b3MeshData* mesh = nullptr;
    auto floorShape = b3DefaultShapeDef(); floorShape.baseMaterial.friction = 1;
    auto material = b3DefaultSurfaceMaterial(); material.friction = .25f;
    if (triangleMaterial) { floorShape.materials = &material; floorShape.materialCount = 1; }
    if (meshGround) {
        std::array<b3Vec3, 4> vertices{{{-30, 0, -30}, {-30, 0, 30}, {30, 0, 30}, {30, 0, -30}}};
        std::array<int32_t, 6> indices{{0, 1, 2, 0, 2, 3}};
        std::array<uint8_t, 2> materials{{0, 0}};
        b3MeshDef def{}; def.vertices = vertices.data(); def.vertexCount = 4;
        def.indices = indices.data(); def.triangleCount = 2; def.materialIndices = materials.data();
        def.weldVertices = true; def.weldTolerance = .0001f; def.identifyEdges = true;
        mesh = b3CreateMesh(&def, nullptr, 0);
        auto body = b3DefaultBodyDef(); floors.push_back(b3CreateBody(world.id(), &body));
        b3CreateMeshShape(floors.back(), &floorShape, mesh, {1, 1, 1});
    } else {
        for (int i = 0; i < (overflow ? 32 : 1); ++i) {
            // Coincident static floors force graph overflow without introducing
            // pad edges that could physically obstruct the sliding test body.
            auto body = b3DefaultBodyDef(); body.position = {0, -.25f, 0};
            floors.push_back(b3CreateBody(world.id(), &body));
            const auto hull = b3MakeBoxHull(30.f, .25f, 30.f);
            b3CreateHullShape(floors.back(), &floorShape, &hull.base);
        }
    }
    auto bodyDef = b3DefaultBodyDef(); bodyDef.type = b3_dynamicBody; bodyDef.position = {0, .13f, 0};
    const auto orientation = glm::angleAxis(steering, glm::vec3(0, 1, 0));
    bodyDef.rotation = {{orientation.x, orientation.y, orientation.z}, orientation.w};
    const auto body = b3CreateBody(world.id(), &bodyDef);
    b3MotionLocks locks{}; locks.angularX = locks.angularY = locks.angularZ = true;
    b3Body_SetMotionLocks(body, locks);
    auto shapeDef = b3DefaultShapeDef(); shapeDef.baseMaterial.friction = 3;
    const auto hull = b3MakeBoxHull(.5f, .1f, .5f);
    const auto shape = b3CreateHullShape(body, &shapeDef, &hull.base);
    if (directional) b3Shape_SetFrictionAxle(shape, {1, 0, 0}, 2);
    for (int i = 0; i < 120; ++i) world.step(1.f / 60);
    const auto velocity = orientation * localVelocity;
    b3Body_SetLinearVelocity(body, {velocity.x, velocity.y, velocity.z});
    world.step(1.f / 60);
    if (overflow) require(b3World_GetCounters(world.id()).colorCounts[23] > 0,
                          "Directional-friction fixture must exercise the scalar overflow solver");
    for (int i = 0; i < 5; ++i) world.step(1.f / 60);
    const auto final = b3Body_GetLinearVelocity(body);
    const auto localFinal = glm::conjugate(orientation) * glm::vec3(final.x, final.y, final.z);
    b3DestroyBody(body);
    for (auto floor : floors) b3DestroyBody(floor);
    if (mesh) b3DestroyMesh(mesh);
    return localVelocity - localFinal;
}
void directionalSolver() {
    for (int mode = 0; mode < 4; ++mode) for (float steering : {0.f, .7f, 1.5707963f}) {
        const bool mesh = mode == 1 || mode == 2, materials = mode == 2, overflow = mode == 3;
        const auto roll = drag(mesh, materials, overflow, steering, {0, 0, -8});
        const auto sideways = drag(mesh, materials, overflow, steering, {8, 0, 0});
        std::cout << "directional mode=" << mode << " steer=" << steering << " drag=" << -roll.z << ',' << sideways.x << '\n';
        require(-roll.z > sideways.x * 1.1f && -roll.z < sideways.x * 1.4f,
                "Rolling-axis grip must exceed sideways grip and follow steering on every solver path");
        require(std::abs(roll.x) < .03f && std::abs(sideways.z) < .03f,
                "Directional friction must oppose motion without introducing cross-axis drift");
        const auto diagonal = drag(mesh, materials, overflow, steering, {8, 0, -8});
        const float grip = std::hypot(diagonal.x / sideways.x, diagonal.z / roll.z);
        require(grip > .85f && grip < 1.05f,
                "Diagonal motion must share an elliptical grip budget instead of doubling both axis limits");
    }
    const auto forward = drag(false, false, false, .7f, {0, 0, -8}, false);
    const auto sideways = drag(false, false, false, .7f, {8, 0, 0}, false);
    require(std::abs(-forward.z - sideways.x) < .01f,
            "Shapes without a friction axle must retain isotropic friction");
    const auto base = drag(true, false, false, 0, {0, 0, -8});
    const auto reduced = drag(true, true, false, 0, {0, 0, -8});
    require(-reduced.z < -base.z * .7f, "Directional tires must still respect per-triangle ground material friction");
}

}
int main() {
    try { contacts(false); contacts(true); directionalSolver(); }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
