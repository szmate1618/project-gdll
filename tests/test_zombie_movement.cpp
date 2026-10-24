#include "zombie_movement.hpp"
#include "zombie_update_profile.hpp"
#include "zombie_chase.hpp"
#include "zombie_layer.hpp"
#include "box_layer.hpp"
#include "door_layer.hpp"
#include "physics_world.hpp"
#include "pole_layer.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <tuple>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
float distance(glm::vec3 a, glm::vec3 b) { return glm::length(glm::vec2(a.x - b.x, a.z - b.z)); }

void quad(viewer::Model& model, const std::string& name,
          glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d,
          bool detailed = false, bool low = false, glm::mat4 transform = glm::mat4(1)) {
    viewer::Primitive primitive;
    for (const auto p : {a, b, c, d}) {
        viewer::Vertex vertex;
        vertex.position = p;
        vertex.normal = glm::normalize(glm::cross(b - a, c - a));
        primitive.vertices.push_back(vertex);
    }
    primitive.indices = {0, 1, 2, 0, 2, 3};
    model.draws.push_back({model.primitives.size(), transform, name, detailed ? 120.f : 0, low,
                           detailed ? "building_test" : ""});
    model.primitives.push_back(std::move(primitive));
}
viewer::Model floor() {
    viewer::Model model;
    quad(model, "terrain", {-100, 0, -100}, {-100, 0, 100}, {100, 0, 100}, {100, 0, -100});
    return model;
}
void wall(viewer::Model& model, const std::string& name = "building_test_walls",
          float bottom = 0, float top = 3) {
    quad(model, name, {0, bottom, -20}, {0, bottom, 20}, {0, top, 20}, {0, top, -20});
}
void clear(const viewer::CollisionWorld& world, glm::vec3 feet) {
    for (const auto& c : world.contacts(feet, .3f, 1.8f))
        require(c.terrain || c.depth < .0002f, "Accepted feet must not penetrate a scene obstacle");
}

void walkingContactRegions() {
    // Compare against the original, complete contact query. Only contacts which
    // movement would ignore may disappear; blockers and their geometry must match.
    const auto key=[](const viewer::Contact& c) {
        return std::make_tuple(c.depth,c.normal.x,c.normal.y,c.normal.z,c.point.x,c.point.y,c.point.z,
                               c.terrain,c.terrainSupport);
    };
    const auto blockers=[&](std::vector<viewer::Contact> contacts,glm::vec3 feet) {
        contacts.erase(std::remove_if(contacts.begin(),contacts.end(),[&](const auto& c) {
            return c.depth<=.0001f || (c.terrainSupport && c.normal.y>=.70710678f) ||
                (c.normal.y>.7f && c.point.y<=feet.y+.0021f);
        }),contacts.end());
        std::sort(contacts.begin(),contacts.end(),[&](const auto& a,const auto& b) { return key(a)<key(b); });
        return contacts;
    };
    std::size_t omitted=0;
    const auto physics=std::make_shared<viewer::PhysicsWorld>();
    viewer::BoxLayer boxes(physics);
    boxes.add({1,.5f,0},{1,1,1});
    for(float shift:{0.f,3000.f,20000.f}) for(float grade:{0.f,.2f,-.4f,.49f,1.1f})
    for(const auto* name:{"terrain","roads","platform"}) {
        viewer::Model model;
        const auto point=[&](float x,float z) { return glm::vec3(x+shift,x*grade,z-shift); };
        quad(model,name,point(-4,-4),point(-4,4),point(4,4),point(4,-4));
        quad(model,"wall",point(-1,-4),point(-1,4),point(-1,4)+glm::vec3(0,2,0),point(-1,-4)+glm::vec3(0,2,0));
        // Downward overhead geometry remains a separate blocker even when the
        // supporting terrain underneath qualifies for reuse.
        quad(model,"ceiling",point(-3,-3)+glm::vec3(0,1.6f,0),
             point(3,-3)+glm::vec3(0,1.6f,0),point(3,3)+glm::vec3(0,1.6f,0),
             point(-3,3)+glm::vec3(0,1.6f,0));
        viewer::CollisionWorld world(model,{{point(2,1),.15f,4}});
        world.setBoxes(&boxes);
        viewer::WalkingGroundCache ground;
        viewer::ContactCandidates candidates;
        for(int i=0;i<400;++i) {
            const float x=-4.01f+(i%20)*.421f,z=-4.01f+(i/20)*.421f;
            const auto hit=world.walkingGroundAt(x+shift,z-shift,-100,100,&ground);
            if(!hit) continue;
            for(float radius:{.1f,.3f,.5f}) for(float dy:{0.f,.001f,-.01f,-.5f,.5f}) {
                const glm::vec3 feet{x+shift,hit->height+dy,z-shift};
                const auto expected=world.contacts(feet,radius,1.8f,.002f,.05f);
                std::vector<viewer::Contact> actual;
                world.visitContacts(feet,radius,1.8f,.002f,.05f,{&actual,[](void* target,const auto& c) {
                    static_cast<std::vector<viewer::Contact>*>(target)->push_back(c);return true;
                }},&candidates,viewer::ContactPolicy::walking);
                omitted+=expected.size()-actual.size();
                const auto a=blockers(expected,feet),b=blockers(actual,feet);
                require(a.size()==b.size(),"Walking support proof omitted an obstacle");
                for(std::size_t j=0;j<a.size();++j)
                    require(key(a[j])==key(b[j]),"Walking support proof changed blocking geometry");
            }
        }
    }
    require(omitted>100,"Walking region certificates were never used");
}

