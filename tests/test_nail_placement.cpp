#include "nail_placement.hpp"
#include "box_layer.hpp"
#include "camera.hpp"
#include "collision_world.hpp"
#include "door_layer.hpp"
#include "ground_items.hpp"
#include "inventory/item_config.hpp"
#include "physics_grab.hpp"
#include "physics_world.hpp"
#include "player_interaction.hpp"
#include "prop_collider.hpp"
#include <box3d/box3d.h>
#include <glm/gtc/quaternion.hpp>
#include <array>
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
const std::filesystem::path root(VIEWER_ITEM_ROOT);
glm::vec3 vector(b3Vec3 value) { return {value.x, value.y, value.z}; }
glm::quat rotation(b3Quat value) { return {value.s, value.v.x, value.v.y, value.v.z}; }

b3BodyId bodyOf(const std::shared_ptr<viewer::PhysicsWorld>& world, const viewer::PhysicsProp& prop) {
    struct Query { std::shared_ptr<viewer::PhysicsWorld> world; const viewer::PhysicsProp& prop; b3BodyId body{}; } query{world, prop, {}};
    const auto center = prop.center();
    const b3AABB aabb{{center.x - 2, center.y - 2, center.z - 2}, {center.x + 2, center.y + 2, center.z + 2}};
    b3World_OverlapAABB(world->id(), aabb, b3DefaultQueryFilter(), [](b3ShapeId shape, void* context) {
        auto& current = *static_cast<Query*>(context);
        const auto body = b3Shape_GetBody(shape);
        if (viewer::PhysicsProp(current.world, body) == current.prop) { current.body = body; return false; }
        return true;
    }, &query);
    require(b3Body_IsValid(query.body), "Could not resolve fixture prop body");
    return query.body;
}

struct Fixture {
    std::shared_ptr<viewer::PhysicsWorld> world = std::make_shared<viewer::PhysicsWorld>(root / "storage");
    viewer::BoxLayer boxes{world};
    viewer::GroundItems items{world};
    viewer::NailPlacement nails{items, world};

    viewer::PhysicsProp nail(glm::vec3 center = {.4f, 1, .4f}) {
        return *items.prop(items.add(viewer::loadItem(root / "storage/items", "nail"), 1, {}, center));
    }
    std::size_t board(glm::vec3 center, std::uint64_t material, float thickness = .02f, bool fixed = true) {
        const auto index = boxes.addCompound(center,
            {{viewer::PropColliderShape::Box, {}, {.2f, .2f, thickness * .5f}, material}});
        if (fixed) b3Body_SetType(bodyOf(world, boxes.prop(index)), b3_staticBody);
        return index;
    }
    viewer::Camera camera(glm::vec3 position = {0, 1, .8f}) const {
        viewer::Camera result;
        result.setPosition(position); result.setOrientation(-90, 0);
        return result;
    }
    void step(int count = 1) { for (int index = 0; index < count; ++index) world->step(1.f / 60); }
};

void eligibleSurfacesAndOcclusion() {
    Fixture fixture;
    fixture.board({0, 1, 0}, viewer::propMaterialId("wood"));
    fixture.board({0, 1, .3f}, 0);
    const auto nail = fixture.nail();
    require(!fixture.nails.snap(nail, fixture.camera()), "A nonwood surface must occlude wood behind it");
    fixture.boxes.remove(1);
    require(fixture.nails.snap(nail, fixture.camera()), "A nail must snap to a wood surface");
    require(glm::distance(fixture.nails.tip(nail), glm::vec3(0, 1, .01f)) < 1e-5f,
        "The nail point must coincide with the cursor ray's actual surface hit");
    const auto pose = fixture.items.poses().front().body;
    require(glm::dot(pose.rotation * glm::vec3(0, -1, 0), glm::vec3(0, 0, -1)) > .99999f,
        "The nail point must face inward along the surface normal");
    const auto body = bodyOf(fixture.world, nail);
    b3ShapeId shape{};
    require(b3Body_GetShapes(body, &shape, 1) == 1 && b3Shape_GetType(shape) == b3_sphereShape,
        "Snapped preview must use one enclosing sphere collider");
    fixture.nails.detach(nail);
    fixture.boxes.replaceColliders(0,
        {{viewer::PropColliderShape::Box, {}, {.2f, .2f, .01f}, viewer::propMaterialId("cardboard")}});
    require(fixture.nails.snap(nail, fixture.camera()), "Cardboard must also accept a nail point");

    Fixture broomFixture;
    broomFixture.items.add(viewer::loadItem(root / "storage/items", "broom"), 0, {}, {0, .65f, 0});
    const auto broomNail = broomFixture.nail();
    require(!broomFixture.nails.snap(broomNail, broomFixture.camera({0, .06f, .8f})),
        "The broom's nonwood bristles must not inherit its wooden head's material");
    require(broomFixture.nails.snap(broomNail, broomFixture.camera({0, .1425f, .8f})),
        "The broom's separately marked wood head must accept nailing");
}

