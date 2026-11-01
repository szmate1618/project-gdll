#include "dog_layer.hpp"
#include "zombie_detached_parts.hpp"
#include "zombie_ragdoll.hpp"
#include "physics_world.hpp"
#include "physics_scene.hpp"
#include <box3d/box3d.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using viewer::RagdollPart;
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
viewer::DogLayer dogAt(glm::vec3 feet = {4,0,3}) {
    viewer::DogLayer dogs; dogs.assets.emplace_back(); dogs.instances.resize(1);
    dogs.instances[0].root = dogs.instances[0].transform = glm::translate(glm::mat4(1),feet);
    dogs.rebuildSpatialIndex();
    auto& behavior = dogs.behaviors[0];
    behavior.active = true; behavior.state = viewer::DogState::wandering;
    behavior.target = feet+glm::vec3(0,0,150);
    behavior.wander.remainingLegs = 3; behavior.wander.hasTarget = true;
    behavior.wander.previous = {4,0,0};
    behavior.wander.waypoints = {{0,0,0},{4,0,0}};
    behavior.wander.nextPartCheck = 5;
    return dogs;
}
std::shared_ptr<viewer::PhysicsWorld> simulation() {
    auto world = std::make_shared<viewer::PhysicsWorld>();
    b3World_SetGravity(world->id(),{0,0,0});
    return world;
}
void queriesAndLifetime() {
    auto physics = simulation(); viewer::ZombieRagdollWorld ragdolls(physics);
    auto& parts = ragdolls.detachedParts();
    std::vector<viewer::DetachedZombiePart> found;
    const auto intact = ragdolls.create({0,0,2},0,{});
    const auto head = ragdolls.create({0,0,4},0,{});
    ragdolls.sever(head,RagdollPart::head);
    parts.queryNear({},100,10,found);
    require(found.empty(),"Intact limbs and detached heads must not be fetch candidates");
    for (int i = 0; i < 15; ++i) {
        const auto handle = ragdolls.create({0,0,float(i*3+6)},0,{});
        ragdolls.sever(handle,RagdollPart::lowerArmLeft);
    }
    const auto far = ragdolls.create({0,120,0},0,{}); ragdolls.sever(far,RagdollPart::lowerLegLeft);
    parts.queryNear({},100,10,found);
    require(found.size() == 10,"Spatial query must return at most ten nearby limb assemblies");
    float previous = 0;
    for (const auto& part : found) {
        const float distance = glm::length(part.position);
        require(part.part == RagdollPart::lowerArmLeft && distance <= 100 && distance >= previous,
                "Candidate query must filter the 3D radius and prioritize nearby limbs");
        previous = distance;
    }
    const auto first = found.front();
    auto grip = parts.carry(first.body,{0,1,0});
    require(grip && grip->valid() && physics->isGrabbed(first.body),"Carry must create a live joint and claim");
    require(!parts.carry(first.body,{0,1,0}),"Two dogs must not attach to one limb");
    parts.queryNear({},100,10,found);
    for (const auto& part : found) require(!B3_ID_EQUALS(part.body,first.body),"Claimed limb leaked into candidates");
    grip.reset();
    require(parts.availablePosition(first.body).has_value(),"Dropping a part must release the claim");
    const auto doomed = ragdolls.create({0,0,-3},0,{}); ragdolls.sever(doomed,RagdollPart::lowerLegRight);
    parts.queryNear({0,0,-3},2,10,found);
    require(!found.empty(),"Missing nearby detached leg");
    const auto stale = found.front().body;
    auto removedGrip = parts.carry(stale,{0,1,0});
    ragdolls.destroy(doomed);
    require(!removedGrip->valid() && !parts.availablePosition(stale),"Corpse removal must invalidate carried targets");
    removedGrip.reset();
    ragdolls.create({0,0,-3},0,{});
    require(!parts.availablePosition(stale),"Reused physics slots must not revive stale limb references");
    ragdolls.destroy(intact);
}
void assemblySnap() {
    auto physics = simulation(); viewer::ZombieRagdollWorld ragdolls(physics);
    const auto handle = ragdolls.create({8,0,0},0,{});
    ragdolls.sever(handle,RagdollPart::upperArmLeft);
    auto& parts = ragdolls.detachedParts();
    std::vector<viewer::DetachedZombiePart> found;
    parts.queryNear({8,0,0},5,10,found);
    require(found.size() == 1,"A detached whole arm must be one candidate");
    const auto before = ragdolls.bodyTransforms(handle);
    const auto revision = ragdolls.poseRevision(handle);
    const glm::vec3 mouth(1,.6f,2);
    auto grip = parts.carry(found[0].body,mouth);
    const auto after = ragdolls.bodyTransforms(handle);
    const auto upper = std::size_t(RagdollPart::upperArmLeft), lower = std::size_t(RagdollPart::lowerArmLeft);
    require(glm::distance(glm::vec3(after[upper][3]),mouth) < .0001f,"Pickup must snap the limb to the mouth");
    require(glm::distance(glm::vec3(after[lower][3]-after[upper][3]),
                          glm::vec3(before[lower][3]-before[upper][3])) < .0001f,
            "Pickup must translate the connected forearm with its upper arm");
    require(after[0] == before[0] && ragdolls.poseRevision(handle) > revision,
            "Pickup must publish the changed pose without moving the corpse torso");
    require(!ragdolls.sleeping(handle),"Pickup must wake the detached physics bodies");
    b3World_SetGravity(physics->id(),{0,-9.8f,0});
    for (int step = 0; step < 60; ++step) {
        const auto movingMouth = mouth+glm::vec3(float(step)*.02f,0,0);
        grip->setTarget(movingMouth); physics->step(1.f/60);
        const auto carried = ragdolls.bodyTransforms(handle);
        require(glm::distance(glm::vec3(carried[upper][3]),movingMouth) < .08f,
                "Carry joint must keep the limb at a moving mouth under gravity");
    }
}
void fetchHome() {
    auto physics = simulation(); viewer::ZombieRagdollWorld ragdolls(physics);
    const auto handle = ragdolls.create({4,0,8},0,{}); ragdolls.sever(handle,RagdollPart::lowerArmLeft);
    auto& parts = ragdolls.detachedParts(); viewer::CollisionWorld empty(viewer::Model{});
    auto dogs = dogAt();
    dogs.updateBehavior(.1f,{},empty,4.99,&parts);
    require(dogs.behaviors[0].state == viewer::DogState::wandering,"Limb scan ran before five seconds");
    const auto interrupted = glm::vec3(dogs.instances[0].root[3]);
    const auto baseline = b3World_GetCounters(physics->id());
    dogs.updateBehavior(.1f,{},empty,5,&parts);
    require(dogs.behaviors[0].state == viewer::DogState::fetching,"Visible detached arm must start fetch mode");
    require(dogs.behaviors[0].fetch->homeRoute.back() == interrupted && dogs.behaviors[0].wander.waypoints.empty(),
            "Fetch must keep an independent route snapshot including its interruption point");
    const auto body = dogs.behaviors[0].fetch->part;
    bool carried = false, interruptionVisited = false, waypointVisited = false;
    for (int step = 1; step < 500 && dogs.behaviors[0].state == viewer::DogState::fetching; ++step) {
        dogs.updateBehavior(.1f,{},empty,5+step*.1,&parts);
        const auto& behavior = dogs.behaviors[0];
        if (behavior.fetch && behavior.fetch->grip) {
            carried = true;
            require(physics->isGrabbed(body),"Carrying dog lost its exclusive claim");
            require(b3World_GetCounters(physics->id()).jointCount == baseline.jointCount+1,
                    "Carrying must add exactly one mouth joint");
            const auto feet = glm::vec3(dogs.instances[0].root[3]);
            if (glm::distance(feet,interrupted) < .13f) interruptionVisited = true;
            if (interruptionVisited && glm::distance(feet,glm::vec3(4,0,0)) < .13f) waypointVisited = true;
        }
        physics->step(.1f);
    }
    require(carried && interruptionVisited && waypointVisited,"Fetch skipped pickup or retraced route points");
    require(dogs.behaviors[0].state == viewer::DogState::idle && !dogs.behaviors[0].fetch &&
            glm::length(glm::vec3(dogs.instances[0].root[3])) < .02f,"Fetching dog must idle only after reaching home");
    require(!physics->isGrabbed(body) && b3World_GetCounters(physics->id()).jointCount == baseline.jointCount,
            "Home arrival must remove the carry joint");
    const auto dropped = parts.availablePosition(body);
    require(dropped && glm::length(*dropped) < 1.5f,"Fetched limb was not dropped at home");
}
void scanCadenceAndOcclusion() {
    auto physics = simulation(); viewer::ZombieRagdollWorld ragdolls(physics);
    auto& parts = ragdolls.detachedParts(); viewer::CollisionWorld empty(viewer::Model{});
    auto dogs = dogAt();
    dogs.updateBehavior(.1f,{},empty,5,&parts);
    const auto handle = ragdolls.create({4,0,8},0,{}); ragdolls.sever(handle,RagdollPart::lowerLegLeft);
    dogs.updateBehavior(.1f,{},empty,9.99,&parts);
    require(dogs.behaviors[0].state == viewer::DogState::wandering,"Empty scans must still wait five seconds before retry");
    dogs.updateBehavior(.1f,{},empty,10,&parts);
    require(dogs.behaviors[0].state == viewer::DogState::fetching,"Next scheduled scan missed the new limb");
    viewer::Model wall; viewer::Primitive primitive;
    for (const auto p : {glm::vec3(-100,-10,6),{100,-10,6},{100,10,6},{-100,10,6}}) {
        viewer::Vertex v; v.position = p; primitive.vertices.push_back(v);
    }
    primitive.indices = {0,1,2,0,2,3}; wall.primitives.push_back(primitive);
    wall.draws.push_back({0,glm::mat4(1),"building_test_walls"});
    viewer::CollisionWorld blocked(wall);
    auto occluded = dogAt(); occluded.updateBehavior(.1f,{},blocked,5,&parts);
    require(occluded.behaviors[0].state == viewer::DogState::wandering,"A limb behind a wall must not trigger fetch");
    for (int i = 0; i < 9; ++i) {
        const auto extra = ragdolls.create({4,0,8+float(i)*.1f},0,{});
        ragdolls.sever(extra,RagdollPart::lowerArmLeft);
    }
    const auto visible = ragdolls.create({30,0,3},0,{}); ragdolls.sever(visible,RagdollPart::lowerArmLeft);
    occluded.updateBehavior(.1f,{},blocked,10,&parts);
    require(occluded.behaviors[0].state == viewer::DogState::wandering,
            "Scan must not test an eleventh visible limb after ten occluded candidates");
    ragdolls.destroy(handle);
    occluded.updateBehavior(.1f,{},blocked,15,&parts);
    require(occluded.behaviors[0].state == viewer::DogState::fetching,
            "Visible limb must be found once it enters the nearest ten candidates");
}
void droppedLimbHasSceneSupport() {
    auto physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::ZombieRagdollWorld ragdolls(physics);
    viewer::Model model; viewer::Primitive primitive;
    for (const auto p : {glm::vec3(-20,0,-20),{-20,0,20},{20,0,20},{20,0,-20}}) {
        viewer::Vertex v; v.position = p; primitive.vertices.push_back(v);
    }
    primitive.indices = {0,1,2,0,2,3}; model.primitives.push_back(primitive);
    model.draws.push_back({0,glm::mat4(1),"terrain"});
    viewer::CollisionWorld collision(model);
    viewer::PhysicsScene scene(physics,collision,{-20,-2,-20},{20,10,20});
    const auto handle = ragdolls.create({12,0,0},0,{}); ragdolls.sever(handle,RagdollPart::lowerArmLeft);
    std::vector<viewer::DetachedZombiePart> found;
    auto& parts = ragdolls.detachedParts(); parts.queryNear({12,0,0},3,10,found);
    require(found.size() == 1,"Support fixture must expose its detached arm");
    auto grip = parts.carry(found[0].body,{0,.7f,0});
    scene.ensureNear({-2,-2,-2},{2,3,2}); grip.reset();
    for (int step = 0; step < 240; ++step) physics->step(1.f/60);
    const auto dropped = parts.availablePosition(found[0].body);
    require(dropped && dropped->y >= -.01f && dropped->y < .5f,
            "Dropped detached limb must settle on scene ground away from its corpse's local floor");
}
void interruptionAndRemoval() {
    for (int action = 0; action < 3; ++action) {
        auto physics = simulation(); viewer::ZombieRagdollWorld ragdolls(physics);
        const auto handle = ragdolls.create({4,0,3},0,{}); ragdolls.sever(handle,RagdollPart::lowerArmLeft);
        auto& parts = ragdolls.detachedParts(); viewer::CollisionWorld empty(viewer::Model{});
        auto dogs = dogAt(); dogs.updateBehavior(.1f,{},empty,5,&parts);
        double now = 5;
        for (int step = 0; step < 30 && dogs.behaviors[0].fetch && !dogs.behaviors[0].fetch->grip; ++step) {
            now += .1;
            dogs.updateBehavior(.1f,{},empty,now,&parts);
        }
        require(dogs.behaviors[0].fetch && dogs.behaviors[0].fetch->grip,"Fixture must pick up its nearby limb");
        const auto body = dogs.behaviors[0].fetch->part;
        if (action == 0) {
            dogs.damage(0,100);
            require(!physics->isGrabbed(body) && !dogs.behaviors[0].fetch,"Dog death must release its carry joint");
        } else if (action == 1) {
            const auto player = glm::vec3(dogs.instances[0].root[3])+glm::vec3(dogs.instances[0].root[2])*20.f;
            dogs.updateBehavior(.1f,{{player}},empty,now+.1,&parts);
            require(!physics->isGrabbed(body) && dogs.behaviors[0].state == viewer::DogState::chasing,
                    "Normal perception must interrupt fetch and drop the limb");
        } else {
            ragdolls.destroy(handle);
            dogs.updateBehavior(.1f,{},empty,now+.1,&parts);
            require(!dogs.behaviors[0].fetch || !dogs.behaviors[0].fetch->grip,
                    "Removed corpse must not leave an invalid carry joint");
        }
    }
}
} // namespace
int main() {
    try {
        queriesAndLifetime(); assemblySnap(); fetchHome(); scanCadenceAndOcclusion();
        droppedLimbHasSceneSupport(); interruptionAndRemoval();
        std::cout << "Dog fetch passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
