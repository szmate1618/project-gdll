#include "physics_world.hpp"
#include <box3d/box3d.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
float length(b3Vec3 v) { return std::sqrt(v.x*v.x + v.y*v.y + v.z*v.z); }
struct Joint {
    viewer::PhysicsWorld world;
    b3BodyId a{}, b{};
    b3JointId joint{};
    bool ball;
    explicit Joint(bool spherical, bool dynamicParent = false) : ball(spherical) {
        b3World_SetGravity(world.id(), b3Vec3_zero);
        auto body = b3DefaultBodyDef(); body.enableSleep = false;
        body.type = dynamicParent ? b3_dynamicBody : b3_staticBody;
        a = b3CreateBody(world.id(), &body);
        body.type = b3_dynamicBody; b = b3CreateBody(world.id(), &body);
        const b3Sphere sphere{b3Vec3_zero, .2f};
        auto shape = b3DefaultShapeDef(); shape.filter.maskBits = 0;
        for (const auto id : {a, b}) {
            b3CreateSphereShape(id, &shape, &sphere);
            if (b3Body_GetType(id) == b3_dynamicBody)
                b3Body_SetMassData(id, {1.f, b3Vec3_zero, b3Mat3_identity});
        }
        if (ball) {
            auto def = b3DefaultSphericalJointDef(); def.base.bodyIdA = a; def.base.bodyIdB = b;
            def.enableSpring = true; def.hertz = 10; def.dampingRatio = 1;
            joint = b3CreateSphericalJoint(world.id(), &def);
        } else {
            auto def = b3DefaultRevoluteJointDef(); def.base.bodyIdA = a; def.base.bodyIdB = b;
            def.enableSpring = true; def.hertz = 10; def.dampingRatio = 1;
            joint = b3CreateRevoluteJoint(world.id(), &def);
        }
        target(.9f);
    }
    void target(float angle) {
        if (ball) {
            // A diagonal axis exposes accidental per-axis rather than total-torque caps.
            const float s = std::sin(angle / 2) / std::sqrt(3.f);
            b3SphericalJoint_SetTargetRotation(joint, {{s, s, s}, std::cos(angle / 2)});
        } else b3RevoluteJoint_SetTargetAngle(joint, angle);
    }
    void limit(float torque) {
        if (ball) b3SphericalJoint_SetMaxSpringTorque(joint, torque);
        else b3RevoluteJoint_SetMaxSpringTorque(joint, torque);
    }
    float limit() const {
        return ball ? b3SphericalJoint_GetMaxSpringTorque(joint) : b3RevoluteJoint_GetMaxSpringTorque(joint);
    }
    // With I = identity and no contacts/gravity, angular momentum change is
    // the applied angular impulse. Measure behavior independently of the solver.
    float measuredTorque(float dt, int substeps) {
        const auto before = b3Body_GetAngularVelocity(b);
        b3World_Step(world.id(), dt, substeps);
        const auto after = b3Body_GetAngularVelocity(b);
        return length({after.x-before.x, after.y-before.y, after.z-before.z}) / dt;
    }
};
void boundedTorque() {
    for (bool ball : {false, true}) for (bool warm : {false, true})
        for (float dt : {1.f/30, 1.f/60, 1.f/120}) for (int substeps : {1, 4, 8}) {
            Joint f(ball, true); b3World_EnableWarmStarting(f.world.id(), warm);
            require(f.limit() == FLT_MAX, "Existing springs must default to unlimited torque");
            f.limit(2);
            for (int step = 0; step < 8; ++step) {
                const float torque = f.measuredTorque(dt, substeps);
                require(torque > 1.8f && torque <= 2.002f, "Spring exceeded its torque cap or failed to apply it");
                const auto a = b3Body_GetAngularVelocity(f.a), b = b3Body_GetAngularVelocity(f.b);
                require(length({a.x+b.x, a.y+b.y, a.z+b.z}) < .0001f,
                    "Capped joint failed to apply equal and opposite torques");
            }
        }
}
void changingLimitsAndRecovery() {
    for (bool ball : {false, true}) {
        Joint f(ball);
        require(f.measuredTorque(1.f/60, 1) > 20, "Unlimited spring no longer tracks its target strongly");
        // Retain a large cached impulse, then reduce both torque and substep duration.
        f.limit(.5f);
        require(f.measuredTorque(1.f/120, 8) <= .501f, "Reduced limit/timestep reused excessive cached impulse");
        f.limit(0);
        require(f.measuredTorque(1.f/60, 4) < .001f, "Zero cap allowed spring torque through warm starting");
        b3Body_SetAngularVelocity(f.b, b3Vec3_zero);
        b3Body_SetTransform(f.b, {0,0,0}, b3Quat_identity);
        f.limit(2);
        for (int step = 0; step < 90; ++step) {
            // A held object blocks rotation but cannot increase muscle strength.
            b3Body_SetAngularVelocity(f.b, b3Vec3_zero);
            b3Body_SetTransform(f.b, {0,0,0}, b3Quat_identity);
            require(f.measuredTorque(1.f/60, 4) <= 2.002f, "Sustained blocked-pose error escaped the cap");
        }
        // This deliberately weak 2 N*m drive on unit inertia needs time to settle.
        for (int step = 0; step < 3600; ++step) b3World_Step(f.world.id(), 1.f/60, 4);
        const auto q = b3Body_GetRotation(f.b);
        const float angle = 2 * std::acos(std::clamp(std::abs(q.s), 0.f, 1.f));
        require(std::abs(angle - .9f) < .01f, "Released capped spring failed to recover its original absolute target");
        f.limit(FLT_MAX);
        require(f.limit() == FLT_MAX, "Cannot restore the unlimited spring default");
    }
}
void recordingReplay() {
    for (bool ball : {false, true}) {
        Joint f(ball); f.limit(3);
        auto* recording = b3CreateRecording(1024 * 1024);
        b3World_StartRecording(f.world.id(), recording);
        f.limit(1); f.target(-.6f);
        for (int i = 0; i < 10; ++i) b3World_Step(f.world.id(), 1.f/60, 4);
        b3World_StopRecording(f.world.id());
        const bool valid = b3ValidateReplay(b3Recording_GetData(recording), b3Recording_GetSize(recording), 1);
        b3DestroyRecording(recording);
        require(valid, "Spring limits did not survive snapshot-seeded recording/replay");
    }
}
} // namespace
int main() {
    try { boundedTorque(); changingLimitsAndRecovery(); recordingReplay();
        std::cout << "Spring torque caps, warm starts, blocked recovery and replay passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
