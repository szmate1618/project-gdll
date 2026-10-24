#include "door_layer.hpp"
#include "physics_world.hpp"
#include "model_visibility.hpp"
#include "zombie_ragdoll.hpp"

#include <box3d/box3d.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

viewer::Model fixture(glm::vec3 normal = {0, 0, 1}, glm::vec3 offset = {}, glm::mat4 node = glm::mat4(1)) {
    viewer::Model model;
    viewer::Primitive primitive;
    const auto right = glm::cross(glm::vec3(0, 1, 0), normal);
    const auto inverse = glm::inverse(node);
    for (const auto xy : {glm::vec2(0, 0), glm::vec2(1, 0), glm::vec2(1, 2.1f), glm::vec2(0, 2.1f)}) {
        viewer::Vertex v;
        v.position = glm::vec3(inverse * glm::vec4(offset + right * xy.x + glm::vec3(0, xy.y, 0), 1));
        v.normal = glm::transpose(glm::mat3(node)) * normal;
        primitive.vertices.push_back(v);
    }
    primitive.indices = {0, 1, 2, 0, 2, 3};
    model.primitives.push_back(primitive);
    model.draws.push_back({0, node, "building_42_doors", 120, false, "building_42"});
    return model;
}

void extractionAndActivation() {
    auto model = fixture({0, 0, 1}, {14, 3, -8}, glm::rotate(glm::mat4(1), -glm::half_pi<float>(), glm::vec3(1, 0, 0)));
    // Add a second disconnected panel, using deliberately unwelded triangles.
    const auto original = model.primitives[0];
    for (const auto index : original.indices) {
        auto vertex = original.vertices[index];
        vertex.position += glm::vec3(10, 0, 0);
        model.primitives[0].indices.push_back(static_cast<std::uint32_t>(model.primitives[0].vertices.size()));
        model.primitives[0].vertices.push_back(vertex);
    }
    const auto panels = viewer::prepareDoorPanels(model);
    require(panels.size() == 2 && model.draws.size() == 2, "Each disconnected panel must become an independent door");
    require(glm::distance(panels[0].hinge, glm::vec3(14, 4.1f, -8)) < .001f, "Scene node transform must be applied exactly once");
    require(model.draws[1].detailGroup == "building_42" && model.draws[1].detailDistance == 120,
            "Split doors must retain building LoD membership");
    auto world = std::make_shared<viewer::PhysicsWorld>();
    {
        viewer::DoorLayer doors(panels, world);
        require(doors.count() == 2 && doors.activeCount() == 0, "Loading must not instantiate physics across the town");
        doors.activateNear(panels[0].hinge, 2);
        require(doors.activeCount() == 1, "Spatial activation must leave distant doors inactive");
        doors.activateNear(panels[0].hinge, 2);
        require(doors.activeCount() == 1, "Repeated proximity queries must not duplicate bodies");
        for (int i = 0; i < 60; ++i) world->step(1.0f / 60);
        require(std::abs(doors.angle(0)) < .001f, "Gravity must not open a vertical hinge");
        const auto p = glm::vec3(doors.transform(0) * glm::vec4(original.vertices[0].position, 1));
        require(glm::distance(p, glm::vec3(14, 3, -8)) < .001f, "Activation must preserve the drawn panel without jumping");
        require(b3World_GetCounters(world->id()).bodyCount == 2, "One panel must own a body and an anchor");
    }
    require(b3World_GetCounters(world->id()).bodyCount == 0, "Destroying a door layer must clean a surviving shared world");
}

