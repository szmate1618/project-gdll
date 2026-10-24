#include "player_body.hpp"
#include "melee_combat.hpp"
#include "physics_world.hpp"
#include "zombie_layer.hpp"
#include "box_layer.hpp"
#include "vehicle_layer.hpp"
#include "collision_world.hpp"
#include <box3d/box3d.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
void require(bool v, const char* message) { if (!v) throw std::runtime_error(message); }
glm::vec3 center(const viewer::PlayerBody& body, viewer::PlayerBodyPart part) { return glm::vec3(body.transform(part)[3]); }
bool solvedContact(b3BodyId body) {
    std::vector<b3ContactData> contacts(b3Body_GetContactCapacity(body));
    const int n = b3Body_GetContactData(body, contacts.data(), static_cast<int>(contacts.size()));
    for (int c = 0; c < n; ++c)
        for (int m = 0; m < contacts[c].manifoldCount; ++m)
            for (int p = 0; p < contacts[c].manifolds[m].pointCount; ++p)
                if (contacts[c].manifolds[m].points[p].totalNormalImpulse > .001f) return true;
    return false;
}
void fistPullRotatesShoulder() {
    for (bool right : {true, false}) {
        auto world = std::make_shared<viewer::PhysicsWorld>();
        viewer::PlayerBody player(world, 1.7f);
        const glm::vec3 eye(0, 1.7f, 0), aim(0, 0, -1);
        player.follow(eye, aim, true);
        if (!right) {
            player.punch(aim);
            player.follow(eye, aim, false);
            player.follow(eye, aim, true);
        }
        const auto upper = player.body(right ? viewer::PlayerBodyPart::rightUpperArm : viewer::PlayerBodyPart::leftUpperArm);
        const auto fist = right ? viewer::PlayerBodyPart::rightFist : viewer::PlayerBodyPart::leftFist;
        // The chest turns with the punch. Measure elevation in the arm's own
        // horizontal plane; a camera/chest Z axis would conflate yaw with drop.
        const float guardHeight = b3Body_GetPosition(player.body(fist)).y;
        float alignment = 0, drop = 0;
        player.punch(aim);
        for (int f = 0; f < 45 && player.punching(fist); ++f)
            world->step(1.f / 60, [&](float s) { player.beforeStep(s); }, [&](float) {
                const auto direction = b3Body_GetWorldVector(upper, {0, 1, 0});
                const auto upperPose = player.transform(right ? viewer::PlayerBodyPart::rightUpperArm : viewer::PlayerBodyPart::leftUpperArm);
                const auto shoulder = glm::vec3(upperPose[3] - .5f * upperPose[1]);
                const auto alongArm = player.fistPosition(right) - shoulder;
                const auto forward = glm::normalize(glm::vec3(alongArm.x, 0, alongArm.z));
                alignment = std::max(alignment, glm::dot(glm::vec3(direction.x, direction.y, direction.z), forward));
                drop = std::max(drop, guardHeight - b3Body_GetPosition(player.body(fist)).y);
                player.afterStep();
            });
        std::cout << (right ? "Right" : "Left") << " shoulder forward alignment " << alignment << ", fist drop " << drop << '\n';
        require(alignment > .985f, "The fist must pull the upper arm forward at the shoulder before punch recovery");
        require(drop < .08f, "The shoulder must follow the fist instead of letting the forearm swing downward");
        require(!player.punching(fist) && player.returning(fist), "A shoulder-driven punch must start returning to guard");
    }
}
void slowGuardRecovery() {
    for (bool right : {true, false}) {
        auto world = std::make_shared<viewer::PhysicsWorld>();
        viewer::PlayerBody player(world, 1.7f);
        const glm::vec3 eye(0, 1.7f, 0), aim(0, 0, -1);
        player.follow(eye, aim, true);
        if (!right) {
            player.punch(aim);
            player.follow(eye, aim, false);
            player.follow(eye, aim, true);
        }
        const auto fist = right ? viewer::PlayerBodyPart::rightFist : viewer::PlayerBodyPart::leftFist;
        const auto chest = player.body(viewer::PlayerBodyPart::upperTorso);
        const auto localFist = [&] {
            const auto p = b3Body_GetLocalPoint(chest, b3Body_GetPosition(player.body(fist)));
            return glm::vec3(p.x, p.y, p.z);
        };
        const auto guard = localFist();
        player.punch(aim);
        int strikeFrames = 0;
        while (player.punching(fist) && strikeFrames++ < 120)
            world->step(1.f / 60, [&](float s) { player.beforeStep(s); }, [&](float) {
                const auto before = player.fistPosition(right);
                player.afterStep();
                if (player.returning(fist))
                    require(glm::distance(before, player.fistPosition(right)) < .00001f,
                            "Starting recovery must not snap the fist toward guard");
            });
        require(player.returning(fist), "A fully extended fist must enter recovery");
        require(glm::distance(localFist(), guard) > .15f, "Recovery must begin with the fist still extended");
        auto previous = localFist();
        int returnFrames = 0;
        while (player.returning(fist) && returnFrames++ < 600) {
            world->step(1.f / 60, [&](float s) { player.beforeStep(s); }, [&](float) { player.afterStep(); });
            const auto current = localFist();
            require(glm::distance(current, previous) < .09f, "The returning fist must move continuously at the faster recovery speed");
            require(!player.punching(fist), "A returning arm must never count as an active punch");
            if (returnFrames == strikeFrames)
                require(glm::distance(current, guard) > .05f, "Recovery must remain visibly slower than the outbound punch");
            previous = current;
        }
        std::cout << "Strike " << strikeFrames << " frames, recovery " << returnFrames << " frames\n";
        require(!player.returning(fist) && std::abs(returnFrames - 6 - 3 * strikeFrames) <= 6,
                "Recovery must include the 0.1 s pause and three times the outbound duration, allowing brief physical settling");
        require(glm::distance(localFist(), guard) < .005f, "Recovery must finish at the original guard pose");
        const auto next = right ? viewer::PlayerBodyPart::leftFist : viewer::PlayerBodyPart::rightFist;
        player.punch(aim);
        world->step(1.f / 60, [&](float s) { player.beforeStep(s); }, [&](float) {
            player.recordArmContact(next);
            player.afterStep();
        });
        require(player.returning(next), "The next hand must enter recovery after contact");
        player.punch(aim);
        require(player.punching(fist), "The other arm may punch during recovery");
        player.punch(aim);
        require(player.returning(next) && !player.punching(next), "A returning arm must not restart a punch before reaching guard");
        player.follow(eye, aim, false);
        player.follow(eye + glm::vec3(5, 0, 2), aim, true);
        require(!player.returning(next) && !player.punching(next), "Re-equipping must cancel unfinished recovery");
        require(glm::distance(localFist(), guard) < .005f, "Re-equipping must restore the base guard without stale motors");
    }
}
void pelvisStaysUpright() {
    for (bool sideways : {false, true}) {
        auto world = std::make_shared<viewer::PhysicsWorld>();
        viewer::PlayerBody player(world, 1.7f);
        glm::vec3 eye(0, 1.7f, 0);
        player.follow(eye, {0, 0, -1}, true);
        const auto hip = player.body(viewer::PlayerBodyPart::hip);
        for (int f = 0; f < 180; ++f) {
            // Start running, reverse, then stop, while applying a bending load.
            const float speed = f < 60 ? 6.f : f < 120 ? -6.f : 0.f;
            eye += (sideways ? glm::vec3(1, 0, 0) : glm::vec3(0, 0, -1)) * (speed / 60.f);
            player.follow(eye, {0, 0, -1}, true);
            world->step(1.f / 60, [&](float s) {
                player.beforeStep(s);
                b3Body_ApplyTorque(hip, sideways ? b3Vec3{0, 0, 50} : b3Vec3{50, 0, 0}, true);
            }, [&](float) { player.afterStep(); });
            const auto up = b3Body_GetWorldVector(hip, {0, 1, 0});
            if (!(std::isfinite(up.y) && up.y > std::cos(glm::radians(1.f))))
                std::cerr << "Pelvis tilt: sideways=" << sideways << " frame=" << f
                          << " degrees=" << glm::degrees(std::acos(glm::clamp(up.y, -1.f, 1.f))) << '\n';
            require(std::isfinite(up.y) && up.y > std::cos(glm::radians(1.f)),
                    "The pelvis must stay upright under running, reversal, and pitch/roll torque");
        }
    }
    auto world = std::make_shared<viewer::PhysicsWorld>();
    viewer::PlayerBody player(world, 1.7f);
    player.follow({0, 1.7f, 0}, {0, 0, -1}, true);
    const auto hip = player.body(viewer::PlayerBodyPart::hip);
    float peakYaw = 0, yaw = 0;
    for (int f = 0; f < 180; ++f) {
        world->step(1.f / 60, [&](float s) {
            player.beforeStep(s);
            if (f < 30) b3Body_ApplyTorque(hip, {0, 50, 0}, true);
        }, [&](float) { player.afterStep(); });
        const auto forward = b3Body_GetWorldVector(hip, {0, 0, -1});
        yaw = std::abs(std::atan2(-forward.x, -forward.z));
        peakYaw = std::max(peakYaw, yaw);
    }
    require(peakYaw > glm::radians(1.f), "The upright pelvis must still allow spring-driven yaw");
    require(yaw < glm::radians(.5f), "The pelvis yaw spring must restore neutral after the load ends");
}
void torsoBendingAndTwisting() {
    // Apply the same sustained chest torque around each world axis. Bending
    // should stay small while the idle torso motors hold a neutral posture.
    for (int axis = 0; axis < 3; ++axis) {
        auto world = std::make_shared<viewer::PhysicsWorld>();
        viewer::PlayerBody player(world, 1.7f);
        player.follow({0, 1.7f, 0}, {0, 0, -1}, true);
        b3Vec3 torque{};
        if (axis == 0) torque.x = 20;
        if (axis == 1) torque.y = 20;
        if (axis == 2) torque.z = 20;
        for (int f = 0; f < 180; ++f)
            world->step(1.f / 60, [&](float s) {
                player.beforeStep(s);
                b3Body_ApplyTorque(player.body(viewer::PlayerBodyPart::upperTorso), torque, true);
            }, [&](float) { player.afterStep(); });
        glm::vec3 parentUp(0, 1, 0);
        for (auto part : {viewer::PlayerBodyPart::hip, viewer::PlayerBodyPart::lowerTorso,
                          viewer::PlayerBodyPart::upperTorso}) {
            const auto up = glm::normalize(glm::vec3(player.transform(part)[1]));
            const float bend = std::acos(glm::clamp(glm::dot(parentUp, up), -1.f, 1.f));
            require(std::isfinite(bend) && bend < .12f,
                    "Each hip/spine connection must resist forward and sideways bending");
            parentUp = up;
        }
        if (axis == 1) {
            const auto forward = glm::normalize(glm::vec3(player.transform(viewer::PlayerBodyPart::upperTorso)[2]));
            const float twist = std::abs(std::atan2(forward.x, forward.z));
            require(twist < .12f, "Idle torso motors must hold neutral rather than drift under sustained torque");
        }
    }
}
void torsoFollowsTheThrowingHand() {
    for (bool right : {true, false}) {
        auto world = std::make_shared<viewer::PhysicsWorld>();
        viewer::PlayerBody player(world, 1.7f);
        player.follow({0, 1.7f, 0}, {0, 0, -1}, true);
        if (!right) {
            player.punch({0, 0, -1});
            player.follow({0, 1.7f, 0}, {0, 0, -1}, false);
            player.follow({0, 1.7f, 0}, {0, 0, -1}, true);
        }
        const auto hip = player.body(viewer::PlayerBodyPart::hip);
        const auto chest = player.body(viewer::PlayerBodyPart::upperTorso);
        float peakTurn = 0, turn = 0;
        player.punch({0, 0, -1});
        for (int f = 0; f < 180; ++f) {
            world->step(1.f / 60, [&](float s) { player.beforeStep(s); }, [&](float) { player.afterStep(); });
            const auto forward = b3Body_GetLocalVector(hip, b3Body_GetWorldVector(chest, {0, 0, -1}));
            turn = std::atan2(-forward.x, -forward.z);
            peakTurn = std::max(peakTurn, (right ? 1.f : -1.f) * turn);
        }
        require(peakTurn > glm::radians(3.f), "Torso motors must advance the throwing shoulder for either hand");
        require(std::abs(turn) < glm::radians(2.f), "Torso motors must return toward neutral with the fist");
    }
}
void torsoStopsWithTheStrike() {
    for (bool right : {false, true}) for (bool impact : {false, true}) {
        auto world = std::make_shared<viewer::PhysicsWorld>();
        viewer::PlayerBody player(world, 1.7f);
        const glm::vec3 eye(0, 1.7f, 0), aim(0, 0, -1);
        player.follow(eye, aim, true);
        if (!right) {
            player.punch(aim);
            player.follow(eye, aim, false); player.follow(eye, aim, true);
        }
        const auto fist = right ? viewer::PlayerBodyPart::rightFist : viewer::PlayerBodyPart::leftFist;
        const auto yaw = [&] {
            const auto axis = b3Body_GetWorldVector(player.body(viewer::PlayerBodyPart::upperTorso), {1, 0, 0});
            return (right ? 1.f : -1.f) * std::atan2(-axis.z, axis.x);
        };
        player.punch(aim);
        for (int f = 0; f < 60 && player.punching(fist); ++f)
            world->step(1.f / 60, [&](float s) { player.beforeStep(s); }, [&](float) {
                if (impact && f == 3) player.recordArmContact(fist);
                player.afterStep();
            });
        require(player.returning(fist), "The strike must end before checking the torso hold");
        const float stopped = yaw();
        if (!impact)
            require(stopped > glm::radians(32.f) && stopped < glm::radians(39.f),
                    "An unobstructed punch must turn the shoulders approximately 35 degrees during the strike");
        float extra = 0;
        for (int f = 0; f < 6; ++f) {
            world->step(1.f / 60, [&](float s) { player.beforeStep(s); }, [&](float) { player.afterStep(); });
            extra = std::max(extra, yaw() - stopped);
        }
        std::cout << (right ? "Right" : "Left") << (impact ? " impact" : " extension/timeout")
                  << " shoulder turn " << glm::degrees(stopped) << ", braking travel " << glm::degrees(extra) << '\n';
        require(extra < glm::radians(3.f), "The torso must brake at the strike end instead of continuing the throw during the pause");
        for (int f = 0; f < 180; ++f)
            world->step(1.f / 60, [&](float s) { player.beforeStep(s); }, [&](float) { player.afterStep(); });
        require(std::abs(yaw()) < glm::radians(2.f), "The torso must recover to neutral after its stopped punch");
    }
}
void torsoFollowDoesNotRedirectThePunch() {
    for (bool hitTarget : {false, true}) for (bool right : {false, true}) {
        auto world = std::make_shared<viewer::PhysicsWorld>();
        viewer::PlayerBody player(world, 1.7f);
        const glm::vec3 eye(0, 1.7f, 0), forward(0, 0, -1), target(0, 1.55f, -2);
        player.follow(eye, forward, true);
        if (!right) {
            player.punch(forward);
            player.follow(eye, forward, false);
            player.follow(eye, forward, true);
        }
        const auto fist = right ? viewer::PlayerBodyPart::rightFist : viewer::PlayerBodyPart::leftFist;
        const auto start = player.fistPosition(right);
        const auto direction = hitTarget ? glm::normalize(target - start) : forward;
        player.punch(forward, hitTarget ? &target : nullptr);
        float sideways = 0, reach = 0;
        for (int f = 0; f < 60 && player.punching(fist); ++f)
            world->step(1.f / 60, [&](float s) { player.beforeStep(s); }, [&](float) {
                const auto travel = player.fistPosition(right) - start;
                const float along = glm::dot(travel, direction);
                sideways = std::max(sideways, glm::length(travel - direction * along));
                reach = std::max(reach, along);
                player.afterStep();
            });
        require(reach > .25f, "Aim compensation must still produce a full outward punch");
        require(sideways < .08f, "Torso follow-through must not carry either fist off the original target line");
    }
}
void bothEndingsWaitBeforeRetraction() {
    for (bool right : {false, true}) for (bool impact : {false, true}) {
        auto world = std::make_shared<viewer::PhysicsWorld>();
        viewer::PlayerBody player(world, 1.7f);
        const glm::vec3 eye(0, 1.7f, 0), direction(0, 0, -1);
        player.follow(eye, direction, true);
        if (!right) {
            player.punch(direction);
            player.follow(eye, direction, false); player.follow(eye, direction, true);
        }
        const auto fist = right ? viewer::PlayerBodyPart::rightFist : viewer::PlayerBodyPart::leftFist;
        player.punch(direction);
        for (int f = 0; f < 60 && player.punching(fist); ++f)
            world->step(1.f / 60, [&](float dt) { player.beforeStep(dt); }, [&](float) {
                if (impact) player.recordArmContact(fist);
                player.afterStep();
            });
        require(!player.punching(fist) && player.returning(fist), "Both strike endings must enter non-damaging recovery");
        const auto forearm = player.body(right ? viewer::PlayerBodyPart::rightLowerArm : viewer::PlayerBodyPart::leftLowerArm);
        std::vector<b3JointId> joints(b3Body_GetJointCount(forearm));
        b3Body_GetJoints(forearm, joints.data(), static_cast<int>(joints.size()));
        b3JointId elbow{};
        for (auto joint : joints) if (b3Joint_GetType(joint) == b3_revoluteJoint) elbow = joint;
        require(b3Joint_IsValid(elbow), "The throwing forearm must retain its elbow joint");
        for (int f = 0; f < 6; ++f)
            world->step(1.f / 60, [&](float dt) {
                player.beforeStep(dt);
                require(b3RevoluteJoint_GetMotorSpeed(elbow) == 0,
                        "Contact and non-contact endings must hold for six full fixed steps");
            }, [&](float) {
                player.afterStep();
                require(!player.punching(fist) && player.returning(fist), "The pause must remain non-damaging");
            });
        world->step(1.f / 60, [&](float dt) {
            player.beforeStep(dt);
            require(std::abs(b3RevoluteJoint_GetMotorSpeed(elbow)) > .0001f,
                    "Retraction must begin after the 0.1 s pause expires");
        }, [&](float) { player.afterStep(); });
    }
}

