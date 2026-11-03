#include "dog_layer.hpp"
#include "zombie_crowd.hpp"
#include "zombie_doorway_fixture.hpp"
#include "door_layer.hpp"
#include "physics_world.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
viewer::DogLayer pack(const std::vector<glm::vec3>& positions, glm::vec3 target) {
    viewer::DogLayer dogs; dogs.assets.emplace_back();
    for (const auto feet : positions) {
        viewer::DogInstance dog;
        dog.root = dog.transform = glm::translate(glm::mat4(1), feet);
        dogs.instances.push_back(dog);
    }
    dogs.rebuildSpatialIndex();
    for (auto& behavior : dogs.behaviors) {
        behavior.active = true; behavior.state = viewer::DogState::chasing;
        behavior.target = target; behavior.targetPlayer = 1;
    }
    return dogs;
}
const std::vector<viewer::ZombiePlayer> distantPlayer{{{300, 0, 300}, 1.7f, 1.8f, 99}};
void checkPack(const viewer::DogLayer& dogs, const std::vector<glm::vec3>& previous, const viewer::CollisionWorld& world) {
    for (std::size_t id = 0; id < dogs.instances.size(); ++id) {
        const auto& dog = dogs.instances[id];
        if (dog.dead || dog.removed) continue;
        const auto feet = glm::vec3(dog.root[3]);
        require(glm::distance(feet, previous[id]) <= 3.f / 60 + .0003f, "Dog steering must preserve its speed limit");
        for (const auto& contact : world.contacts(feet, viewer::dogRadius, viewer::dogHeight, 0, .05f))
            require(contact.terrainSupport || contact.depth < .001f || contact.normal.y > .7f,
                    "Dog passage must respect walls, panels and jambs");
        for (std::size_t other = 0; other < id; ++other) {
            if (dogs.instances[other].dead || dogs.instances[other].removed) continue;
            const auto position = glm::vec3(dogs.instances[other].root[3]);
            if (std::abs(position.y - feet.y) < viewer::dogHeight)
                require(glm::length(glm::vec2(feet.x-position.x, feet.z-position.z)) >= .4998f,
                        "Dog passage and spreading must not overlap living dogs");
        }
    }
}
std::vector<glm::vec3> positions(const viewer::DogLayer& dogs) {
    std::vector<glm::vec3> result;
    for (const auto& dog : dogs.instances) result.emplace_back(dog.root[3]);
    return result;
}
void spreading() {
    const viewer::CollisionWorld world(viewer::Model{});
    auto dogs = pack({{0,0,0}, {0,0,.7f}, {0,0,1.4f}, {0,0,2.1f}}, {0,0,30});
    dogs.updateBehavior(1.f/60, distantPlayer, world, 0);
    require(std::abs(dogs.instances[0].root[3].x) > .005f,
            "A straight dog queue must spread behind blockers");
    auto isolated = pack({{0,0,0}}, {0,0,30});
    isolated.updateBehavior(1.f/60, distantPlayer, world, 0);
    require(std::abs(isolated.instances[0].root[3].x) < .0001f,
            "A dog without neighbors must retain its direct approach");
    float width = 0;
    for (int step = 1; step < 180; ++step) {
        const auto previous = positions(dogs);
        dogs.updateBehavior(1.f/60, distantPlayer, world, double(step)/60);
        checkPack(dogs, previous, world);
        for (const auto& dog : dogs.instances) width = std::max(width, std::abs(dog.root[3].x));
    }
    require(width > .3f && dogs.instances[0].root[3].z > 6,
            "Spreading must give queued dogs lateral room and forward progress");
    auto floors = pack({{0,0,0}, {0,2,.7f}}, {0,0,30});
    floors.updateBehavior(1.f/60, distantPlayer, world, 0);
    require(std::abs(floors.instances[0].root[3].x) < .0001f,
            "Dogs on another floor must not cause spreading");
    auto dead = pack({{0,0,0}, {0,0,.7f}}, {0,0,30});
    dead.damage(1, 100); dead.updateBehavior(1.f/60, distantPlayer, world, 0);
    require(std::abs(dead.instances[0].root[3].x) < .0001f, "Dead dogs must leave spreading immediately");
}
struct Scene {
    viewer::Model model;
    std::shared_ptr<viewer::PhysicsWorld> physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::DoorLayer doors;
    viewer::CollisionWorld world;
    glm::mat4 rotation;
    static viewer::Model geometry(bool outward, float yaw, float width) {
        auto model = viewer::test::doorway(outward,width);
        const auto rotation = glm::rotate(glm::mat4(1), yaw, glm::vec3(0,1,0));
        for (auto& draw : model.draws) draw.transform = rotation * draw.transform;
        return model;
    }
    Scene(bool outward, float yaw, float width = 1) : model(geometry(outward,yaw,width)),
        doors(viewer::prepareDoorPanels(model), physics), world(model, {}, &doors),
        rotation(glm::rotate(glm::mat4(1),yaw,glm::vec3(0,1,0))) { doors.setAngle(0,glm::radians(90.f)); }
    glm::vec3 point(glm::vec3 local) const { return glm::vec3(rotation*glm::vec4(local,1)); }
};
std::vector<glm::vec3> passage(bool outward, float yaw, bool zombieQueue, bool killLeader = false) {
    Scene scene(outward,yaw);
    std::vector<glm::vec3> start;
    for (int row = 0; row < 3; ++row) for (const float x : {-.3f,.3f}) start.push_back(scene.point({x,0,1.4f+row*.7f}));
    const auto target = scene.point({.3f,0,-20});
    auto dogs = pack(start, target);
    viewer::ZombieCrowd zombies; zombies.reset(1); zombies.update(0,scene.point({0,0,.5f}));
    std::vector<glm::vec3> replay;
    std::size_t entered = 0;
    const auto total = dogs.instances.size() - (killLeader ? 1 : 0);
    for (int step = 0; step < 1200 && entered < total; ++step) {
        if (killLeader && step == 30) dogs.damage(0,100);
        if (zombieQueue) {
            zombies.beginStep(1.f/60);
            zombies.doorwayDestination(0,target,scene.world);
        }
        const auto previous = positions(dogs);
        dogs.updateBehavior(1.f/60,distantPlayer,scene.world,double(step)/60);
        checkPack(dogs, previous, scene.world);
        entered = 0;
        for (std::size_t id = 0; id < dogs.instances.size(); ++id) {
            const auto feet = glm::vec3(dogs.instances[id].root[3]);
            replay.push_back(feet);
            if (dogs.instances[id].dead) continue;
            entered += glm::vec3(glm::inverse(scene.rotation)*glm::vec4(feet,1)).z < -1.5f;
            require(dogs.behaviors[id].target == target && dogs.behaviors[id].state == viewer::DogState::chasing,
                    "Doorway hints must preserve the dog's remembered target and behavior");
        }
    }
    std::cout << "Dog door: outward=" << outward << " yaw=" << yaw << " entered=" << entered << '/' << total << '\n';
    require(entered == total, "All queued dogs must get through the fully open door");
    return replay;
}
void closedAndCloseGoal() {
    Scene scene(true,0); scene.doors.setAngle(0,0);
    auto dogs = pack({{0,0,1}}, {0,0,-8});
    for (int step = 0; step < 180; ++step) dogs.updateBehavior(1.f/60,distantPlayer,scene.world,double(step)/60);
    require(dogs.instances[0].root[3].z > .25f, "Dogs must not cross a closed door");
    scene.doors.setAngle(0,glm::radians(90.f));
    dogs = pack({{0,0,.5f}}, {0,0,-.2f}); dogs.behaviors[0].state = viewer::DogState::investigating;
    for (int step = 0; step < 180 && dogs.behaviors[0].state != viewer::DogState::idle; ++step)
        dogs.updateBehavior(1.f/60,distantPlayer,scene.world,double(step)/60);
    require(dogs.behaviors[0].state == viewer::DogState::idle && std::abs(dogs.instances[0].root[3].z+.2f) < .0002f,
            "Door hints must not overshoot a close remembered goal or report a queue point as arrival");

    Scene narrow(true,0,.68f);
    viewer::ZombieDoorwayGuidance humanoid;
    require(!humanoid.destination(0,{0,0,.8f},{0,0,-8},narrow.world),
            "The narrow fixture must remain too small for humanoid passage guidance");
    dogs = pack({{0,0,.8f}}, {0,0,-8});
    for (int step = 0; step < 180; ++step) {
        const auto previous = positions(dogs);
        dogs.updateBehavior(1.f/60,distantPlayer,narrow.world,double(step)/60);
        checkPack(dogs,previous,narrow.world);
    }
    require(dogs.instances[0].root[3].z < -1.5f,
            "Dogs must use dog-sized aperture clearance instead of the larger humanoid dimensions");
}
}
int main() {
    try {
        spreading(); closedAndCloseGoal();
        for (const bool outward : {true,false}) for (const float yaw : {0.f,.73f}) {
            const auto baseline = passage(outward,yaw,false);
            require(baseline == passage(outward,yaw,true), "Zombie passage reservations must not affect dog queues or movement");
        }
        passage(true,0,false,true);
        std::cout << "Dog spreading and independent doorway passage passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
