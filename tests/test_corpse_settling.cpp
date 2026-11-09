#include "corpse_settling.hpp"
#include "physics_attachment.hpp"
#include "physics_world.hpp"

#include <box3d/box3d.h>

#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
constexpr float stepSeconds = 1.f / 60;
constexpr float nativeThreshold = 1e-8f;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct Fixture {
    std::shared_ptr<viewer::PhysicsWorld> world = std::make_shared<viewer::PhysicsWorld>();
    b3BodyId floor{};

    Fixture() { addFloor(); }

    void addFloor() {
        auto definition = b3DefaultBodyDef();
        definition.position = {0, -.1f, 0};
        floor = b3CreateBody(world->id(), &definition);
        auto shape = b3DefaultShapeDef();
        shape.baseMaterial.friction = 0;
        const auto hull = b3MakeBoxHull(20, .1f, 20);
        b3CreateHullShape(floor, &shape, &hull.base);
    }

    b3BodyId sphere(float x = 0, float y = 1) {
        auto definition = b3DefaultBodyDef();
        definition.type = b3_dynamicBody;
        definition.position = {x, y, 0};
        const auto body = b3CreateBody(world->id(), &definition);
        auto shape = b3DefaultShapeDef();
        shape.filter.categoryBits = viewer::ragdollBodyCategory;
        shape.baseMaterial.friction = 0;
        shape.baseMaterial.rollingResistance = 0;
        const b3Sphere geometry{{0, 0, 0}, .25f};
        b3CreateSphereShape(body, &shape, &geometry);
        return body;
    }

    void tick(int frames) {
        for (int frame = 0; frame < frames; ++frame) world->step(stepSeconds);
    }

    void slowSlide(b3BodyId body, bool tracked = true) {
        // Actual motion qualifies for the policy while remaining above the
        // tiny native threshold. This makes a policy sleep distinguishable
        // from an ordinary native sleep on an exactly motionless fixture.
        b3Body_SetSleepThreshold(body, nativeThreshold);
        b3Body_SetLinearVelocity(body, {.001f, 0, 0});
        b3Body_SetAngularVelocity(body, {0, 0, 0});
        if (tracked) world->corpseSettling().track(body);
    }
};

bool touching(b3BodyId first, b3BodyId second) {
    std::vector<b3ContactData> contacts(b3Body_GetContactCapacity(first));
    const int count = b3Body_GetContactData(first, contacts.data(), int(contacts.size()));
    for (int i = 0; i < count; ++i) {
        const auto& contact = contacts[std::size_t(i)];
        const auto a = b3Shape_GetBody(contact.shapeIdA), b = b3Shape_GetBody(contact.shapeIdB);
        if (!(B3_ID_EQUALS(a, second) || B3_ID_EQUALS(b, second))) continue;
        for (int j = 0; j < contact.manifoldCount; ++j)
            if (contact.manifolds[j].pointCount > 0) return true;
    }
    return false;
}

void quietSupportedMotionSleepsAfterTheWindow() {
    Fixture f;
    const auto corpse = f.sphere(), unrelated = f.sphere(5);
    f.tick(90);
    f.slowSlide(corpse);
    f.slowSlide(unrelated, false);
    std::uint64_t revision = 0;
    f.world->trackBodyMotion(corpse, revision);
    b3Body_SetLinearVelocity(unrelated, {.1f, 0, 0});
    f.tick(108);
    require(b3Body_IsAwake(corpse), "A newly quiet corpse must remain awake before the two-second window");
    require(b3Body_IsSleepEnabled(corpse) && b3Body_GetSleepThreshold(corpse) == nativeThreshold,
            "The fixture must preserve native sleeping with its deliberately tiny threshold");
    for (int frame = 0; frame < 42 && b3Body_IsAwake(corpse); ++frame) {
        const auto previousRevision = revision;
        f.tick(1);
        if (!b3Body_IsAwake(corpse)) {
            bool finalSleepEvent = false;
            const auto events = b3World_GetBodyEvents(f.world->id());
            for (int i = 0; i < events.moveCount; ++i)
                if (B3_ID_EQUALS(events.moveEvents[i].bodyId, corpse) && events.moveEvents[i].fellAsleep)
                    finalSleepEvent = true;
            require(finalSleepEvent && revision > previousRevision,
                    "Policy sleep must publish and consume its final body-motion event");
        }
    }
    require(!b3Body_IsAwake(corpse), "Supported motion below five millimeters per second must eventually use policy sleep");
    require(b3Body_IsAwake(unrelated) && b3Body_GetPosition(unrelated).x > 5.2f,
            "A shared static floor must not connect an unrelated moving body to the sleeping component");
    const auto sleepingRevision = revision;
    f.tick(60);
    require(revision == sleepingRevision, "A policy-slept corpse must retain its final motion revision while resting");
    f.world->untrackBodyMotion(corpse);
}

