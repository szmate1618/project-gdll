#include "dog_layer.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>

namespace {
constexpr float pi = 3.14159265359f;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
viewer::DogLayer pack(std::size_t count = 1) {
    viewer::DogLayer dogs;
    dogs.assets.emplace_back();
    dogs.instances.resize(count);
    for (std::size_t id = 0; id < count; ++id)
        dogs.instances[id].root = dogs.instances[id].transform =
            glm::translate(glm::mat4(1),{float(id)*2,0,0});
    dogs.rebuildSpatialIndex();
    return dogs;
}
void quad(viewer::Model& model, const std::vector<glm::vec3>& points, const char* name) {
    viewer::Primitive primitive;
    for (const auto point : points) {
        viewer::Vertex vertex; vertex.position = point; primitive.vertices.push_back(vertex);
    }
    primitive.indices = {0,1,2,0,2,3};
    model.draws.push_back({model.primitives.size(),glm::mat4(1),name});
    model.primitives.push_back(primitive);
}
viewer::Model ground() {
    viewer::Model model;
    quad(model,{{-2000,0,-2000},{-2000,0,2000},{2000,0,2000},{2000,0,-2000}},"terrain");
    return model;
}
void rayWall(viewer::Model& model, glm::vec3 origin, float degrees, float height = 3) {
    const float angle = degrees*pi/180;
    const glm::vec3 direction(std::sin(angle),0,std::cos(angle));
    const glm::vec3 side(std::cos(angle),0,-std::sin(angle));
    const auto center = origin+direction*10.f;
    quad(model,{center-side,center+side,center+side+glm::vec3(0,height,0),
                center-side+glm::vec3(0,height,0)},"building_test_walls");
}
void start(viewer::DogLayer& dogs, int legs = 3) {
    auto& behavior = dogs.behaviors[0];
    behavior = {};
    behavior.active = true;
    behavior.state = viewer::DogState::wandering;
    behavior.wander.remainingLegs = legs;
    behavior.wander.previous = glm::vec3(dogs.instances[0].root[3]);
}
std::size_t wandering(const viewer::DogLayer& dogs) {
    std::size_t count = 0;
    for (const auto& behavior : dogs.behaviors)
        count += behavior.state == viewer::DogState::wandering;
    return count;
}
void selection() {
    viewer::CollisionWorld empty(viewer::Model{});
    auto dogs = pack(1002);
    dogs.instances[1000].dead = true;
    dogs.instances[1001].removed = true;
    dogs.updateBehavior(.1f,{},empty,500);
    dogs.updateBehavior(.1f,{},empty,559.99);
    require(wandering(dogs) == 0,"Wandering selection must wait a simulation minute");
    dogs.updateBehavior(.1f,{},empty,560);
    require(wandering(dogs) == 5,"A thousand living dogs must select five unique wanderers");
    for (std::size_t id = 0; id < 1000; ++id) {
        const auto& behavior = dogs.behaviors[id];
        if (behavior.state != viewer::DogState::wandering) continue;
        require(behavior.wander.remainingLegs >= 3 && behavior.wander.remainingLegs <= 7,
                "Wandering trip must have three to seven legs");
        require(dogs.instances[id].walking && !dogs.instances[id].running,
                "Distant wandering dogs must walk without players");
    }
    dogs.updateBehavior(.1f,{},empty,560);
    require(wandering(dogs) == 5,"One minute must not select twice");
    auto busy = pack(1000);
    busy.updateBehavior(.1f,{},empty,0);
    for (auto& behavior : busy.behaviors) behavior.state = viewer::DogState::chasing;
    busy.updateBehavior(.1f,{},empty,60);
    require(wandering(busy) == 0,"Selected busy dogs must not start wandering");
    auto smaller = pack(200);
    smaller.updateBehavior(.1f,{},empty,0);
    smaller.updateBehavior(.1f,{},empty,60);
    require(wandering(smaller) == 1,"Two hundred dogs must select one wanderer");
}
void directions() {
    const float angles[] = {0,30,-30,45,-45};
    auto model = ground();
    for (int blocked = 0; blocked <= 5; ++blocked) {
        viewer::CollisionWorld world(model);
        const auto target = viewer::dogWanderTarget({0,0,0},{0,1},150,world);
        if (blocked == 5) require(!target,"All five blocked rays must reject the leg");
        else {
            require(target.has_value(),"An open alternative direction was missed");
            const float angle = std::atan2(target->x,target->z)*180/pi;
            require(std::abs(angle-angles[blocked]) < .001f,"Wandering direction fallback order changed");
            require(std::abs(glm::length(*target)-150) < .001f,"Angular fallback changed leg length");
            rayWall(model,{0,0,0},angles[blocked]);
        }
    }
}
void trip() {
    viewer::CollisionWorld empty(viewer::Model{});
    for (int legs : {3,7}) {
        auto dogs = pack(); start(dogs,legs);
        glm::vec3 previous(0);
        int completed = 0;
        for (int step = 0; step < 4000; ++step) {
            const int remaining = dogs.behaviors[0].wander.remainingLegs;
            const auto reached = glm::vec3(dogs.instances[0].root[3]);
            dogs.updateBehavior(1,{},empty,step);
            const auto& behavior = dogs.behaviors[0];
            if (remaining > 0 && behavior.wander.remainingLegs < remaining) {
                const float distance = glm::length(reached-previous);
                require(distance >= 99.98f && distance <= 300.01f,"Leg left the 100-300 meter range");
                require(std::abs(reached.x) < .001f && reached.z > previous.z,
                        "Open legs must keep the original trip direction");
                previous = reached;
                ++completed;
            }
            if (behavior.state == viewer::DogState::idle) break;
        }
        require(completed == legs && dogs.behaviors[0].state == viewer::DogState::idle,
                "Wandering must finish after the chosen number of legs");
        require(glm::length(glm::vec3(dogs.instances[0].root[3])) < .02f,
                "Completed wandering trips must retrace all waypoints home");
        require(!dogs.instances[0].walking,"Completed trips must stop their walking animation");
    }
}
void returning() {
    auto model = ground();
    for (float angle : {0.f,30.f,-30.f,45.f,-45.f}) rayWall(model,{0,0,10},angle);
    viewer::CollisionWorld world(model);
    auto dogs = pack(); start(dogs);
    dogs.instances[0].root[3].z = 10;
    dogs.rebuildSpatialIndex();
    dogs.updateBehavior(.1f,{},world,0);
    require(dogs.behaviors[0].wander.returning && dogs.behaviors[0].target == glm::vec3(0),
            "Failed direction checks must return to the previous waypoint");
    require(dogs.instances[0].root[3].z < 10,"Returning dog walked in the wrong direction");
    for (int step = 1; step < 100 && dogs.behaviors[0].state == viewer::DogState::wandering; ++step)
        dogs.updateBehavior(.1f,{},world,step*.1);
    require(dogs.behaviors[0].state == viewer::DogState::idle && std::abs(dogs.instances[0].root[3].z) < .02f,
            "Dog must stop at the previous waypoint after returning");
    auto enclosed = pack(); start(enclosed);
    enclosed.instances[0].root[3].z = 10;
    enclosed.behaviors[0].wander.previous.z = 10;
    enclosed.rebuildSpatialIndex();
    enclosed.updateBehavior(.1f,{},world,0);
    require(enclosed.behaviors[0].state == viewer::DogState::wandering && enclosed.behaviors[0].wander.returning &&
            enclosed.behaviors[0].target == glm::vec3(0),
            "A blocked trip begun away from spawn must return to its territorial home");
    for (int step = 1; step < 110 && enclosed.behaviors[0].state == viewer::DogState::wandering; ++step)
        enclosed.updateBehavior(.1f,{},world,step*.1);
    require(enclosed.behaviors[0].state == viewer::DogState::idle && std::abs(enclosed.instances[0].root[3].z) < .02f,
            "The territorial home must survive movement and spatial-index rebuilding");

    // Eye-height visibility misses this low barrier; capsule movement must
    // still block it and eventually return instead of walking forever in place.
    auto low = ground();
    quad(low,{{-10,0,1},{10,0,1},{10,.45f,1},{-10,.45f,1}},"building_test_walls");
    viewer::CollisionWorld barrier(low);
    require(viewer::dogWanderTarget({0,0,0},{0,1},150,barrier).has_value(),"Low wall unexpectedly blocks sight");
    auto stalled = pack(); start(stalled);
    for (int step = 0; step < 100 && stalled.behaviors[0].state == viewer::DogState::wandering; ++step)
        stalled.updateBehavior(.1f,{},barrier,step*.1);
    require(stalled.behaviors[0].state == viewer::DogState::idle && stalled.instances[0].root[3].z < .02f,
            "Unwalkable visible legs must return and finish");
}
void perception() {
    viewer::CollisionWorld empty(viewer::Model{});
    auto dogs = pack(); start(dogs);
    dogs.updateBehavior(.1f,{{{0,0,200}}},empty,0);
    require(dogs.behaviors[0].state == viewer::DogState::wandering && dogs.behaviors[0].suspended,
            "Distant players must not prevent wandering");
    dogs.updateBehavior(.1f,{{{0,0,20}}},empty,.1);
    require(dogs.behaviors[0].state == viewer::DogState::chasing && dogs.instances[0].running &&
            dogs.behaviors[0].wander.remainingLegs == 0,"Full sight on reentry must interrupt wandering");
    auto model = ground(); rayWall(model,{0,0,0},0,.4f);
    viewer::CollisionWorld partial(model);
    auto investigating = pack(); start(investigating);
    investigating.updateBehavior(.1f,{{{0,0,20}}},partial,0);
    require(investigating.behaviors[0].state == viewer::DogState::investigating,
            "Partial sight must interrupt wandering with investigation");
    auto hearing = pack(); start(hearing);
    hearing.makeNoise({10,0,0},40);
    require(hearing.behaviors[0].state == viewer::DogState::looking && hearing.behaviors[0].wander.remainingLegs == 0,
            "Audible noise must interrupt wandering");
    auto biting = pack(); start(biting);
    biting.updateBehavior(.1f,{{{0,0,.8f}}},empty,0);
    require(biting.instances[0].attacking,"A nearby visible player must interrupt wandering with a bite");
}
} // namespace

int main() {
    try {
        selection(); directions(); trip(); returning(); perception();
        std::cout << "Dog wandering passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