void walkingRegionWallPadding() {
    auto model=floor();
    // A padded wall just below the feet can still produce a shallow-angle
    // blocker for a small capsule. A below-feet bound alone is insufficient.
    wall(model,"building_test_walls",-2,-.001f);
    const viewer::CollisionWorld world(model);
    for(const auto settings:{glm::vec2(.1f,.05f),glm::vec2(.3f,.5f)}) {
        const float radius=settings.x,padding=settings.y;
        const glm::vec3 feet{-radius*1.06f,0,0};
        const auto blocking=[&](const viewer::Contact& c) {
            return c.depth>.0001f && !(c.terrainSupport && c.normal.y>=.70710678f) &&
                !(c.normal.y>.7f && c.point.y<=feet.y+.0021f);
        };
        bool expected=false,actual=false;
        for(const auto& c:world.contacts(feet,radius,1.8f,.002f,padding)) expected=expected||blocking(c);
        struct Context { bool* actual;decltype(blocking)* predicate; } context{&actual,&blocking};
        viewer::ContactCandidates cache;
        world.visitContacts(feet,radius,1.8f,.002f,padding,{&context,[](void* target,const viewer::Contact& c) {
            auto& state=*static_cast<Context*>(target);*state.actual=*state.actual||(*state.predicate)(c);return true;
        }},&cache,viewer::ContactPolicy::walking);
        require(expected && actual,"Walking certificate hid a padded below-feet wall contact");
    }
}

void thinWallsSlidingAndCorners() {
    auto model = floor();
    wall(model);
    const viewer::CollisionWorld world(model);
    for (const float side : {-1.f, 1.f}) {
        const auto move = viewer::moveZombieBody({side * 2, 0, 0}, {-side * 4, 0, 0}, world);
        require(move.feet.x * side >= .35f && move.feet.x * side < .36f,
                "Large movement must retain 5 cm extra wall clearance on either side");
        clear(world, move.feet);
    }
    const auto slid = viewer::moveZombieBody({-1, 0, -1}, {1, 0, 1}, world);
    require(slid.feet.x <= -.35f && slid.feet.z > .9f && slid.travel <= 2.83f,
            "Oblique movement must slide along a wall without gaining speed");
    quad(model, "building_test_walls", {-20, 0, 0}, {20, 0, 0}, {20, 3, 0}, {-20, 3, 0});
    const viewer::CollisionWorld corner(model);
    const auto stopped = viewer::moveZombieBody({-1, 0, -1}, {2, 0, 2}, corner);
    require(stopped.feet.x <= -.35f && stopped.feet.z <= -.35f, "Inside corners must retain wall clearance");
    clear(corner, stopped.feet);
    // Non-identity scene nodes must be applied exactly once, as in rendering.
    auto transformed = floor();
    const auto node = glm::translate(glm::mat4(1), {4, 0, 0}) *
        glm::rotate(glm::mat4(1), glm::radians(90.f), {0, 1, 0});
    quad(transformed, "building_test_walls", {0, 0, -5}, {0, 0, 5}, {0, 3, 5}, {0, 3, -5}, false, false, node);
    const viewer::CollisionWorld transformedWorld(transformed);
    require(viewer::moveZombieBody({4, 0, -2}, {4, 0, 2}, transformedWorld).feet.z <= -.35f,
            "Node-transformed collision must match the rendered wall");
}

