#include <box3d/box3d.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void motorImpulse(bool steering, bool warmStarting, int frames, int substeps,
                  float direction, bool braking) {
    auto definition = b3DefaultWorldDef();
    definition.gravity = {0, 0, 0};
    definition.enableSleep = false;
    const auto world = b3CreateWorld(&definition);
    b3World_EnableWarmStarting(world, warmStarting);

    auto bodyDefinition = b3DefaultBodyDef();
    const auto chassis = b3CreateBody(world, &bodyDefinition);
    bodyDefinition.type = b3_dynamicBody;
    bodyDefinition.allowFastRotation = true;
    const auto wheel = b3CreateBody(world, &bodyDefinition);
    auto shapeDefinition = b3DefaultShapeDef();
    shapeDefinition.density = 1;
    // A sphere's inertia stays constant as it spins, so torque * time gives
    // the expected angular momentum independently of the joint solver.
    const b3Sphere sphere{{0, 0, 0}, 1};
    b3CreateSphereShape(wheel, &shapeDefinition, &sphere);
    const float initialSpeed = braking ? direction * 10 : 0;
    b3Body_SetAngularVelocity(wheel, {0, 0, initialSpeed});
    const auto inertia = b3Body_GetMassData(wheel).inertia.cz.z;

    constexpr float torque = 1;
    auto joint = b3DefaultWheelJointDef();
    joint.base.bodyIdA = chassis;
    joint.base.bodyIdB = wheel;
    joint.enableSuspensionSpring = false;
    joint.enableSteering = steering;
    joint.enableSpinMotor = true;
    joint.maxSpinTorque = torque;
    joint.spinSpeed = braking ? 0 : direction * 100;
    b3CreateWheelJoint(world, &joint);

    const float seconds = 1.f / frames;
    for (int frame = 0; frame < frames; ++frame)
        b3World_Step(world, seconds, substeps);
    const auto velocity = b3Body_GetAngularVelocity(wheel);
    const float impulse = inertia * (velocity.z - initialSpeed);
    const float expected = (braking ? -direction : direction) * torque;
    b3DestroyWorld(world);

    if (std::abs(impulse - expected) > .001f) {
        std::cerr << "steering=" << steering << " warmStarting=" << warmStarting
                  << " frames=" << frames << " substeps=" << substeps
                  << " direction=" << direction << " braking=" << braking
                  << " impulse=" << impulse << " expected=" << expected << '\n';
        throw std::runtime_error("Wheel motor must respect its commanded torque over time");
    }
}
} // namespace

int main() {
    try {
        for (bool steering : {false, true})
            for (bool warmStarting : {false, true})
                for (int frames : {60, 120})
                    for (int substeps : {1, 4})
                        for (float direction : {-1.f, 1.f})
                            for (bool braking : {false, true})
                                motorImpulse(steering, warmStarting, frames, substeps, direction, braking);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