void impactWaitsBeforeRetraction() {
    auto world = std::make_shared<viewer::PhysicsWorld>();
    viewer::PlayerBody player(world, 1.7f);
    player.follow({0, 1.7f, 0}, {0, 0, -1}, true);
    const auto guard = player.fistPosition(true);
    player.punch({0, 0, -1});
    world->step(1.f / 60, [&](float s) { player.beforeStep(s); }, [&](float) {
        // Simulate the solved-contact notification used by mesh handoff.
        player.recordArmContact(viewer::PlayerBodyPart::rightFist);
        player.afterStep();
    });
    for (int f = 0; f < 6; ++f) {
        world->step(1.f / 60, [&](float s) { player.beforeStep(s); }, [&](float) { player.afterStep(); });
        require(player.returning(viewer::PlayerBodyPart::rightFist) && !player.punching(viewer::PlayerBodyPart::rightFist),
                "Collision recovery must remain non-damaging throughout the 0.1 s impact pause");
    }
    require(glm::distance(player.fistPosition(true), guard) > .005f, "The impact pause must not snap the hand back to guard");
    for (int f = 0; f < 180 && player.returning(viewer::PlayerBodyPart::rightFist); ++f)
        world->step(1.f / 60, [&](float s) { player.beforeStep(s); }, [&](float) { player.afterStep(); });
    require(!player.returning(viewer::PlayerBodyPart::rightFist), "Retraction must finish after the impact pause expires");
}
void anatomyAndDynamics() {
    auto world = std::make_shared<viewer::PhysicsWorld>();
    viewer::PlayerBody player(world, 1.7f);
    const glm::vec3 eye(10, 1.7f, -4);
    player.follow(eye, {0, 0, -1}, true);
    auto left = player.fistPosition(false), right = player.fistPosition(true);
    require(std::abs(left.y - right.y) < .0001f && std::abs(left.z - right.z) < .0001f &&
            std::abs(left.x + right.x - 2 * eye.x) < .0001f, "Guard must be symmetric");
    require(std::abs(right.y - (eye.y - .2374f)) < .001f, "The vertical-arm guard must keep fists just below shoulder height");
    for (bool rightSide : {false, true}) {
        const auto upper = player.transform(rightSide ? viewer::PlayerBodyPart::rightUpperArm : viewer::PlayerBodyPart::leftUpperArm);
        const auto lower = player.transform(rightSide ? viewer::PlayerBodyPart::rightLowerArm : viewer::PlayerBodyPart::leftLowerArm);
        const auto shoulder = glm::vec3(upper * glm::vec4(0, -.5f, 0, 1));
        const auto offset = player.fistPosition(rightSide) - shoulder;
        require(std::abs(offset.z + .08f) < .0001f && std::abs(offset.y + .0174f) < .0001f,
                "Guard fist centers must sit 8 cm forward and just below their shoulders");
        require(glm::distance(glm::normalize(glm::vec3(upper[1])), glm::vec3(0, -1, 0)) < .0001f,
                "Guard upper arms must point vertically down from the shoulders");
        require(std::abs(glm::length(upper[1]) - .31f) < .0001f && std::abs(glm::length(lower[1]) - .28f) < .0001f,
                "The close guard must preserve anatomical upper-arm and forearm lengths");
    }
    const auto relative = [&](viewer::PlayerBodyPart part) {
        return glm::inverse(player.transform(viewer::PlayerBodyPart::upperTorso)) * player.transform(part);
    };
    const auto baseLeft = relative(viewer::PlayerBodyPart::leftFist);
    const auto baseRight = relative(viewer::PlayerBodyPart::rightFist);
    for (int f = 0; f < 90; ++f) {
        world->step(1.f / 60, [&](float s) { player.beforeStep(s); }, [&](float) { player.afterStep(); });
        require(glm::distance(glm::vec3(relative(viewer::PlayerBodyPart::leftFist)[3]), glm::vec3(baseLeft[3])) < .002f &&
                glm::distance(glm::vec3(relative(viewer::PlayerBodyPart::rightFist)[3]), glm::vec3(baseRight[3])) < .002f,
                "Unthrown arms must remain locked in their base positions");
    }
    const auto initialHip = player.transform(viewer::PlayerBodyPart::hip);
    player.punch({0, 0, -1});
    require(player.punching(viewer::PlayerBodyPart::rightFist) && !player.punching(viewer::PlayerBodyPart::leftFist),
            "A punch must release only the throwing arm");
    float reach = 0, twist = 0;
    bool extended = false;
    for (int f = 0; f < 180; ++f) {
        world->step(1.f / 60, [&](float s) { player.beforeStep(s); }, [&](float) {
            reach = std::max(reach, right.z - player.fistPosition(true).z);
            const auto a = glm::normalize(glm::vec3(player.transform(viewer::PlayerBodyPart::rightUpperArm)[1]));
            const auto b = glm::normalize(glm::vec3(player.transform(viewer::PlayerBodyPart::rightLowerArm)[1]));
            extended |= glm::dot(a, b) > std::cos(glm::radians(5.1f));
            player.afterStep();
        });
        const auto hip = player.transform(viewer::PlayerBodyPart::hip);
        twist = std::max(twist, std::abs(hip[2].x - initialHip[2].x));
        require(glm::distance(glm::vec3(relative(viewer::PlayerBodyPart::leftFist)[3]), glm::vec3(baseLeft[3])) < .005f,
                "The other hand must stay in guard while one arm punches");
        for (std::size_t p = 0; p < viewer::playerBodyPartCount; ++p) {
            const auto b = player.body(static_cast<viewer::PlayerBodyPart>(p));
            const auto x = center(player, static_cast<viewer::PlayerBodyPart>(p));
            require(std::isfinite(x.x + x.y + x.z) && glm::distance(x, eye) < 2.f, "Body must stay finite and attached");
            // No world obstacles: overlapping body parts must never self-collide.
            require(b3Body_GetContactCapacity(b) == 0, "Player parts must not self-collide");
        }
    }
    std::cout << "Punch reach " << reach << ", hip twist " << twist << '\n';
    require(reach > .12f, "A fist impulse must extend the arm");
    require(twist > .0001f, "A punch must propagate rotation into the hip");
    require(extended && !player.punching(viewer::PlayerBodyPart::rightFist), "A fully extended arm must stop punching");
    require(glm::distance(glm::vec3(relative(viewer::PlayerBodyPart::rightFist)[3]), glm::vec3(baseRight[3])) < .005f,
            "An extended arm must return to its base position without springs");
    player.follow(eye, {0, 0, -1}, false);
    require(!b3Body_IsEnabled(player.body(viewer::PlayerBodyPart::rightFist)), "Unequipped body must leave simulation");
    player.follow({100, 1.7f, 50}, {0, 0, -1}, true);
    require(glm::distance(player.fistPosition(true), glm::vec3(100.215f, 1.4626f, 49.81f)) < .01f, "Reequip must reset body without a teleport impulse");

    viewer::PlayerBody tall(world, 2.04f);
    tall.follow({}, {0, 0, -1}, true);
    require(std::abs(glm::length(tall.transform(viewer::PlayerBodyPart::upperTorso)[0]) - .456f) < .001f,
            "Anatomy must scale with standing eye height");
}
viewer::AnimatedModel zombieCube() {
    viewer::AnimatedModel asset;
    asset.model = viewer::BoxLayer::unitCube();
    asset.model.draws.emplace_back();
    for (auto& v : asset.model.primitives[0].vertices) v.position = v.position * glm::vec3(.45f, 1.8f, .32f) + glm::vec3(0, .9f, 0);
    asset.model.boundsMin = {-.225f, 0, -.16f}; asset.model.boundsMax = {.225f, 1.8f, .16f};
    return asset;
}
void returningArmCannotRagdoll() {
    auto world = std::make_shared<viewer::PhysicsWorld>();
    viewer::PlayerBody player(world, 1.7f);
    viewer::MeleeCombat melee(world);
    player.follow({0, 1.7f, 0}, {0, 0, -1}, true);
    player.punch({0, 0, -1});
    for (int f = 0; f < 120 && player.punching(viewer::PlayerBodyPart::rightFist); ++f)
        world->step(1.f / 60, [&](float s) { player.beforeStep(s); }, [&](float) { player.afterStep(); });
    require(player.returning(viewer::PlayerBodyPart::rightFist), "The contact fixture must start with a returning fist");
    const auto fist = player.fistPosition(true);
    viewer::ZombieLayer zombies;
    zombies.assets.push_back(zombieCube()); zombies.instances.emplace_back();
    auto& zombie = zombies.instances[0];
    zombie.restRoot = zombie.restTransform = zombie.transform =
        glm::translate(glm::mat4(1), glm::vec3(fist.x, 0, fist.z + .20f));
    zombies.attachPhysics(world); zombies.rebuildSpatialIndex();
    bool contacted = false;
    for (int f = 0; f < 90; ++f) {
        world->step(1.f / 60, [&](float s) { player.beforeStep(s); melee.prepare(player, &zombies, 0); }, [&](float) {
            contacted |= player.returning(viewer::PlayerBodyPart::rightFist) && solvedContact(player.body(viewer::PlayerBodyPart::rightFist));
            melee.finish(player, &zombies, 0); player.afterStep();
        });
        require(!zombie.ragdoll, "Contact with a returning arm must not ragdoll a zombie");
    }
    require(contacted, "The returning fist must make a real solved zombie contact");
}
void meshContact(bool blocked, bool distant, int publishedFrame = -1) {
    auto world = std::make_shared<viewer::PhysicsWorld>();
    viewer::PlayerBody player(world, 1.7f);
    viewer::MeleeCombat melee(world);
    viewer::ZombieLayer zombies;
    zombies.assets.push_back(zombieCube());
    if (publishedFrame >= 0) {
        auto& asset = zombies.assets[0];
        asset.frameCount = 2; asset.duration = 1;
        asset.animation.resize(1);
        for (int frame = 0; frame < 2; ++frame)
            for (const auto& vertex : asset.model.primitives[0].vertices)
                asset.animation[0].frames.push_back({glm::vec4(vertex.position + glm::vec3(frame ? 0.f : 4.f, 0, 0), 1), {0, 1, 0, 0}});
    }
    zombies.instances.emplace_back();
    auto& z = zombies.instances[0];
    z.displayedAnimationFrame = static_cast<float>(publishedFrame);
    z.restRoot = glm::translate(glm::mat4(1), glm::vec3(0, 0, distant ? -4.f : -.78f));
    z.restTransform = z.transform = z.restRoot;
    zombies.attachPhysics(world);
    zombies.rebuildSpatialIndex();
    viewer::BoxLayer boxes(world);
    if (blocked) boxes.add({0, 1.45f, -.53f}, {2, 2, .12f});
    viewer::Model empty;
    viewer::CollisionWorld scene(empty);
    player.follow({0, 1.7f, 0}, {0, 0, -1}, true);
    melee.prepare(player, &zombies, 0);
    melee.punch(player, {0, 1.7f, 0}, {0, -.3f, -1}, scene);
    require(!z.ragdoll, "Aiming ray must never damage a zombie");
    bool recoveredOnContact = false;
    for (int frame = 0; frame < 120; ++frame) {
        world->step(1.f / 60, [&](float s) {
            player.beforeStep(s); melee.prepare(player, &zombies, 0);
        }, [&](float) {
            const bool wasRagdoll = z.ragdoll;
            melee.finish(player, &zombies, 0); player.afterStep();
            if (!wasRagdoll && z.ragdoll) {
                require(!player.punching(viewer::PlayerBodyPart::rightFist) && player.returning(viewer::PlayerBodyPart::rightFist),
                        "A zombie hit must start non-damaging recovery immediately");
                recoveredOnContact = true;
            }
        });
        zombies.synchronizePhysics();
    }
    std::cout << "mesh blocked=" << blocked << " distant=" << distant << " ragdoll=" << z.ragdoll << "\n";
    require(z.ragdoll == (!blocked && !distant && publishedFrame != 0),
            "Only real, unoccluded contact with the published animated mesh may create a ragdoll");
    require(!player.punching(viewer::PlayerBodyPart::rightFist), "Hit or missed punches must recover to guard");
    require(!z.ragdoll || recoveredOnContact, "Every punch handoff must recover its arm on the contact step");
}