void forceAndLimits() {
    for (const auto normal : {glm::vec3(0, 0, 1), glm::vec3(0, 0, -1), glm::vec3(1, 0, 0),
                              glm::vec3(-1, 0, 0), glm::normalize(glm::vec3(1, 0, 2))}) {
        auto model = fixture(normal);
        const auto panels = viewer::prepareDoorPanels(model);
        auto world = std::make_shared<viewer::PhysicsWorld>();
        viewer::DoorLayer doors(panels, world);
        const auto initial = panels[0];
        const auto right = glm::cross(glm::vec3(0, 1, 0), normal);
        const auto freeEdge = initial.center + right * .45f;
        doors.applyImpulse(0, normal * 20.0f, freeEdge);
        for (int frame = 0; frame < 45; ++frame) world->step(1.0f / 60);
        require(doors.angle(0) > .2f, "An outward impulse must rotate the panel on every facade orientation");
        const auto closedInverse = glm::inverse(initial.closedTransform);
        const auto pivot = glm::vec3(doors.transform(0) * closedInverse * glm::vec4(initial.hinge, 1));
        require(glm::distance(pivot, initial.hinge) < .01f, "The hinge must stay attached to the wall");
        const auto movedFreeEdge = glm::vec3(doors.transform(0) * closedInverse * glm::vec4(freeEdge, 1));
        require(glm::dot(movedFreeEdge - freeEdge, normal) > .1f, "The free edge must swing out of the building");
        for (int frame = 0; frame < 180; ++frame) {
            const auto point = glm::vec3(doors.transform(0) * closedInverse * glm::vec4(freeEdge, 1));
            const auto radial = point - initial.hinge;
            const auto tangent = glm::cross(glm::vec3(0, -1, 0), radial);
            doors.applyForce(0, tangent * 100.0f, point);
            world->step(1.0f / 60);
            require(doors.angle(0) <= glm::radians(110.0f) + .03f, "Outward rotation must stop at the hinge limit");
        }
        require(doors.angle(0) > glm::radians(100.0f), "Sustained force must reach the outward stop");
        for (int frame = 0; frame < 240; ++frame) {
            const auto point = glm::vec3(doors.transform(0) * closedInverse * glm::vec4(freeEdge, 1));
            doors.applyForce(0, glm::cross(glm::vec3(0, 1, 0), point - initial.hinge) * 100.0f, point);
            world->step(1.0f / 60);
            require(doors.angle(0) >= -.03f, "An inward force must never open the panel inside the building");
        }
        require(std::abs(doors.angle(0)) < .03f, "Inward force must return the door to the closed stop");
        bool rejected = false;
        try { doors.applyForce(0, {std::numeric_limits<float>::quiet_NaN(), 0, 0}, {}); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "Nonfinite force must not enter the physics engine");
    }
}

void localAndWideColliderQueries() {
    std::vector<viewer::DoorPanel> panels;
    for(int x=-3;x<=3;++x) for(int z=-20;z<=20;++z) {
        viewer::DoorPanel p;
        p.draw=panels.size();p.hinge={x*40.f,0,z*40.f};
        p.center=p.hinge+glm::vec3(.5f,1,0);p.halfExtents={.5f,1,.04f};
        p.outward={0,0,1};p.closedTransform=glm::translate(glm::mat4(1),p.center);
        panels.push_back(p);
    }
    const auto physics=std::make_shared<viewer::PhysicsWorld>();
    viewer::DoorLayer doors(panels,physics);
    constexpr std::size_t middle=3*41+20;
    const auto local=doors.colliders({-.2f,-1,-.2f},{.2f,3,.2f});
    require(local.size()==1 && local.front().door==middle,"Local lookup must select only the nearby hinge sweep");
    const auto wide=doors.colliders({-100,-1,-1000},{100,3,1000});
    require(wide.size()==5*41,"Wide sparse lookup lost a swept panel");
    for(std::size_t i=1;i<wide.size();++i)
        require(wide[i-1].door<wide[i].door,"Local/wide query order must remain stable");
    doors.setAngle(middle,glm::half_pi<float>());
    const auto opened=doors.colliders({-.2f,-1,-.2f},{.2f,3,.2f});
    require(opened.size()==1 && glm::distance(opened.front().center,local.front().center)>.1f,
            "Local hinge candidates must use the panel's current open pose");
    require(doors.colliders({10,-1,10},{11,3,11}).empty(),"An empty local query must not acquire distant panels");
}