void parkedPivotAndTargetMotion() {
    Fixture fixture;
    const auto target = fixture.board({0, 1, 0}, viewer::propMaterialId("wood"), .02f, false);
    const auto control = fixture.board({2, 1, 0}, viewer::propMaterialId("wood"), .02f, false);
    const auto nail = fixture.nail();
    require(fixture.nails.snap(nail, fixture.camera()), "Could not snap the moving-target fixture");
    const auto snapped = fixture.nails.tip(nail);
    const auto q = glm::angleAxis(glm::radians(35.f), glm::vec3(1, 0, 0)) * fixture.items.poses().front().body.rotation;
    fixture.nails.rotate(nail, q);
    require(glm::distance(fixture.nails.tip(nail), snapped) < 1e-6f,
        "Rotating a preview must pivot exactly about the point without translating it");
    fixture.step(30);
    const auto targetPose = fixture.boxes.pose(target);
    const auto controlPose = fixture.boxes.pose(control);
    require(glm::distance(targetPose.center, controlPose.center - glm::vec3(2, 0, 0)) < 1e-5f,
        "A parked nail must not push its target while ordinary gravity continues");
    require(glm::distance(fixture.nails.tip(nail), targetPose.center + targetPose.rotation * glm::vec3(0, 0, .01f)) < 1e-5f,
        "A released preview must remain attached to the moving target's local point");
    fixture.nails.detach(nail);
    require(!fixture.nails.pinned(nail) && !fixture.world->isCraftingHeld(bodyOf(fixture.world, nail)),
        "Detaching a preview must release its parked physics ownership");
    require(b3Body_GetShapeCount(bodyOf(fixture.world, nail)) == 2,
        "Detaching must restore the nail's two ordinary part colliders");
}