void idleArmContactCannotRagdoll() {
    auto world = std::make_shared<viewer::PhysicsWorld>();
    viewer::PlayerBody player(world, 1.7f);
    viewer::MeleeCombat melee(world);
    viewer::ZombieLayer zombies;
    zombies.assets.push_back(zombieCube()); zombies.instances.emplace_back();
    auto& zombie = zombies.instances[0];
    player.follow({0, 1.7f, 0}, {0, 0, -1}, true);
    const auto guardFist = player.fistPosition(false);
    zombie.restRoot = zombie.restTransform = zombie.transform =
        glm::translate(glm::mat4(1), glm::vec3(guardFist.x - .135f, 0, guardFist.z - .175f));
    zombies.attachPhysics(world); zombies.rebuildSpatialIndex();
    // Right punches away from the zombie while the left stays in guard.
    player.punch({1, 0, 0});
    bool leftContact = false, contactDuringOtherPunch = false;
    std::vector<b3ContactData> contacts;
    for (int f = 0; f < 20; ++f) {
        world->step(1.f / 60, [&](float s) { player.beforeStep(s); melee.prepare(player, &zombies, 0); }, [&](float) {
            for (const auto part : {viewer::PlayerBodyPart::leftUpperArm, viewer::PlayerBodyPart::leftLowerArm, viewer::PlayerBodyPart::leftFist}) {
                const auto body = player.body(part);
                contacts.resize(b3Body_GetContactCapacity(body));
                const int n = b3Body_GetContactData(body, contacts.data(), static_cast<int>(contacts.size()));
                for (int c = 0; c < n; ++c)
                    for (int m = 0; m < contacts[c].manifoldCount; ++m)
                        for (int p = 0; p < contacts[c].manifolds[m].pointCount; ++p)
                            if (contacts[c].manifolds[m].points[p].totalNormalImpulse > .001f) {
                                leftContact = true;
                                contactDuringOtherPunch |= player.punching(viewer::PlayerBodyPart::rightFist);
                            }
            }
            melee.finish(player, &zombies, 0); player.afterStep();
        });
        require(!zombie.ragdoll, "Guard-arm contact must not ragdoll a zombie while the other arm punches");
    }
    require(leftContact && contactDuringOtherPunch, "The fixture must physically contact the idle arm during the other punch");
}

