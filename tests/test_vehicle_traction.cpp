#include "vehicle_traction.hpp"
#include "physics_world.hpp"
#include <box3d/box3d.h>
#include <cmath>
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
    require(state.motion == viewer::WheelMotion::Sliding && std::abs(state.lateralSlip) > 2.9f,
            "A spinning wheel may roll forward while sliding sideways");
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
}
int main() {
    try { contacts(false); contacts(true); }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