void polesFencesGuardrailsAndTrunks() {
    const auto poles = viewer::buildPoleBatches({{"test", {0, 0, 0}, 7, .2f}});
    const viewer::CollisionWorld poleWorld(poles);
    const auto pole = viewer::moveZombieBody({-2, 0, 0}, {2, 0, 0}, poleWorld);
    require(pole.feet.x < -.39f && pole.feet.x > -.41f, "Mapped pole columns must block zombie bodies");
    const viewer::CollisionWorld trunkWorld(viewer::Model{}, {{{0, 0, 0}, .15f, 6}});
    require(viewer::moveZombieBody({-2, 0, 0}, {2, 0, 0}, trunkWorld).feet.x <= -.45f,
            "Tree trunks must also participate in shared world collision");
    auto fences = floor();
    quad(fences, "fences_wire", {0, 0, -5}, {0, 0, -.6f}, {0, 1.5f, -.6f}, {0, 1.5f, -5});
    quad(fences, "fences_wire", {0, 0, .6f}, {0, 0, 5}, {0, 1.5f, 5}, {0, 1.5f, .6f});
    fences.materials.emplace_back();
    fences.materials.back().alphaMode = "MASK";
    fences.materials.back().doubleSided = true;
    fences.primitives[1].material = fences.primitives[2].material = 0;
    for (auto& draw : fences.draws) draw.fence = draw.name == "fences_wire";
    const viewer::CollisionWorld fenceWorld(fences);
    const auto fenced = viewer::moveZombieBody({-2, 0, 2}, {2, 0, 2}, fenceWorld);
    require(fenced.feet.x <= -.3f && fenced.feet.x > -.31f,
            "Fence panels must retain their original clearance, including alpha-masked panels");
    require(viewer::moveZombieBody({-2, 0, 0}, {2, 0, 0}, fenceWorld).feet.x > 1.99f,
            "An actual fence entrance must remain traversable");
    viewer::removeFenceDraws(fences);
    const viewer::CollisionWorld noFences(fences);
    require(viewer::moveZombieBody({-2, 0, 2}, {2, 0, 2}, noFences).feet.x > 1.99f,
            "Disabled fence draws must not leave zombie collision behind");
    auto rails = floor();
    wall(rails, "guardrails_metal", .55f, .85f);
    const viewer::CollisionWorld railWorld(rails);
    const auto railed = viewer::moveZombieBody({-2, 0, 0}, {2, 0, 0}, railWorld);
    require(railed.feet.x <= -.3f && railed.feet.x > -.31f,
            "A low guardrail must retain its original clearance without being climbed");
    clear(railWorld, viewer::moveZombieBody({-2, 0, 0}, {2, 0, 0}, railWorld).feet);
}