void worldSleepSettingsControlPolicyQualification() {
    Fixture f;
    const auto body = f.sphere();
    f.tick(90);
    f.slowSlide(body);
    b3World_EnableSleeping(f.world->id(), false);
    f.tick(240);
    require(b3Body_IsAwake(body), "Disabling sleeping for the world must also disable corpse policy sleep");
    b3World_EnableSleeping(f.world->id(), true);
    f.tick(108);
    require(b3Body_IsAwake(body), "Re-enabling world sleep must start a fresh quiet window");
    f.tick(42);
    require(!b3Body_IsAwake(body), "Re-enabled world sleep must allow a newly qualified corpse to settle");
}

void rotationCountsAsPointMotion() {
    Fixture f;
    const auto body = f.sphere();
    f.tick(90);
    f.slowSlide(body);
    b3Body_SetLinearVelocity(body, {0, 0, 0});
    b3Body_SetAngularVelocity(body, {0, 0, 1});
    f.world->corpseSettling().invalidate(body);
    f.tick(240);
    require(b3Body_IsAwake(body), "A fixed center must not conceal substantial rotational point motion");
    b3Body_SetAngularVelocity(body, {0, 0, .001f});
    b3Body_SetLinearVelocity(body, {.001f, 0, 0});
    f.world->corpseSettling().invalidate(body);
    f.tick(108);
    require(b3Body_IsAwake(body), "Reducing rotation must begin a fresh quiet window");
    f.tick(42);
    require(!b3Body_IsAwake(body), "Small supported translation and rotation may qualify together");
}

void unsupportedQuietBodiesStayAwake() {
    Fixture f;
    b3World_SetGravity(f.world->id(), {0, 0, 0});
    const auto body = f.sphere(0, 3);
    f.slowSlide(body);
    f.tick(240);
    require(b3Body_IsAwake(body) && std::abs(b3Body_GetPosition(body).y - 3) < .001f,
            "An unsupported quiet body must not be frozen in the air by the corpse policy");
}

void liveContactComponentsRejectUntrackedBodies() {
    Fixture f;
    const auto bottom = f.sphere(0, .25f), top = f.sphere(0, .75f);
    f.tick(90);
    f.slowSlide(bottom);
    f.slowSlide(top, false);
    require(touching(bottom, top), "The mixed-contact fixture must actually share touching contacts");
    f.tick(240);
    require(b3Body_IsAwake(bottom) && b3Body_IsAwake(top),
            "A tracked body must not force an untracked touching body asleep");
    f.world->corpseSettling().track(top);
    f.tick(60);
    require(b3Body_IsAwake(bottom), "Registering the missing contact member must not inherit an old quiet window");
    f.tick(120);
    require(!b3Body_IsAwake(bottom) && !b3Body_IsAwake(top),
            "A fully tracked quiet contact component must be able to sleep together");
}

void liveJointComponentsRejectIneligibleBodies() {
    enum class Exclusion { untracked, driven, sleepDisabled, kinematic };
    for (const auto exclusion : {Exclusion::untracked, Exclusion::driven, Exclusion::sleepDisabled, Exclusion::kinematic}) {
        Fixture f;
        const auto first = f.sphere(0), second = f.sphere(1);
        f.tick(90);
        f.slowSlide(first);
        f.slowSlide(second, exclusion != Exclusion::untracked);
        auto joint = b3DefaultDistanceJointDef();
        joint.base.bodyIdA = first;
        joint.base.bodyIdB = second;
        joint.length = 1;
        b3CreateDistanceJoint(f.world->id(), &joint);
        if (exclusion == Exclusion::driven) f.world->corpseSettling().setEnabled(second, false);
        if (exclusion == Exclusion::sleepDisabled) b3Body_EnableSleep(second, false);
        if (exclusion == Exclusion::kinematic) {
            b3Body_SetType(second, b3_kinematicBody);
            b3Body_SetLinearVelocity(second, {.001f, 0, 0});
        }
        f.tick(240);
        require(b3Body_IsAwake(first) && b3Body_IsAwake(second),
                "The complete live joint component must be eligible before any member is forced asleep");
    }
}

void blockedBridgeMembersDoNotSplitQualification() {
    for (const bool held : {false, true}) {
        Fixture f;
        const auto left = f.sphere(0), middle = f.sphere(1), right = f.sphere(2);
        f.tick(90);
        for (const auto body : {left, middle, right}) f.slowSlide(body);
        for (const auto& pair : {std::pair<b3BodyId, b3BodyId>{left, middle}, {middle, right}}) {
            auto joint = b3DefaultDistanceJointDef();
            joint.base.bodyIdA = pair.first;
            joint.base.bodyIdB = pair.second;
            joint.length = 1;
            b3CreateDistanceJoint(f.world->id(), &joint);
        }
        std::unique_ptr<viewer::PhysicsAttachment> attachment;
        if (held) attachment = std::make_unique<viewer::PhysicsAttachment>(f.world, middle);
        else f.world->corpseSettling().setEnabled(middle, false);
        auto target = b3Body_GetPosition(middle);
        for (int frame = 0; frame < 240; ++frame) {
            if (attachment) {
                target.x += .001f * stepSeconds;
                attachment->setTarget({target.x, target.y, target.z});
            }
            f.tick(1);
        }
        require(b3Body_IsAwake(left) && b3Body_IsAwake(middle) && b3Body_IsAwake(right),
                "An ineligible bridge member must exclude both grounded ends of the same live joint component");
    }
}

