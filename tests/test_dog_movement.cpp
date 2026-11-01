#include "dog_layer.hpp"
#include "fps_controller.hpp"
#include "zombie_movement.hpp"
#include "zombie_placement.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void quad(viewer::Model& model, const std::string& name,
          glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d) {
    viewer::Primitive primitive;
    for (const auto point : {a,b,c,d}) {
        viewer::Vertex vertex; vertex.position = point; primitive.vertices.push_back(vertex);
    }
    primitive.indices = {0,1,2,0,2,3};
    model.draws.push_back({model.primitives.size(),glm::mat4(1),name});
    model.primitives.push_back(std::move(primitive));
}
viewer::Model ledge(float rise, int kind, float slope = 0) {
    viewer::Model model;
    quad(model,"terrain",{-10,-10*slope,-10},{-10,-10*slope,10},
         {10,10*slope,10},{10,10*slope,-10});
    if (kind == 1) {
        // Exposed upper side of a generated foundation's downward-facing cap.
        quad(model,"building_test_walls",{0,rise,-3},{5,rise+5*slope,-3},
             {5,rise+5*slope,3},{0,rise,3});
    } else {
        quad(model,kind == 0 ? "roads_residential" : "pavement",{0,rise,-3},
             {0,rise,3},{5,rise+5*slope,3},{5,rise+5*slope,-3});
        if (kind == 2)
            quad(model,"curb",{0,0,-3},{0,0,3},{0,rise,3},{0,rise,-3});
    }
    return model;
}
viewer::DogLayer pursuer(glm::vec3 start, glm::vec3 destination) {
    viewer::DogLayer dogs; dogs.assets.emplace_back();
    dogs.assets[0].duration = 1; dogs.assets[0].frameCount = 2;
    viewer::DogInstance dog;
    const auto delta = destination-start;
    dog.root = dog.transform = glm::translate(glm::mat4(1),start)*
        glm::rotate(glm::mat4(1),std::atan2(delta.x,delta.z),{0,1,0});
    dogs.instances.push_back(dog); dogs.rebuildSpatialIndex();
    auto& behavior = dogs.behaviors[0];
    behavior.active = true; behavior.state = viewer::DogState::chasing;
    behavior.target = destination; behavior.targetPlayer = 1; behavior.sight = 2;
    // Keep remembered-target movement independent of scheduled sight classification.
    behavior.perception.initialized = true; behavior.perception.period = .1;
    behavior.perception.nextSeconds = 1000;
    return dogs;
}
glm::vec3 walk(const viewer::CollisionWorld& world, glm::vec3 start, glm::vec3 destination, int rate) {
    auto dogs = pursuer(start,destination);
    for (int tick = 0; tick < 2*rate; ++tick) {
        dogs.updateBehavior(1.f/rate,{{destination,1.7f,1.8f,1}},world,double(tick)/rate);
        const auto feet = glm::vec3(dogs.instances[0].root[3]);
        for (const auto& contact : world.contacts(feet,viewer::dogRadius,viewer::dogHeight)) {
            if (contact.terrainSupport) continue;
            if (contact.depth > .0003f) {
                std::cerr << "penetration at " << feet.x << ',' << feet.y << ',' << feet.z
                          << " depth=" << contact.depth << '\n';
                throw std::runtime_error("A dog step entered solid scene geometry");
            }
        }
    }
    return glm::vec3(dogs.instances[0].root[3]);
}
void smallLedges() {
    for (int kind : {0,1,2}) for (float rise : {.02f,.087f,.25f,.30f,.31f,.45f}) {
        const viewer::CollisionWorld world(ledge(rise,kind));
        for (int rate : {30,60,144}) {
            const auto end = walk(world,{-1,0,0},{4,rise+.002f,0},rate);
            if (rise <= .30f) {
                if (end.x < 2) std::cerr << "blocked kind=" << kind << " rise=" << rise
                    << " rate=" << rate << " feet=" << end.x << ',' << end.y << '\n';
                require(end.x > 2,"Dog failed to climb a ledge within the 30 cm step limit");
                require(std::abs(end.y-rise) < .006f,"Dog did not remain supported on the raised surface");
                const auto down = walk(world,{2,rise+.002f,0},{-4,.002f,0},rate);
                require(down.x < -1 && std::abs(down.y) < .006f,"Dog failed to descend from the ledge");
            } else require(end.x < 0,"Dog climbed above its 30 cm step limit");
        }
    }
}
void slopedRoads() {
    for (float slope : {-.15f,.07f,.25f}) for (float rise : {.087f,.25f,.30f,.31f}) {
        const viewer::CollisionWorld world(ledge(rise,0,slope));
        const auto end = walk(world,{-1,-slope,0},{4,rise+4*slope+.002f,0},60);
        if (rise <= .30f) {
            if (end.x < 2) std::cerr << "blocked slope=" << slope << " rise=" << rise
                << " feet=" << end.x << ',' << end.y << '\n';
            require(end.x > 2,"Dog failed to climb a small road ledge on a slope");
        } else require(end.x < 0,"Slope compensation admitted an over-height ledge");
    }
}
void glancingWall() {
    auto model = ledge(.087f,0);
    quad(model,"building_test_walls",{1,-1,-1},{1,2,-1},{1,2,1},{1,-1,1});
    const viewer::CollisionWorld world(model);
    const auto end = walk(world,{-1,0,0},{4,.089f,2},60);
    require(end.x > 2,"Dog stopped at a glancing wall instead of sliding past its end");
}
void missingSupport() {
    viewer::Model model;
    quad(model,"terrain",{-10,0,-10},{-10,0,10},{0,0,10},{0,0,-10});
    const viewer::CollisionWorld world(model);
    require(walk(world,{-1,0,0},{4,.002f,0},60).x <= .001f,
            "Dog continued into empty space without walking support");
}
void clearance() {
    auto lowCeiling = ledge(.25f,2);
    quad(lowCeiling,"ceiling",{-.6f,1.f,-3},{5,1.f,-3},{5,1.f,3},{-.6f,1.f,3});
    const viewer::CollisionWorld tight(lowCeiling);
    require(walk(tight,{-1,0,0},{4,.252f,0},60).x < 0,"Dog step ignored insufficient headroom");
    auto wall = ledge(.2f,0);
    quad(wall,"building_test_walls",{.2f,0,-3},{.2f,0,3},{.2f,3,3},{.2f,3,-3});
    const viewer::CollisionWorld blocked(wall);
    require(walk(blocked,{-1,0,0},{4,.202f,0},60).x < 0,"Dog step passed through a wall");
}
void mapRoutes(const std::filesystem::path& path) {
    const auto model = viewer::loadModel(path);
    const viewer::CollisionWorld world(model);
    viewer::FPSController player;
    require(player.spawn({0,model.boundsMax.y+5,0},world),"Near-origin viewer spawn is unavailable");
    const auto target = player.feet();
    const auto placements = viewer::placeZombies(world,model,{0,0},30,100);
    std::size_t reachable = 0;
    for (const auto& placement : placements) {
        const auto start = glm::vec3(placement[3]);
        auto human = start;
        for (int tick = 0; tick < 600; ++tick) {
            const auto offset = glm::vec2(target.x-human.x,target.z-human.z);
            const float distance = glm::length(offset);
            if (distance <= 1) break;
            const auto direction = offset/distance;
            human = viewer::moveZombieBody(human,human+glm::vec3(direction.x*.05f,0,direction.y*.05f),world).feet;
        }
        if (glm::length(human-target) > 1.1f) continue; // Full walls also block the humanoid reference.
        auto dogs = pursuer(start,target);
        for (int tick = 0; tick < 600; ++tick)
            dogs.updateBehavior(1.f/60,{{target,player.eyeHeight(),player.bodyHeight(),1}},world,double(tick)/60);
        const auto end = glm::vec3(dogs.instances[0].root[3]);
        if (glm::length(end-target) > 1.1f)
            std::cerr << "Mapped dog blocked from " << start.x << ',' << start.y << ',' << start.z
                      << " at " << end.x << ',' << end.y << ',' << end.z << '\n';
        require(glm::length(end-target) <= 1.1f,"Dog could not follow a reachable near-spawn zombie approach");
        ++reachable;
    }
    require(reachable > 0,"Actual-map fixture contains no reachable near-spawn approaches");
    std::cout << "Actual-map dog approaches matched " << reachable << " reachable zombie routes\n";
}

}
int main(int argc, char** argv) {
    try { smallLedges(); slopedRoads(); glancingWall(); missingSupport(); clearance(); if (argc > 1) mapRoutes(argv[1]); std::cout << "Dog ledges and clearance passed\n"; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