void boxesAndDoorPoses() {
    auto physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::BoxLayer boxes(physics);
    boxes.add({0, .5f, 0}, {1, 1, 1});
    auto model = floor();
    viewer::CollisionWorld boxWorld(model);
    boxWorld.setBoxes(&boxes);
    const auto before = boxes.pose(0).center;
    const auto blocked = viewer::moveZombieBody({-2, 0, 0}, {2, 0, 0}, boxWorld);
    require(blocked.feet.x <= -.8f && boxes.pose(0).center == before && boxes.velocity(0) == glm::vec3(0),
            "Boxes must block zombie movement without being pushed or mutated by queries");
    boxWorld.setBoxes(nullptr);
    require(viewer::moveZombieBody({-2, 0, 0}, {2, 0, 0}, boxWorld).feet.x > 1.99f,
            "Detaching dynamic boxes must remove their collision immediately");

    quad(model, "building_test_walls", {-3, 0, 0}, {3, 0, 0}, {3, 3, 0}, {-3, 3, 0}, true, true);
    quad(model, "building_test_detailed_walls", {-3, 0, 0}, {-.5f, 0, 0}, {-.5f, 3, 0}, {-3, 3, 0}, true);
    quad(model, "building_test_detailed_walls", {.5f, 0, 0}, {3, 0, 0}, {3, 3, 0}, {.5f, 3, 0}, true);
    quad(model, "building_test_detailed_walls", {-.5f, 2.1f, 0}, {.5f, 2.1f, 0}, {.5f, 3, 0}, {-.5f, 3, 0}, true);
    quad(model, "building_test_doors", {-.5f, 0, 0}, {.5f, 0, 0}, {.5f, 2.1f, 0}, {-.5f, 2.1f, 0}, true);
    viewer::DoorLayer doors(viewer::prepareDoorPanels(model), physics);
    const viewer::CollisionWorld doorWorld(model, {}, &doors);
    require(doors.count() == 1, "Fixture must have a physical door panel");
    const auto closed = viewer::moveZombieBody({0, 0, 2}, {0, 0, -2}, doorWorld);
    require(closed.feet.z >= .319f && closed.feet.z < .33f && doors.activeCount() == 0,
            "A closed door must block without collision queries activating its physics");
    const auto& panel = doors.panel(0);
    const auto edge = panel.center + glm::cross(glm::vec3(0, 1, 0), panel.outward) * (panel.halfExtents.x * .9f);
    for (int step = 0; step < 180; ++step) {
        const auto point = glm::vec3(doors.transform(0) * glm::inverse(panel.closedTransform) * glm::vec4(edge, 1));
        doors.applyForce(0, glm::cross(glm::vec3(0, -1, 0), point - panel.hinge) * 100.f, point);
        physics->step(1.f / 60);
    }
    require(viewer::moveZombieBody({0, 0, 2}, {0, 0, -2}, doorWorld).feet.z < -1.99f,
            "An opened physical door must reveal the actual doorway without rebuilding collision");
    require(viewer::moveZombieBody({.12f, 0, 2}, {.12f, 0, -2}, doorWorld).feet.z < -1.99f,
            "A 1 m doorway must retain passage off center with the extra wall clearance");
    glm::vec3 nearJamb{.150861f, 0, .044788f};
    for (int step = 0; step < 60; ++step) {
        const auto desired = nearJamb + glm::normalize(glm::vec3(0, 0, -12) - nearJamb) * .05f;
        const auto accepted = viewer::moveZombieBody(nearJamb, desired, doorWorld);
        require(distance(nearJamb, accepted.feet) <= .0501f, "Jamb sliding must preserve the speed bound");
        nearJamb = accepted.feet;
    }
    require(nearJamb.z < -1, "A glancing approach must slide past a cleared doorway jamb without wedging");
    const auto current = doors.colliders({-3, 0, -3}, {3, 3, 3}).at(0);
    const auto axis = current.rotation * glm::vec3(0, 0, 1);
    auto from = current.center + axis * 1.2f, to = current.center - axis * 1.2f;
    from.y = to.y = 0;
    const auto panelStop = viewer::moveZombieBody(from, to, doorWorld);
    require(glm::dot(panelStop.feet - current.center, axis) > .3f,
            "The rotated open panel must still block at its current physical pose");
}

