#include "vehicle_player.hpp"
#include "physics_world.hpp"
#include "physics_scene.hpp"
#include "zombie_layer.hpp"
#include "interior_fixture.hpp"
#include <box3d/box3d.h>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
viewer::Model ground(bool wall = false) {
    viewer::Model m; m.materials.push_back({});
    interior_fixture::quad(m, {-80, 0, -80}, {-80, 0, 80}, {80, 0, 80}, {80, 0, -80}, "terrain");
    if (wall) interior_fixture::quad(m, {-12, 0, -18}, {12, 0, -18}, {12, 5, -18}, {-12, 5, -18}, "building_test_walls", glm::mat4(1), true);
    interior_fixture::bounds(m); m.boundsMax.y = 8;
    return m;
}
struct Fixture {
    viewer::Model model;
    std::shared_ptr<viewer::PhysicsWorld> physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::CollisionWorld world;
    viewer::PhysicsScene scene;
    viewer::VehicleLayer cars{physics};
    Fixture(bool wall = false, std::vector<viewer::TrunkCollider> trunks = {}) : model(ground(wall)), world(model, trunks),
        scene(physics, world, model.boundsMin, model.boundsMax) {
        world.setVehicles(&cars);
        cars.add({0, .8f, 0});
    }
    void tick(int n, float dt = 1.f / 60) {
        for (int i = 0; i < n; ++i) physics->step(dt, [&](float step) {
            viewer::prepareVehicleScene(cars, scene); cars.beforeStep(step);
        });
    }
};
void suspensionAndDrive() {
    Fixture f; f.tick(180);
    auto box = f.cars.pose(0).chassis;
    std::cout << "rest y=" << box.center.y << " grounded=" << f.cars.groundedWheels(0) << '\n';
    require(box.center.y > .5f && box.center.y < .85f && f.cars.groundedWheels(0) == 4,
            "All four cylindrical tires must support the chassis on the real ground");
    for (const auto& wheel : f.cars.pose(0).wheels) {
        const float suspension = glm::vec3(wheel[3]).y - box.center.y + .32f;
        require(suspension > .01f && suspension < .175f, "Springs must compress under chassis weight without bottoming out");
    }
    f.cars.drive(0, 1, 0, false); f.tick(120);
    std::cout << "drive z=" << f.cars.pose(0).chassis.center.z << " v=" << f.cars.velocity(0).z << '\n';
    require(f.cars.pose(0).chassis.center.z < -4 && f.cars.velocity(0).z < -4, "W must accelerate along local -Z");
    require(std::abs(f.cars.pose(0).chassis.center.x) < .5f, "Centered tire colliders must keep straight-line driving stable");
    const float speed = glm::length(f.cars.velocity(0));
    f.cars.drive(0, 0, 0, false); f.tick(30);
    require(glm::length(f.cars.velocity(0)) > speed * .7f, "Releasing throttle must preserve rolling momentum");
    f.cars.drive(0, 0, 0, true); f.tick(180);
    require(glm::length(f.cars.velocity(0)) < .4f, "Brake must stop the car");
    f.cars.drive(0, -1, 0, false); f.tick(90);
    require(f.cars.velocity(0).z > 2, "S must reverse after stopping");
}
void accelerationAndGrip() {
    Fixture f; f.tick(120);
    for (const auto& wheel : f.cars.wheelTraction(0))
        require(wheel.motion == viewer::WheelMotion::Rolling, "Each parked wheel must independently report rolling contact");
    f.cars.drive(0, 1, 0, false);
    // Measurements from the previous 6,400 N car on this same flat fixture,
    // at .25, .5, 1 and 2 seconds after starting. Extra torque is load reserve,
    // not permission to make the ordinary launch faster or substantially slower.
    const std::array<float, 4> originalSpeeds{1.69314f, 3.32747f, 6.58299f, 12.3049f};
    const std::array<int, 4> frames{15, 15, 30, 60};
    for (std::size_t i = 0; i < frames.size(); ++i) {
        f.tick(frames[i]);
        const float speed = -f.cars.velocity(0).z;
        std::cout << "launch " << i << " speed=" << speed << '\n';
        require(speed <= originalSpeeds[i] + .1f && speed >= originalSpeeds[i] * .85f,
                "Tripled torque reserve must preserve the previous normal acceleration envelope");
    }
    Fixture skid; skid.tick(120); skid.cars.drive(0, 0, 0, false);
    const auto center = skid.cars.pose(0).chassis.center;
    skid.cars.applyImpulse(0, {3300, 0, 0}, center);
    bool detected = false;
    for (int frame = 0; frame < 20; ++frame) {
        skid.tick(1);
        for (const auto& wheel : skid.cars.wheelTraction(0)) if (wheel.motion == viewer::WheelMotion::Sliding) {
            detected = true;
            require(wheel.lateralFriction == viewer::slidingTireFriction && wheel.friction > .85f,
                    "Sideways skids must use sliding friction independently of forward grip");
            if (!wheel.longitudinalSliding)
                require(wheel.friction == viewer::rollingTireFriction, "Lateral slip must retain forward rolling grip");
        }
    }
    require(detected, "A sideways impulse must produce an observable per-wheel skid");
    skid.tick(120);
    for (const auto& wheel : skid.cars.wheelTraction(0))
        require(wheel.motion == viewer::WheelMotion::Rolling && wheel.friction == viewer::rollingTireFriction,
                "Settled wheels must recover rolling grip");
}
void crowdedStart() {
    Fixture f; f.tick(120);
    viewer::ZombieLayer zombies; zombies.assets.emplace_back(); zombies.attachPhysics(f.physics);
    for (int row = 0; row < 3; ++row) for (int column = -1; column <= 1; ++column) {
        viewer::ZombieInstance z;
        z.restRoot = z.restTransform = z.transform = glm::translate(glm::mat4(1), glm::vec3(column * .65f, 0, -2.09f - row * .61f));
        zombies.instances.push_back(z);
    }
    zombies.rebuildSpatialIndex();
    f.cars.drive(0, 1, 0, false);
    for (int frame = 0; frame < 180; ++frame) {
        const auto previous = f.cars.pose(0).chassis;
        f.physics->step(1.f / 60, [&](float seconds) {
            viewer::prepareVehicleScene(f.cars, f.scene); f.cars.beforeStep(seconds);
        }, [&](float seconds) {
            viewer::collideVehicleZombies(f.cars, 0, previous, zombies, f.world, seconds, 0);
        });
    }
    std::cout << "crowded start z=" << f.cars.pose(0).chassis.center.z << '\n';
    // The old car advanced only about 14 cm into this crowd in three seconds.
    require(f.cars.pose(0).chassis.center.z < -.6f && zombies.ragdollCount() == 0,
            "A dead-stop start must push through more of a standing crowd without ragdolling at low speed");
    const float loadedSpeed = -f.cars.velocity(0).z;
    for (auto& zombie : zombies.instances) zombie.removed = true;
    zombies.rebuildSpatialIndex();
    f.tick(60);
    require(-f.cars.velocity(0).z - loadedSpeed < 6.7f,
            "Clearing the crowd must not leave a sustained torque boost or accumulate a launch surge");
}