void immediateDriveAndJointLifetime() {
    Fixture fixture;
    std::set<std::uint64_t> targets;
    for (const auto pair : {std::pair<float, std::uint64_t>{0, viewer::propMaterialId("wood")},
             {-.035f, viewer::propMaterialId("cardboard")}, {-.075f, 0}}) {
        const auto index = fixture.board({0, 1, pair.first}, pair.second);
        targets.insert(b3StoreBodyId(bodyOf(fixture.world, fixture.boxes.prop(index))));
    }
    const auto nail = fixture.nail();
    require(fixture.nails.snap(nail, fixture.camera()), "Could not snap the stacked-board fixture");
    const auto originalTip = fixture.nails.tip(nail);
    require(fixture.nails.drive(nail), "Driving a snapped nail must succeed");
    require(glm::distance(fixture.nails.tip(nail), originalTip + glm::vec3(0, 0, -.099f)) < 1e-6f,
        "Driving must move the point exactly 9.9 cm inward without waiting for physics collision");
    require(fixture.nails.jointCount(nail) == 3 && fixture.nails.driven(nail),
        "Driving must immediately hinge every intersected body, including an unknown material behind the wood");
    const auto body = bodyOf(fixture.world, nail);
    std::array<b3JointId, 8> joints{};
    const int count = b3Body_GetJoints(body, joints.data(), static_cast<int>(joints.size()));
    require(count == 3, "One nail connection per distinct penetrated body is required");
    for (int index = 0; index < count; ++index) {
        const auto joint = joints[static_cast<std::size_t>(index)];
        require(b3Joint_GetType(joint) == b3_revoluteJoint, "Nailed objects must use revolute hinges");
        const auto a = b3Joint_GetBodyA(joint), b = b3Joint_GetBodyB(joint);
        require(targets.erase(b3StoreBodyId(a)) == 1 && B3_ID_EQUALS(b, body),
            "Each penetrated object must be connected directly to the nail");
        const auto fa = b3Joint_GetLocalFrameA(joint), fb = b3Joint_GetLocalFrameB(joint);
        const auto anchorA = vector(b3Body_GetWorldPoint(a, fa.p));
        const auto anchorB = vector(b3Body_GetWorldPoint(b, fb.p));
        require(glm::distance(anchorA, anchorB) < 1e-6f, "Nail hinge creation must not introduce an anchor correction");
        const auto axis = rotation(b3Body_GetRotation(a)) * rotation(fa.q) * glm::vec3(0, 0, 1);
        require(glm::dot(axis, glm::vec3(0, 0, -1)) > .99999f, "Nail hinges must rotate around its shaft axis");
    }
    {
        viewer::PhysicsGrab grip(nail, {0, 0, 0}, 100);
        require(!fixture.nails.pinned(nail) && fixture.nails.jointCount(nail) == 0,
            "Grabbing a driven nail must remove its connections synchronously");
    }
    require(b3World_GetCounters(fixture.world->id()).jointCount == 0, "Releasing a detached nail leaked a hinge");

    Fixture thin;
    thin.board({0, 1, 0}, viewer::propMaterialId("cardboard"), .001f);
    const auto thinNail = thin.nail();
    require(thin.nails.snap(thinNail, thin.camera()) && thin.nails.drive(thinNail) && thin.nails.jointCount(thinNail) == 1,
        "A one-millimetre sheet intersecting only the nail head must still be fastened");
}

void removedBodiesDoNotLeaveStaleAttachments() {
    Fixture fixture;
    fixture.board({0, 1, 0}, viewer::propMaterialId("wood"));
    const auto nail = fixture.nail();
    require(fixture.nails.snap(nail, fixture.camera()), "Could not create the target-deletion fixture");
    fixture.boxes.remove(0);
    fixture.step();
    require(!fixture.nails.pinned(nail) && !fixture.world->isCraftingHeld(bodyOf(fixture.world, nail)),
        "Deleting a target must release its preview and parked ownership");
    fixture.board({0, 1, 0}, viewer::propMaterialId("wood"));
    require(fixture.nails.snap(nail, fixture.camera()), "A nail must snap again after target replacement");
    const auto id = fixture.items.idOf(nail);
    require(id && fixture.items.erase(*id), "Could not remove the preview nail");
    fixture.step();
    require(!nail.valid() && b3World_GetCounters(fixture.world->id()).jointCount == 0,
        "Deleting a nail must not retain a stale attachment or reused body generation");
}