void contactCandidateReuse() {
    auto model=floor();
    wall(model);
    const auto physics=std::make_shared<viewer::PhysicsWorld>();
    viewer::BoxLayer boxes(physics);
    boxes.add({-2,.5f,0},{.5f,1,.5f});
    viewer::CollisionWorld world(model,{{{-1,0,1},.15f,5}});
    world.setBoxes(&boxes);
    viewer::ContactCandidates cache;
    const auto key=[](const viewer::Contact& c) {
        return std::make_tuple(c.depth,c.normal.x,c.normal.y,c.normal.z,c.point.x,c.point.y,c.point.z,
                               c.terrain,c.terrainSupport);
    };
    const auto compare=[&](const viewer::CollisionWorld& scene,glm::vec3 feet,float skin=.002f) {
        auto expected=scene.contacts(feet,.3f,1.8f,skin,.05f);
        std::vector<viewer::Contact> actual;
        scene.visitContacts(feet,.3f,1.8f,skin,.05f,{&actual,[](void* target,const viewer::Contact& c) {
            static_cast<std::vector<viewer::Contact>*>(target)->push_back(c);return true;
        }},&cache);
        const auto less=[&](const auto& a,const auto& b) { return key(a)<key(b); };
        std::sort(expected.begin(),expected.end(),less);std::sort(actual.begin(),actual.end(),less);
        require(actual.size()==expected.size(),"Candidate cache lost or duplicated contacts");
        for(std::size_t i=0;i<actual.size();++i)
            require(key(actual[i])==key(expected[i]),"Candidate cache changed contact geometry");
        std::size_t visited=0;
        scene.visitContacts(feet,.3f,1.8f,skin,.05f,{&visited,[](void* target,const viewer::Contact&) {
            ++*static_cast<std::size_t*>(target);return false;
        }},&cache);
        require(visited==(actual.empty()?0u:1u),"Any-contact traversal failed to stop at its first contact");
    };
    for(int i=0;i<300;++i) compare(world,{-3+i*.02f,0,.1f});
    compare(world,{-2,0,0});
    boxes.setPose(0,{-2,.5f,0},{1,0,0,0});compare(world,{-2,0,0});
    boxes.setPose(0,{2,.5f,0},{1,0,0,0});compare(world,{-2,0,0});
    compare(world,{-2,.2f,0},.01f);
    auto dense=floor();
    for(int i=0;i<70;++i) wall(dense);
    std::vector<viewer::TrunkCollider> trunks(40,{{0,0,0},.15f,5});
    const viewer::CollisionWorld crowded(dense,trunks);
    compare(crowded,{-.2f,0,0});compare(crowded,{-.21f,0,0});
    world=viewer::CollisionWorld(floor());
    compare(world,{-.2f,0,0});
    world=viewer::CollisionWorld(viewer::Model{});
    compare(world,{-.2f,0,0});
    viewer::DoorPanel panel;
    panel.hinge={0,0,0};panel.center={.5f,1,0};panel.halfExtents={.5f,1,.04f};
    panel.outward={0,0,1};panel.closedTransform=glm::translate(glm::mat4(1),panel.center);
    viewer::DoorLayer doors({panel},physics);
    const viewer::CollisionWorld doorWorld(floor(),{},&doors);
    compare(doorWorld,{-3,0,0}); // Certified empty hinge patch.
    compare(doorWorld,{.5f,0,0}); // Entering a populated patch must restore checks.
    compare(doorWorld,{0,0,.5f}); // No closed-panel contact, but a potential sweep.
    doors.setAngle(0,glm::half_pi<float>());
    compare(doorWorld,{0,0,.5f});compare(doorWorld,{0,0,-.5f});
    compare(doorWorld,{-3,0,0});
}

