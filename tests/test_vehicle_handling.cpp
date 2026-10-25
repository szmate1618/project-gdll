#include "physics_world.hpp"
#include "vehicle_layer.hpp"
#include <box3d/box3d.h>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
constexpr float stepSeconds = 1.f / 60;
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
struct Fixture {
    std::shared_ptr<viewer::PhysicsWorld> physics = std::make_shared<viewer::PhysicsWorld>();
    b3MeshData* mesh = nullptr;
    b3BodyId floor{};
    viewer::VehicleLayer cars;

    explicit Fixture(const viewer::VehicleConfig& config) : cars(physics, {config}) {
        // Match the town's triangle ground and material, without loading map assets.
        std::array<b3Vec3, 4> vertices{{{-80, 0, -80}, {-80, 0, 80},
                                       {80, 0, 80}, {80, 0, -80}}};
        std::array<int32_t, 6> indices{{0, 1, 2, 0, 2, 3}};
        b3MeshDef definition{};
        definition.vertices = vertices.data(); definition.vertexCount = 4;
        definition.indices = indices.data(); definition.triangleCount = 2;
        definition.weldVertices = true; definition.weldTolerance = .0001f;
        definition.identifyEdges = true;
        mesh = b3CreateMesh(&definition, nullptr, 0);
        require(mesh != nullptr, "Cannot create handling fixture ground");
        auto body = b3DefaultBodyDef(); floor = b3CreateBody(physics->id(), &body);
        auto shape = b3DefaultShapeDef(); shape.baseMaterial.friction = .6f;
        b3CreateMeshShape(floor, &shape, mesh, {1, 1, 1});
        cars.add({0, .8f, 0}); cars.setDriven(0, true);
        tick(120);
        require(cars.groundedWheels(0) == 4, config.name + ": fixture must load all four tires");
    }
    ~Fixture() {
        b3DestroyBody(floor);
        b3DestroyMesh(mesh);
    }
    void tick(int frames) {
        for (int i = 0; i < frames; ++i)
            physics->step(stepSeconds, [&](float seconds) { cars.beforeStep(seconds); });
    }
};
float heading(const viewer::VehiclePose& pose) {
    const auto forward = pose.chassis.rotation * glm::vec3(0, 0, -1);
    return std::atan2(forward.x, -forward.z);
}
float frontAngle(const viewer::VehiclePose& pose, std::size_t wheel) {
    // Cylinder-local Y is the axle. Its direction survives wheel spin.
    const auto axle = glm::conjugate(pose.chassis.rotation) * glm::vec3(pose.wheels[wheel][1]);
    return std::atan2(axle.z, axle.x);
}
float rearAxleSideSpeed(const viewer::VehicleLayer& cars, const viewer::VehiclePose& pose, glm::vec3 localAxle) {
    const auto lateral = pose.chassis.rotation * glm::vec3(1, 0, 0);
    const auto axlePosition = pose.chassis.center + pose.chassis.rotation * localAxle;
    return std::abs(glm::dot(cars.pointVelocity(0, axlePosition), lateral));
}
void loadedSteering(const viewer::VehicleConfig& config, float direction) {
    Fixture f(config);
    f.cars.drive(0, 0, direction, false); f.tick(90);
    const auto pose = f.cars.pose(0);
    const std::size_t inner = direction > 0 ? 1 : 0, outer = 1 - inner;
    const float inside = direction * frontAngle(pose, inner);
    const float outside = direction * frontAngle(pose, outer);
    std::cout << config.name << " stationary steering=" << inside << ',' << outside << '\n';
    require(inside > .50f && outside > .38f,
            config.name + ": loaded front tires must attain useful full steering lock");
    require(inside > outside + .03f,
            config.name + ": inside front tire must turn farther to avoid parking-speed scrub");
    require(glm::length(f.cars.velocity(0)) < .3f,
            config.name + ": steering stationary tires must not launch the chassis");
}
void lowSpeedTurn(const viewer::VehicleConfig& config, float direction, float throttle) {
    Fixture f(config);
    // Small throttle limits both wheel speed and torque, keeping the whole
    // measured turn below walking/running speed rather than accelerating out.
    f.cars.drive(0, throttle, 0, false); f.tick(180);
    f.cars.drive(0, throttle, direction, false); f.tick(60);
    auto previous = f.cars.pose(0);
    float distance = 0, yaw = 0, rearSlide = 0, minimumSpeed = 100, maximumSpeed = 0;
    const glm::vec3 rearAxle = (config.wheels[2] + config.wheels[3]) * .5f;
    constexpr int frames = 240;
    for (int frame = 0; frame < frames; ++frame) {
        f.tick(1);
        const auto pose = f.cars.pose(0);
        const auto displacement = pose.chassis.center - previous.chassis.center;
        distance += std::hypot(displacement.x, displacement.z);
        yaw += std::remainder(heading(pose) - heading(previous), glm::two_pi<float>());
        const float speed = glm::length(f.cars.velocity(0));
        minimumSpeed = std::min(minimumSpeed, speed); maximumSpeed = std::max(maximumSpeed, speed);
        // COM velocity has a legitimate sideways component while cornering.
        // The unsteered rear axle's velocity is the relevant sideways slip.
        rearSlide += rearAxleSideSpeed(f.cars, pose, rearAxle);
        previous = pose;
    }
    const float wheelbase = rearAxle.z - (config.wheels[0].z + config.wheels[1].z) * .5f;
    const float radius = distance / std::abs(yaw);
    std::cout << config.name << " turn=" << direction << " speed=" << minimumSpeed << ',' << maximumSpeed
              << " radius=" << radius << " rear slide=" << rearSlide / frames << '\n';
    require(minimumSpeed > 2 && maximumSpeed < 4,
            config.name + ": turning fixture must remain in the low-speed range");
    require(direction * yaw > .7f && radius < 2.5f * wheelbase,
            config.name + ": sustained full-lock turns must have a tight low-speed radius");
    require(rearSlide / frames < .35f,
            config.name + ": low-speed corners must retain rear tire grip");

    f.cars.drive(0, 0, direction, false);
    float coastSlide = 0;
    const float initialHeading = heading(previous);
    for (int frame = 0; frame < 60; ++frame) {
        f.tick(1);
        coastSlide += rearAxleSideSpeed(f.cars, f.cars.pose(0), rearAxle);
    }
    const float coastTurn = std::remainder(heading(f.cars.pose(0)) - initialHeading, glm::two_pi<float>());
    std::cout << config.name << " coast speed=" << glm::length(f.cars.velocity(0))
              << " rear slide=" << coastSlide / 60 << '\n';
    require(direction * coastTurn > .15f && glm::length(f.cars.velocity(0)) > 1.5f && coastSlide / 60 < .35f,
            config.name + ": coasting through a low-speed corner must preserve tire grip");
}
void sidewaysRecovery(const viewer::VehicleConfig& config) {
    Fixture f(config);
    f.cars.drive(0, 0, 0, false);
    const auto start = f.cars.pose(0).chassis.center;
    f.cars.applyImpulse(0, {config.weightKg * 3, 0, 0}, start);
    f.tick(45);
    const auto finish = f.cars.pose(0).chassis.center;
    std::cout << config.name << " skid distance=" << finish.x - start.x
              << " remaining speed=" << glm::length(f.cars.velocity(0)) << '\n';
    require(glm::length(f.cars.velocity(0)) < .35f && finish.x - start.x < 1.2f,
            config.name + ": dry-ground tires must promptly arrest a sideways skid");
}
} // namespace
int main() {
    try {
        struct Case { viewer::VehicleConfig config; float throttle; };
        const std::array<Case, 2> cases{{{viewer::VehicleConfig{}, .25f},
            {viewer::loadVehicleConfig(std::filesystem::path(TEST_VEHICLE_CONFIGS) / "mercedes_benz_s_500_1995.toml"), .35f}}};
        for (const auto& test : cases) {
            const auto& config = test.config;
            for (float direction : {-1.f, 1.f}) {
                loadedSteering(config, direction);
                lowSpeedTurn(config, direction, test.throttle);
            }
            sidewaysRecovery(config);
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
