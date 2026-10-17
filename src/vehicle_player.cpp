#include "vehicle_player.hpp"
#include "physics_scene.hpp"
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>

namespace viewer {
namespace {
float yaw(const BoxPose& box) {
    const auto f = box.rotation * glm::vec3(0, 0, -1);
    return glm::degrees(std::atan2(f.z, f.x));
}
glm::vec3 seat(const BoxPose& box) { return box.center + box.rotation * glm::vec3(-.38f, .9f, -.25f); }
}
bool placeVehicleNear(VehicleLayer& cars, const CollisionWorld& world,
                      glm::vec3 eye, glm::vec3 minimum, glm::vec3 maximum) {
    // Require supported, clear footprint on published terrain, rather than
    // dropping the car into a building or choosing a GIS/cache height.
    for (float distance : {5.f, 8.f, 12.f, 18.f, 25.f}) for (int a = 0; a < 16; ++a) {
        const float angle = glm::two_pi<float>() * a / 16;
        glm::vec3 p = eye + glm::vec3(std::cos(angle) * distance, 0, std::sin(angle) * distance);
        if (p.x < minimum.x + 2 || p.z < minimum.z + 3 || p.x > maximum.x - 2 || p.z > maximum.z - 3) continue;
        const auto ground = world.groundAt(p.x, p.z, minimum.y - 2, maximum.y + 2, true);
        if (!ground || ground->normal.y < .9f) continue;
        p.y = ground->height;
        bool clear = true;
        for (float x : {-.75f, 0.f, .75f}) for (float z : {-1.65f, -.8f, 0.f, .8f, 1.65f}) {
            const auto sample = p + glm::vec3(x, .06f, z);
            const auto support = world.groundAt(sample.x, sample.z, p.y - .25f, p.y + .25f, true);
            if (!support || support->normal.y < .9f || world.insideBuilding(sample, .4f, 1.3f)) { clear = false; break; }
            for (const auto& c : world.contacts(sample, .42f, 1.3f))
                if (c.depth > .02f && c.normal.y < .7f) clear = false;
        }
        if (clear) { cars.add(p + glm::vec3(0, .8f, 0)); return true; }
    }
    return false;
}
void prepareVehicleScene(const VehicleLayer& cars, PhysicsScene& scene) {
    for (std::size_t i = 0; i < cars.count(); ++i) {
        const auto p = cars.pose(i).chassis.center;
        const auto travel = cars.velocity(i) * .25f;
        scene.ensureNear(p + glm::min(travel, glm::vec3(0)) - glm::vec3(5),
                         p + glm::max(travel, glm::vec3(0)) + glm::vec3(5));
    }
}
bool VehiclePlayer::toggle(VehicleLayer& cars, CameraRig& rig, const CollisionWorld& world) {
    if (car_) {
        const auto box = cars.pose(*car_).chassis;
        if (glm::length(cars.velocity(*car_)) > 1.5f) return false;
        const auto& config = rig.player().config();
        for (const auto local : {glm::vec3(-1.65f, .4f, -.25f), glm::vec3(1.65f, .4f, -.25f),
                                 glm::vec3(-1.65f, .4f, 1.4f), glm::vec3(1.65f, .4f, 1.4f)}) {
            const auto preferred = box.center + box.rotation * local;
            const auto ground = world.groundAt(preferred.x, preferred.z, preferred.y - 2, preferred.y + .5f);
            if (!ground || ground->normal.y < .707f) continue;
            const glm::vec3 feet(preferred.x, ground->height + .005f, preferred.z);
            if (!world.lineOfSight(seat(box), feet + glm::vec3(0, config.eyeHeight, 0))) continue;
            bool clear = true;
            for (const auto& c : world.contacts(feet, config.radius, config.height)) if (c.depth > .002f) clear = false;
            if (!clear || world.insideBuilding(feet, config.radius, config.height)) continue;
            if (rig.enterWalking(feet + glm::vec3(0, config.eyeHeight, 0))) {
                if (thirdPerson_) rig.camera().setOrientation(rig.camera().yawDegrees(), firstPersonPitch_);
                thirdPerson_ = false;
                cars.drive(*car_, 0, 0, true); car_.reset(); return true;
            }
        }
        return false;
    }
    if (!rig.walking()) return false;
    std::optional<std::size_t> nearest;
    float reach = 3.f;
    for (std::size_t i = 0; i < cars.count(); ++i) {
        const auto box = cars.pose(i).chassis;
        const auto door = box.center + box.rotation * glm::vec3(-1.1f, .8f, -.25f);
        const float distance = glm::distance(rig.camera().position(), door);
        if (distance < reach && glm::length(cars.velocity(i)) < 1.5f && world.lineOfSight(rig.camera().position(), door)) {
            nearest = i; reach = distance;
        }
    }
    if (!nearest) return false;
    car_ = nearest;
    thirdPerson_ = false;
    const auto box = cars.pose(*car_).chassis;
    lastYaw_ = yaw(box);
    rig.enterFreeFly();
    rig.camera().setPosition(seat(box)); rig.camera().setOrientation(lastYaw_, 0);
    return true;
}
bool VehiclePlayer::toggleThirdPerson(const VehicleLayer& cars, Camera& camera, const CollisionWorld& world) {
    if (!car_) return false;
    thirdPerson_ = !thirdPerson_;
    if (thirdPerson_) {
        firstPersonPitch_ = camera.pitchDegrees();
        camera.setOrientation(camera.yawDegrees(), -18.f);
    } else camera.setOrientation(camera.yawDegrees(), firstPersonPitch_);
    updateCamera(cars, camera, world);
    return true;
}
glm::vec3 VehiclePlayer::eyePosition(const VehicleLayer& cars) const {
    return seat(cars.pose(car_.value()).chassis);
}
void VehiclePlayer::updateCamera(const VehicleLayer& cars, Camera& camera, const CollisionWorld& world) {
    if (!car_) return;
    const auto box = cars.pose(*car_).chassis;
    const float current = yaw(box);
    camera.setOrientation(camera.yawDegrees() + std::remainder(current - lastYaw_, 360.f), camera.pitchDegrees());
    lastYaw_ = current;
    if (!thirdPerson_) {
        camera.setPosition(seat(box));
        return;
    }
    // Orbit above the car in world-up space; chassis yaw carries the view around
    // turns, while mouse look changes the driver's relative viewing angle.
    camera.setOrientation(camera.yawDegrees(), std::clamp(camera.pitchDegrees(), -65.f, -5.f));
    const auto pivot = box.center + glm::vec3(0, .65f, 0);
    const auto backward = -camera.direction();
    float distance = 6.f;
    if (!world.lineOfSight(pivot, pivot + backward * distance)) {
        float clear = 0, blocked = distance;
        for (int i = 0; i < 12; ++i) {
            const float middle = (clear + blocked) * .5f;
            if (world.lineOfSight(pivot, pivot + backward * middle)) clear = middle;
            else blocked = middle;
        }
        distance = std::max(0.f, clear - .2f); // Keep the near plane away from the blocker.
    }
    camera.setPosition(pivot + backward * distance);
}
} // namespace viewer