void cachedLiveSupport() {
    const auto physics=std::make_shared<viewer::PhysicsWorld>();
    viewer::BoxLayer boxes(physics);
    boxes.add({0,.1f,0},{2,.2f,2});
    viewer::CollisionWorld world(floor());
    viewer::WalkingGroundCache cache;
    world.setBoxes(&boxes);
    const auto check = [&](float expected) {
        const auto hit=world.walkingGroundAt(0,0,-1,1,&cache);
        require(hit && std::abs(hit->height-expected)<1e-5f,"Static ground cache concealed live box support changes");
    };
    check(.2f);
    boxes.setPose(0,{0,.2f,0},{1,0,0,0}); check(.3f);
    boxes.setPose(0,{5,.2f,0},{1,0,0,0}); check(0);
    boxes.setPose(0,{0,.1f,0},{1,0,0,0}); check(.2f);
    world.setBoxes(nullptr); check(0);
}

void descendingSupportUsesOneContactQuery() {
    viewer::Model model;
    quad(model,"terrain",{-4,-.4f,-4},{-4,-.4f,4},{4,.4f,4},{4,.4f,-4});
    const viewer::CollisionWorld world(model);
    viewer::zombie_profile::Profile profile;
    viewer::ZombieMovement movement;
    {
        viewer::zombie_profile::Capture capture(&profile);
        movement=viewer::moveZombieBody({1,.1f,0},{.98f,.1f,0},world);
    }
    require(std::abs(movement.feet.x-.98f)<1e-6f && std::abs(movement.feet.y-.098f)<1e-6f,
            "Descending support must follow the rendered slope");
    const auto queries=profile.stages[std::size_t(viewer::zombie_profile::Stage::contacts)].calls;
    require(queries==1,"An already-clear supported pose repeated the identical contact query");
}

void cachedMovementOnSlopeAndStep() {
    auto model=floor();
    quad(model,"terrain",{-4,-.4f,-4},{-4,-.4f,4},{4,.4f,4},{4,.4f,-4});
    quad(model,"platform",{0,.2f,-1},{0,.2f,1},{1,.2f,1},{1,.2f,-1});
    quad(model,"platform",{0,0,-1},{0,0,1},{0,.2f,1},{0,.2f,-1});
    const viewer::CollisionWorld world(model);
    viewer::WalkingGroundCache cache;
    glm::vec3 position{-3,0,0};
    for(int i=0;i<300;++i) {
        const auto desired=position+glm::vec3(.02f,0,0);
        const auto plain=viewer::moveZombieBody(position,desired,world);
        const auto cached=viewer::moveZombieBody(position,desired,world,nullptr,0,&cache);
        require(plain.feet==cached.feet && plain.travel==cached.travel,
                "Cached support changed movement across slopes, seams or step edges");
        position=plain.feet;
    }
    require(position.x>2.9f,"Slope/step fixture failed to traverse its surfaces");
}

void blockedBehaviorAndAnimation() {
    auto model = floor();
    wall(model);
    const viewer::CollisionWorld world(model);
    viewer::ZombieLayer layer;
    layer.assets.emplace_back();
    viewer::ZombieInstance instance;
    instance.restRoot = glm::translate(glm::mat4(1), {-1, 0, 0});
    instance.chaseNormalization = glm::scale(glm::mat4(1), {-1.2f, 1.2f, 1.2f});
    instance.chaseNormalization[2].x = .2f;
    instance.transform = instance.restTransform = instance.restRoot * instance.chaseNormalization;
    layer.instances.push_back(instance);
    auto& behavior = layer.behaviors.create(0, 0);
    behavior.state = viewer::ZombieState::chasing;
    behavior.target = {10, 0, 0};
    for (int step = 0; step < 20; ++step) layer.chase(.1f, {300, 0, 0}, world);
    const auto& stopped = layer.instances[0];
    const auto clock = stopped.walkSeconds;
    require(stopped.restRoot[3].x <= -.35f && stopped.restRoot[3].x > -.36f && !stopped.walking,
            "Offscreen pursuit must stop at the world obstacle and leave walking animation");
    require(layer.behaviors.find(0)->state == viewer::ZombieState::chasing &&
            layer.behaviors.find(0)->target == glm::vec3(10, 0, 0),
            "World blockage must retain state and target rather than report arrival");
    layer.chase(.1f, {300, 0, 0}, world);
    require(layer.instances[0].walkSeconds == clock, "A blocked zombie must not advance its locomotion clock");
    const auto normalization = glm::inverse(stopped.restRoot) * stopped.transform;
    for (int column = 0; column < 4; ++column)
        require(glm::length(normalization[column] - instance.chaseNormalization[column]) < .0001f,
                "Scene collision must preserve asset scale, reflection and shear");
}