void foundationSeamsDoNotShortenCollider() {
    auto model = fixture();
    auto& mesh = model.primitives[0];
    // A sloping lower edge and the horizontal first-window sill are both
    // triangulation constraints in generated door panels.
    mesh.vertices[1].position.y = .3f;
    auto a = mesh.vertices[0], b = mesh.vertices[1];
    a.position.y = b.position.y = .9f;
    mesh.vertices.push_back(a);
    mesh.vertices.push_back(b);
    mesh.indices = {0, 1, 5, 0, 5, 4, 4, 5, 2, 4, 2, 3};
    const auto panels = viewer::prepareDoorPanels(model);
    require(panels.size() == 1, "A foundation seam must not split a door into independent bodies");
    require(std::abs(panels[0].halfExtents.y - .85f) < .001f,
            "Collider must start above the sloping base, rather than the internal sill-height seam");
    require(model.primitives.size() == 1, "Superseded batched door geometry must not remain for GPU upload");
}

void bodyCollisionOpensDoor() {
    auto model = fixture();
    auto world = std::make_shared<viewer::PhysicsWorld>();
    viewer::DoorLayer doors(viewer::prepareDoorPanels(model), world);
    doors.activateNear({0, 1, 0});
    auto bodyDef = b3DefaultBodyDef();
    bodyDef.type = b3_dynamicBody;
    bodyDef.position = {.8, 1, -.8};
    bodyDef.gravityScale = 0;
    const auto body = b3CreateBody(world->id(), &bodyDef);
    auto shapeDef = b3DefaultShapeDef();
    shapeDef.density = 800;
    const auto hull = b3MakeBoxHull(.15f, .35f, .15f);
    b3CreateHullShape(body, &shapeDef, &hull.base);
    b3Body_SetLinearVelocity(body, {0, 0, 4});
    for (int frame = 0; frame < 60; ++frame) world->step(1.0f / 60);
    require(doors.angle(0) > .2f, "A colliding physics box must transfer momentum and open the door without any door force call");
    b3DestroyBody(body);
}

void sharedRagdollCollision() {
    auto model = fixture();
    auto world = std::make_shared<viewer::PhysicsWorld>();
    viewer::DoorLayer doors(viewer::prepareDoorPanels(model), world);
    doors.activateNear({0, 1, 0});
    {
        viewer::ZombieRagdollWorld ragdolls(world);
        ragdolls.create({.7f, 0, -.6f}, 0, {0, 0, 5});
        for (int frame = 0; frame < 90; ++frame) world->step(1.0f / 60);
        require(doors.angle(0) > .05f, "Ragdolls and doors must physically collide in the shared world");
    }
    require(b3World_GetCounters(world->id()).bodyCount == 2, "Ragdoll cleanup must retain the shared door bodies");
    world->step(1.0f / 60);
}

void sweptBoundsStayVisible() {
    auto model = fixture({0, 0, 1}, {0, 0, -20});
    const auto panels = viewer::prepareDoorPanels(model);
    viewer::ModelVisibility visibility(model);
    // A narrow camera looks across the open panel's sweep, outside its closed
    // zero-thickness plane. The stationary BVH must retain it in a scope too.
    const auto view = glm::lookAt(glm::vec3(.5f, 1, -15), glm::vec3(.5f, 1, -19.1f), glm::vec3(0, 1, 0));
    const auto projection = glm::perspective(glm::radians(1.0f), 1.0f, .1f, 4.5f);
    std::vector<std::size_t> visible;
    visibility.query(projection * view, visible);
    visibility.filterDetails({.5f, 1, -15}, projection, visible);
    require(visible.size() == 1, "Frustum bounds must include an open door beyond its closed plane");
    const auto away = glm::lookAt(glm::vec3(100, 1, -15), glm::vec3(100, 1, -20), glm::vec3(0, 1, 0));
    visibility.query(projection * away, visible);
    require(visible.empty(), "Motion padding must not disable scope frustum culling");
}
} // namespace

int main() {
    try {
        extractionAndActivation(); localAndWideColliderQueries(); foundationSeamsDoNotShortenCollider(); forceAndLimits(); bodyCollisionOpensDoor();
        sharedRagdollCollision(); sweptBoundsStayVisible();
        std::cout << "Door physics tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Door physics test failed: " << error.what() << '\n';
        return 1;
    }
}