void steeringAndAir() {
    Fixture f; f.tick(120); f.cars.drive(0, 1, 0, false); f.tick(90);
    const auto before = f.cars.pose(0).chassis;
    f.cars.drive(0, 1, 1, false); f.tick(70);
    const auto after = f.cars.pose(0).chassis;
    const auto forward = after.rotation * glm::vec3(0, 0, -1);
    std::cout << "turn x=" << after.center.x << " forward x=" << forward.x << '\n';
    require(after.center.x > before.center.x + .5f && forward.x > .15f, "D must steer right while moving forward");
    const auto v = f.cars.velocity(0);
    const auto lateral = after.rotation * glm::vec3(1, 0, 0);
    require(std::abs(glm::dot(v, lateral)) > .1f, "Turning must retain finite lateral slip, permitting drift");
    auto physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::VehicleLayer airborne(physics); airborne.add({0, 20, 0}); airborne.drive(0, 1, 1, false);
    for (int i = 0; i < 30; ++i) physics->step(1.f / 60, [&](float s) { airborne.beforeStep(s); });
    require(airborne.groundedWheels(0) == 0 && std::abs(airborne.velocity(0).z) < .001f,
            "Airborne tires must supply no propulsion");
}
void frameRates() {
    Fixture slow, fast;
    slow.tick(60, 1.f / 30); fast.tick(240, 1.f / 120);
    slow.cars.drive(0, 1, -.3f, false); fast.cars.drive(0, 1, -.3f, false);
    slow.tick(60, 1.f / 30); fast.tick(240, 1.f / 120);
    require(glm::distance(slow.cars.pose(0).chassis.center, fast.cars.pose(0).chassis.center) < .02f,
            "Drive forces and suspension must run at the shared fixed rate, independent of rendering");
}
void obstacles() {
    for (bool tree : {false, true}) {
        Fixture f(!tree, tree ? std::vector<viewer::TrunkCollider>{{{0, 0, -12}, .5f, 7}} : std::vector<viewer::TrunkCollider>{});
        f.tick(120); f.cars.drive(0, 1, 0, false); f.tick(300);
        const auto p = f.cars.pose(0).chassis.center;
        std::cout << "obstacle " << tree << " z=" << p.z << '\n';
        require(p.z > (tree ? -11.f : -17.f), "Vehicle must not pass through static walls or indexed tree trunks");
    }
}
void player() {
    Fixture f; f.tick(120);
    viewer::CameraRig rig(f.world, f.model.boundsMin, f.model.boundsMax, 1.5f);
    require(rig.enterWalking(glm::vec3(-1.8f, 1.7f, -.25f)), "Driver fixture must spawn beside car");
    viewer::VehiclePlayer driver;
    const auto walkingEye = rig.camera().position();
    require(!driver.toggleThirdPerson(f.cars, rig.camera(), f.world) && !driver.thirdPerson() &&
            rig.camera().position() == walkingEye, "Third person must be unavailable outside a car");
    require(driver.toggle(f.cars, rig, f.world) && driver.driving(), "Enter must occupy nearby driver seat");
    const auto seat = rig.camera().position();
    rig.camera().look(100, 20); const float relativeYaw = rig.camera().yawDegrees();
    driver.updateCamera(f.cars, rig.camera(), f.world);
    require(glm::distance(seat, rig.camera().position()) < .001f && std::abs(relativeYaw - rig.camera().yawDegrees()) < .001f,
            "First person driver camera must retain mouse look");
    const float firstPitch = rig.camera().pitchDegrees();
    require(driver.toggleThirdPerson(f.cars, rig.camera(), f.world) && driver.thirdPerson(),
            "Driving must allow toggling third person");
    const auto pivot = f.cars.pose(0).chassis.center + glm::vec3(0, .65f, 0);
    const auto chase = rig.camera().position();
    require(chase.y > pivot.y + 1 && glm::distance(chase, pivot) > 5.5f &&
            glm::dot(glm::normalize(pivot - chase), rig.camera().direction()) > .999f,
            "Third person must show the car from above and behind the viewing direction");
    require(glm::distance(driver.eyePosition(f.cars), seat) < .001f,
            "Third person must not move the driver or zombie perception target");
    rig.camera().look(160, 0); driver.updateCamera(f.cars, rig.camera(), f.world);
    require(glm::distance(chase, rig.camera().position()) > .5f, "Mouse look must orbit the car in third person");
    require(driver.toggleThirdPerson(f.cars, rig.camera(), f.world) && !driver.thirdPerson() &&
            glm::distance(rig.camera().position(), seat) < .001f && rig.camera().pitchDegrees() == firstPitch,
            "Toggling back must restore the seat and first person pitch");
    driver.toggleThirdPerson(f.cars, rig.camera(), f.world);
    require(driver.toggle(f.cars, rig, f.world) && !driver.driving() && !driver.thirdPerson() && rig.walking(), "Enter must find a clear walking exit");
    require(glm::distance(rig.player().feet(), f.cars.pose(0).chassis.center) > 1.2f, "Exit must place player outside the chassis");
}
void cameraObstacles() {
    auto model = ground();
    interior_fixture::quad(model, {-10, 0, 3}, {10, 0, 3}, {10, 5, 3}, {-10, 5, 3}, "camera_wall");
    auto physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::CollisionWorld world(model);
    viewer::VehicleLayer cars(physics); cars.add({0, .8f, 0}); world.setVehicles(&cars);
    viewer::CameraRig rig(world, model.boundsMin, model.boundsMax, 1.5f);
    require(rig.enterWalking(glm::vec3(-1.8f, 1.7f, -.25f)), "Camera fixture must start beside car");
    viewer::VehiclePlayer driver; require(driver.toggle(cars, rig, world), "Camera fixture must enter car");
    driver.toggleThirdPerson(cars, rig.camera(), world);
    const auto pivot = cars.pose(0).chassis.center + glm::vec3(0, .65f, 0);
    require(rig.camera().position().z > 1 && rig.camera().position().z < 2.9f &&
            world.lineOfSight(pivot, rig.camera().position()), "Chase camera must pull in before a wall behind the car");
}
void zombies() {
    for (float speed : {1.f, 9.f}) {
        Fixture f; f.tick(120);
        viewer::ZombieLayer zombies; zombies.assets.emplace_back(); zombies.instances.emplace_back(); zombies.attachPhysics(f.physics);
        auto& z = zombies.instances[0];
        z.restRoot = z.restTransform = z.transform = glm::translate(glm::mat4(1), glm::vec3(0, 0, -2.08f));
        zombies.rebuildSpatialIndex();
        const auto previous = f.cars.pose(0).chassis;
        // Give the chassis actual momentum, then exercise the same swept contact path.
        f.cars.applyImpulse(0, {0, 0, -1100 * speed}, previous.center);
        viewer::collideVehicleZombies(f.cars, 0, previous, zombies, f.world, 1.f / 60, 0);
        if (speed < 5) {
            std::cout << "pushed z=" << z.restRoot[3].z << '\n';
            require(!z.ragdoll && z.restRoot[3].z < -2.10f, "Slow impacts must move the living zombie away");
            std::vector<std::size_t> found; zombies.queryLivingNear(glm::vec3(z.restRoot[3]), .05f, found);
            require(found.size() == 1 && found[0] == 0, "Pushing must update the living-body spatial index");
        } else {
            require(z.ragdoll && zombies.ragdollCount() == 1, "Fast impacts must use the owning ragdoll handoff");
            std::vector<std::size_t> found; zombies.queryLivingNear({0, 0, -2.08f}, 3, found);
            require(found.empty(), "Impact ragdolls must leave the living-body index");
        }
    }
}
}
int main() {
    try { suspensionAndDrive(); accelerationAndGrip(); crowdedStart(); steeringAndAir(); frameRates(); obstacles(); player(); cameraObstacles(); zombies(); }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