void boundedExternalMovement() {
    const viewer::CollisionWorld world(floor());
    const auto capped = viewer::moveZombieBody({0, 0, 0}, {100000, 0, 0}, world);
    require(capped.feet.x > 200 && capped.feet.x < 205,
            "Huge external movement must stop at the segment budget instead of enlarging steps");
    const viewer::CollisionWorld empty(viewer::Model{});
    const auto invalid = viewer::moveZombieBody({0, 0, 0}, {std::numeric_limits<float>::infinity(), 0, 0}, empty);
    require(invalid.feet == glm::vec3(0) && invalid.travel == 0, "Invalid destinations must not corrupt placement");
}

void crowdAgainstWallAndDeterminism() {
    auto model = floor();
    wall(model);
    const viewer::CollisionWorld world(model);
    constexpr std::size_t count = 1000;
    std::vector<glm::vec3> positions;
    for (int z = 0; z < 20; ++z)
        for (int x = 0; x < 50; ++x) positions.emplace_back(-.4f - x * .65f, 0, (z - 10) * .65f);
    std::vector<viewer::ContactCandidates> contactCaches(count);
    std::vector<viewer::WalkingGroundCache> groundCaches(count);
    viewer::ZombieCrowd a, b;
    a.reset(count); b.reset(count);
    for (std::size_t id = 0; id < count; ++id) a.update(id, positions[id]);
    for (std::size_t id = count; id-- > 0;) b.update(id, positions[id]);
    const auto begin = std::chrono::steady_clock::now();
    for (int step = 0; step < 60; ++step) {
        a.beginStep(); b.beginStep();
        for (std::size_t id = 0; id < count; ++id) {
            const auto desired = positions[id] + glm::normalize(glm::vec3(10, 0, 0) - positions[id]) * .15f;
            const auto accepted = viewer::moveZombieBody(positions[id], desired, world, &a, id);
            const auto replay = viewer::moveZombieBody(positions[id], desired, world, &b, id, &groundCaches[id],&contactCaches[id]);
            require(accepted.feet == replay.feet, "World/crowd movement must repeat across bucket insertion histories");
            require(accepted.feet.x <= -.35f && distance(positions[id], accepted.feet) <= .1502f,
                    "World/crowd correction must not cross a wall or add speed");
            positions[id] = accepted.feet;
            a.update(id, accepted.feet); b.update(id, accepted.feet);
        }
        require(a.stats().neighborChecks <= count * viewer::ZombieCrowd::neighborBudget,
                "Substeps must share one neighbor budget per zombie movement");
    }
    for (std::size_t id = 0; id < count; ++id) {
        clear(world, positions[id]);
        for (std::size_t other = id + 1; other < count; ++other)
            require(distance(positions[id], positions[other]) >= .5998f,
                    "Wall collision correction must preserve separation throughout the crowded queue");
    }
    std::cout << "1,000 zombies against a wall, 60 steps, two replays: "
              << std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count() << " s\n";
}
}

int main() {
    try {
        walkingContactRegions();
        walkingRegionWallPadding();
        thinWallsSlidingAndCorners();
        polesFencesGuardrailsAndTrunks();
        boxesAndDoorPoses();
        contactCandidateReuse();
        cachedLiveSupport();
        descendingSupportUsesOneContactQuery();
        cachedMovementOnSlopeAndStep();
        blockedBehaviorAndAnimation();
        boundedExternalMovement();
        crowdAgainstWallAndDeterminism();
        std::cout << "Zombie world movement tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Zombie world movement failed: " << e.what() << '\n';
        return 1;
    }
}