void playerInteractionGestures() {
    Fixture fixture;
    fixture.board({0, 1, 0}, viewer::propMaterialId("wood"));
    const auto nail = fixture.nail({0, 1, .4f});
    auto& nails = fixture.items.nails();
    viewer::CollisionWorld collision(viewer::Model{});
    collision.setBoxes(&fixture.boxes);
    viewer::DoorLayer doors({}, fixture.world);
    viewer::PlayerInteraction hand;
    auto camera = fixture.camera();
    hand.setCraftingMode(true, &fixture.items);
    require(hand.press(viewer::Hand::Left, camera, doors, fixture.boxes, collision, 1),
        "Crafting left press must select the physical nail through the shared player interaction");
    hand.update(camera, 1.f / 60, doors, fixture.boxes, collision, 1);
    require(nails.pinned(nail), "Dragging a selected nail toward wood must snap through PlayerInteraction");
    hand.release(viewer::Hand::Left, camera, doors, fixture.boxes, collision, 1.1);
    const auto initialTip = nails.tip(nail);
    fixture.step(3);
    require(hand.grabbing() && glm::distance(nails.tip(nail), initialTip) < 1e-6f && !hand.takeNailDriven(),
        "Initial mouse release must leave the snapped preview selected without driving it");

    require(hand.press(viewer::Hand::Left, camera, doors, fixture.boxes, collision, 2),
        "A pinned preview must accept another left press");
    camera.setPosition(camera.position() + glm::vec3(.05f, 0, 0));
    hand.update(camera, 1.f / 60, doors, fixture.boxes, collision, 2.01);
    hand.release(viewer::Hand::Left, camera, doors, fixture.boxes, collision, 2.1);
    require(!hand.takeNailDriven() && nails.pinned(nail) &&
            glm::distance(nails.tip(nail), initialTip + glm::vec3(.05f, 0, 0)) < 1e-5f,
        "A left re-drag must move the point to its new cursor hit instead of triggering a drive");
    const auto pivot = nails.tip(nail);
    require(hand.press(viewer::Hand::Right, camera, doors, fixture.boxes, collision, 3),
        "Right press must begin rotation of the selected preview");
    hand.moveGrip(40, -20);
    hand.update(camera, 1.f / 60, doors, fixture.boxes, collision, 3.01);
    fixture.step();
    hand.release(viewer::Hand::Right, camera, doors, fixture.boxes, collision, 3.1);
    require(glm::distance(nails.tip(nail), pivot) < 1e-6f,
        "Shared right-drag rotation must leave the snapped nail point fixed");
    const auto q = fixture.items.poses().front().body.rotation;
    require(hand.press(viewer::Hand::Left, camera, doors, fixture.boxes, collision, 4),
        "A rotated preview must still accept its drive click");
    hand.release(viewer::Hand::Left, camera, doors, fixture.boxes, collision, 4.1);
    require(hand.takeNailDriven() && !hand.takeNailDriven() && !hand.grabbing() && nails.jointCount(nail) == 1,
        "A stationary left click must drive once, consume its event, and release the active hand grip");
    require(glm::distance(nails.tip(nail), pivot + q * glm::vec3(0, -.099f, 0)) < 1e-5f,
        "Shared drive gesture must respect the nail's rotated inward direction");

    Fixture cancelled;
    cancelled.board({0, 1, 0}, viewer::propMaterialId("wood"));
    const auto cancelledNail = cancelled.nail({0, 1, .4f});
    auto& cancelledNails = cancelled.items.nails();
    viewer::CollisionWorld empty(viewer::Model{});
    viewer::DoorLayer noDoors({}, cancelled.world);
    viewer::PlayerInteraction cancelledHand;
    auto cancelledCamera = cancelled.camera();
    cancelledHand.setCraftingMode(true, &cancelled.items);
    require(cancelledHand.press(viewer::Hand::Left, cancelledCamera, noDoors, cancelled.boxes, empty, 1),
        "Could not select the cancellation fixture's nail");
    cancelledHand.update(cancelledCamera, 1.f / 60, noDoors, cancelled.boxes, empty, 1);
    cancelledHand.release(viewer::Hand::Left, cancelledCamera, noDoors, cancelled.boxes, empty, 1.1);
    const auto parked = cancelledNails.tip(cancelledNail);
    cancelledHand.cancel();
    cancelled.step(3);
    require(!cancelledHand.grabbing() && cancelledNails.pinned(cancelledNail) &&
            cancelled.world->isCraftingHeld(bodyOf(cancelled.world, cancelledNail)) &&
            glm::distance(cancelledNails.tip(cancelledNail), parked) < 1e-6f,
        "Cancelling the hand must preserve the world-owned, nonpushing nail preview");
}
}

int main() {
    try {
        eligibleSurfacesAndOcclusion();
        parkedPivotAndTargetMotion();
        immediateDriveAndJointLifetime();
        removedBodiesDoNotLeaveStaleAttachments();
        playerInteractionGestures();
        std::cout << "Nail placement checks passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
