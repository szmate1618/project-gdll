#include "vehicle_player.hpp"
#include "physics_scene.hpp"
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

namespace viewer {
namespace {
float yaw(const BoxPose& box) {
    const auto f = box.rotation * glm::vec3(0, 0, -1);
    return glm::degrees(std::atan2(f.z, f.x));
}
}
glm::vec3 vehicleSeat(const BoxPose& box) { return box.center + box.rotation * VehicleConfig{}.seats[0].eyePosition; }
glm::vec3 vehicleSeat(const VehicleLayer& cars, std::size_t car, std::size_t seat) {
    const auto box = cars.pose(car).chassis;
    return box.center + box.rotation * cars.config(car).seats.at(seat).eyePosition;
}
std::optional<VehicleSeatRef> vehicleEntry(const VehicleLayer& cars, glm::vec3 eye, const CollisionWorld& world,
    const std::function<bool(std::size_t, std::size_t)>& available) {
    std::optional<VehicleSeatRef> result;
    float nearest = std::numeric_limits<float>::infinity();
    for (std::size_t car = 0; car < cars.count(); ++car) {
        if (glm::length(cars.velocity(car)) >= 1.5f) continue;
        const auto box = cars.pose(car).chassis;
        const auto& seats = cars.config(car).seats;
        for (std::size_t seat = 0; seat < seats.size(); ++seat) {
            if (available && !available(car, seat)) continue;
            const auto point = box.center + box.rotation * seats[seat].entryPosition;
            const float distance = glm::distance(eye, point);
            if (distance <= seats[seat].entryRadius && distance < nearest && world.lineOfSight(eye, point)) {
                result = VehicleSeatRef{car, seat}; nearest = distance;
            }
        }
    }
    return result;
}
std::string vehicleEntryLabel(const VehicleLayer& cars, const VehicleSeatRef& entry) {
    return "Enter: " + cars.config(entry.car).name + " / " + vehicleSeatName(entry.seat);
}
std::vector<glm::vec3> vehicleExits(const VehicleLayer& cars, std::size_t car,
                                  const CollisionWorld& world, const FPSConfig& config, std::size_t seat) {
    if (glm::length(cars.velocity(car)) > 1.5f) return {};
    std::vector<glm::vec3> exits;
    const auto box = cars.pose(car).chassis;
    const auto& seats = cars.config(car).seats;
    for (std::size_t offset = 0; offset < seats.size(); ++offset) {
        const auto local = seats.at((seat + offset) % seats.size()).exitPosition;
        const auto preferred = box.center + box.rotation * local;
        const auto ground = world.groundAt(preferred.x, preferred.z, preferred.y - 2, preferred.y + .5f);
        if (!ground || ground->normal.y < .707f) continue;
        const glm::vec3 feet(preferred.x, ground->height + .005f, preferred.z);
        if (!world.lineOfSight(vehicleSeat(cars, car, seat), feet + glm::vec3(0, config.eyeHeight, 0))) continue;
        bool clear = true;
        for (const auto& c : world.contacts(feet, config.radius, config.height))
            if (c.depth > .002f) clear = false;
        if (clear && !world.insideBuilding(feet, config.radius, config.height)) exits.push_back(feet);
    }
    return exits;
}
bool placeVehicleNear(VehicleLayer& cars, const CollisionWorld& world,
                      glm::vec3 eye, glm::vec3 minimum, glm::vec3 maximum, std::size_t type) {
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
        if (clear) { cars.add(p + glm::vec3(0, .32f + VehicleLayer::wheelRadius, 0),0,type); return true; }
    }
    return false;
}
void prepareVehicleScene(const VehicleLayer& cars, PhysicsScene& scene) {
    for (const auto i : cars.simulatedCars()) {
        const auto p = cars.pose(i).chassis.center;
        const auto travel = cars.velocity(i) * .25f;
        scene.ensureNear(p + glm::min(travel, glm::vec3(0)) - glm::vec3(5),
                         p + glm::max(travel, glm::vec3(0)) + glm::vec3(5));
    }
}
bool VehiclePlayer::toggle(VehicleLayer& cars, CameraRig& rig, const CollisionWorld& world) {
    if (car_) {
        const auto& config = rig.player().config();
        for (const auto feet : vehicleExits(cars, *car_, world, config, seat_)) {
            if (!rig.enterWalking(feet + glm::vec3(0, config.eyeHeight, 0))) continue;
            if (driving()) cars.setDriven(*car_, false);
            followCar({}, cars, rig.camera());
            return true;
        }
        return false;
    }
    if (!rig.walking()) return false;
    const auto nearest = vehicleEntry(cars, rig.camera().position(), world);
    if (!nearest) return false;
    if (nearest->seat == 0) cars.setDriven(nearest->car, true);
    rig.enterFreeFly();
    followCar(nearest->car, cars, rig.camera(), nearest->seat);
    return true;
}
void VehiclePlayer::followCar(std::optional<std::size_t> car, const VehicleLayer& cars, Camera& camera, std::size_t seat) {
    if (car) (void)cars.config(*car).seats.at(seat);
    if (car == car_ && (!car || seat == seat_)) return;
    if (thirdPerson_) camera.setOrientation(camera.yawDegrees(), firstPersonPitch_);
    thirdPerson_ = false;
    car_ = car; seat_ = seat;
    if (car_) {
        const auto box = cars.pose(*car_).chassis;
        lastYaw_ = yaw(box);
        camera.setPosition(vehicleSeat(cars, *car_, seat_)); camera.setOrientation(lastYaw_, 0);
    }
}
bool VehiclePlayer::toggleThirdPerson(const VehicleLayer& cars, Camera& camera, const CollisionWorld& world) {
    if (!driving()) return false;
    thirdPerson_ = !thirdPerson_;
    if (thirdPerson_) {
        firstPersonPitch_ = camera.pitchDegrees();
        camera.setOrientation(camera.yawDegrees(), -18.f);
    } else camera.setOrientation(camera.yawDegrees(), firstPersonPitch_);
    updateCamera(cars, camera, world);
    return true;
}
glm::vec3 VehiclePlayer::eyePosition(const VehicleLayer& cars) const {
    return vehicleSeat(cars, car_.value(), seat_);
}
void VehiclePlayer::updateCamera(const VehicleLayer& cars, Camera& camera, const CollisionWorld& world) {
    if (!car_) return;
    const auto box = cars.pose(*car_).chassis;
    const float current = yaw(box);
    camera.setOrientation(camera.yawDegrees() + std::remainder(current - lastYaw_, 360.f), camera.pitchDegrees());
    lastYaw_ = current;
    if (!thirdPerson_) {
        camera.setPosition(vehicleSeat(cars, *car_, seat_));
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