void parkedVehicleStopsThePunch() {
    auto world = std::make_shared<viewer::PhysicsWorld>();
    viewer::VehicleLayer cars(world);
    cars.add({0,.68f,-1.35f}, glm::radians(90.f));
    viewer::PlayerBody player(world, 1.7f);
    player.follow({0,1.7f,0}, {0,0,-1}, true);
    player.punch({0,0,-1});
    bool contacted = false;
    for (int f=0; f<60 && player.punching(viewer::PlayerBodyPart::rightFist); ++f)
        world->step(1.f/60, [&](float step) {player.beforeStep(step);}, [&](float) {
            for (auto part : {viewer::PlayerBodyPart::rightUpperArm,viewer::PlayerBodyPart::rightLowerArm,viewer::PlayerBodyPart::rightFist})
                contacted |= solvedContact(player.body(part));
            player.afterStep();
        });
    require(contacted && player.returning(viewer::PlayerBodyPart::rightFist),
            "A parked car must stop the punching arm with real contact");
    require(cars.simulatedCars().empty(), "A punch must retain the parked car's cheap colliders");
}
void wallContactReturnsArmWithoutZombies() {
    auto world = std::make_shared<viewer::PhysicsWorld>();
    viewer::PlayerBody player(world, 1.7f);
    auto definition = b3DefaultBodyDef(); definition.position = {0, 1.45f, -.60f};
    const auto wall = b3CreateBody(world->id(), &definition);
    const auto hull = b3MakeBoxHull(1, 1, .03f);
    auto shape = b3DefaultShapeDef(); b3CreateHullShape(wall, &shape, &hull.base);
    player.follow({0, 1.7f, 0}, {0, 0, -1}, true);
    player.punch({0, 0, -1});
    bool contacted = false;
    std::vector<b3ContactData> contacts;
    for (int f = 0; f < 60 && !contacted; ++f) {
        world->step(1.f / 60, [&](float s) { player.beforeStep(s); }, [&](float) {
            const auto fist = player.body(viewer::PlayerBodyPart::rightFist);
            contacts.resize(b3Body_GetContactCapacity(fist));
            const int n = b3Body_GetContactData(fist, contacts.data(), static_cast<int>(contacts.size()));
            for (int c = 0; c < n; ++c)
                for (int m = 0; m < contacts[c].manifoldCount; ++m)
                    for (int p = 0; p < contacts[c].manifolds[m].pointCount; ++p)
                        contacted |= contacts[c].manifolds[m].points[p].totalNormalImpulse > .001f;
            player.afterStep();
            if (contacted) require(!player.punching(viewer::PlayerBodyPart::rightFist),
                                   "A wall hit must stop the punch on that physics step without zombies");
        });
    }
    require(contacted, "The wall fixture must produce a real fist contact");
    b3DestroyBody(wall);
}
}
int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--torso-punch") {
            torsoStopsWithTheStrike();
            torsoFollowsTheThrowingHand();
            std::cout << "Torso turn and stop checks passed\n";
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--pelvis-upright") {
            pelvisStaysUpright();
            torsoFollowsTheThrowingHand();
            std::cout << "Upright pelvis and torso punch-follow checks passed\n";
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--retract-pause") {
            bothEndingsWaitBeforeRetraction();
            impactWaitsBeforeRetraction();
            std::cout << "Punch retraction pause checks passed\n";
            return 0;
        }
        fistPullRotatesShoulder();
        slowGuardRecovery();
        returningArmCannotRagdoll();
        pelvisStaysUpright();
        torsoBendingAndTwisting();
        torsoFollowsTheThrowingHand();
        torsoStopsWithTheStrike();
        torsoFollowDoesNotRedirectThePunch();
        impactWaitsBeforeRetraction();
        bothEndingsWaitBeforeRetraction();
        anatomyAndDynamics();
        meshContact(false, false); meshContact(true, false); meshContact(false, true);
        meshContact(false, false, 0); meshContact(false, false, 1);
        idleArmContactCannotRagdoll();
        parkedVehicleStopsThePunch(); wallContactReturnsArmWithoutZombies();
        std::cout << "Melee tests passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