void aGrabResetsTheQuietWindow() {
    Fixture f;
    const auto body = f.sphere();
    f.tick(90);
    f.slowSlide(body);
    f.tick(90);
    require(b3Body_IsAwake(body), "The grab fixture must begin before the quiet window expires");
    auto target = b3Body_GetPosition(body);
    {
        viewer::PhysicsAttachment attachment(f.world, body);
        for (int frame = 0; frame < 180; ++frame) {
            target.x += .001f * stepSeconds;
            attachment.setTarget({target.x, target.y, target.z});
            f.tick(1);
        }
        require(f.world->isGrabbed(body) && b3Body_IsAwake(body),
                "A held body must remain excluded even when its actual motion is very small");
    }
    b3Body_SetLinearVelocity(body, {.001f, 0, 0});
    f.tick(60);
    require(!f.world->isGrabbed(body) && b3Body_IsAwake(body),
            "Releasing a grab must begin a new quiet window instead of restoring an old one");
    f.tick(120);
    require(!b3Body_IsAwake(body), "A released supported body may settle after its new quiet window");
}

void supportChangesRestartQualification() {
    Fixture f;
    const auto body = f.sphere();
    f.tick(90);
    f.slowSlide(body);
    f.tick(90);
    b3DestroyBody(f.floor);
    b3World_SetGravity(f.world->id(), {0, 0, 0});
    f.tick(180);
    require(b3Body_IsAwake(body), "Removing support must invalidate a partially accumulated quiet window");
    f.addFloor();
    b3World_SetGravity(f.world->id(), {0, -9.8f, 0});
    f.tick(60);
    require(b3Body_IsAwake(body), "Replacement support must receive a fresh quiet window");
    f.tick(120);
    require(!b3Body_IsAwake(body), "Replacement support must permit ordinary policy sleep after qualifying again");
}

void nativeWakeAndBodyReuseRemainSafe() {
    Fixture f;
    const auto body = f.sphere();
    f.tick(90);
    f.slowSlide(body);
    f.tick(180);
    require(!b3Body_IsAwake(body), "The wake fixture must first sleep through the corpse policy");
    {
        viewer::PhysicsAttachment attachment(f.world, body);
        require(b3Body_IsAwake(body) && f.world->isGrabbed(body),
                "Grabbing a policy-slept body must wake it immediately");
        f.tick(1);
    }
    f.slowSlide(body, false);
    f.tick(180);
    require(!b3Body_IsAwake(body), "A previously grabbed corpse must be able to settle again");
    b3Body_ApplyLinearImpulseToCenter(body, {.02f, 0, 0}, true);
    require(b3Body_IsAwake(body), "A normal Box3D impulse must still wake a policy-slept body");
    f.tick(1);
    b3Body_SetLinearVelocity(body, {.001f, 0, 0});
    f.tick(60);
    require(b3Body_IsAwake(body), "An impulse wake must discard the previous quiet window");
    f.tick(120);
    require(!b3Body_IsAwake(body), "A woken body must be able to qualify again");

    const auto position = b3Body_GetPosition(body);
    f.sphere(position.x, 2);
    bool woke = false;
    for (int frame = 0; frame < 90 && !woke; ++frame) {
        f.tick(1);
        woke = b3Body_IsAwake(body);
    }
    require(woke, "A normal collision must wake a corpse without explicit policy assistance");
    const auto oldId = body;
    f.world->corpseSettling().untrack(body);
    b3DestroyBody(body);
    const auto replacement = f.sphere(8, .25f);
    require(replacement.index1 == oldId.index1 && !B3_ID_EQUALS(replacement, oldId),
            "The reuse fixture must exercise the same native slot with a new body generation");
    f.slowSlide(replacement, false);
    f.tick(240);
    require(b3Body_IsAwake(replacement), "Untracking and native ID reuse must not transfer eligibility to a new body");
}
} // namespace

int main() {
    try {
        quietSupportedMotionSleepsAfterTheWindow();
        worldSleepSettingsControlPolicyQualification();
        rotationCountsAsPointMotion();
        unsupportedQuietBodiesStayAwake();
        liveContactComponentsRejectUntrackedBodies();
        liveJointComponentsRejectIneligibleBodies();
        blockedBridgeMembersDoNotSplitQualification();
        aGrabResetsTheQuietWindow();
        supportChangesRestartQualification();
        nativeWakeAndBodyReuseRemainSafe();
        std::cout << "Corpse settling tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << "Corpse settling test failed: " << error.what() << '\n';
        return 1;
    }
}
