#include "door_config.hpp"
#include "door_layer.hpp"
#include "physics_world.hpp"
#include <box3d/box3d.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F run) {
    bool rejected = false;
    try { run(); } catch (const std::exception&) { rejected = true; }
    require(rejected, "Invalid door configuration was accepted");
}

void configuredClosedTravel(const std::filesystem::path& path) {
    for (const bool inward : {false, true}) {
        for (const float width : {.5f, 1.f, 2.f}) {
            for (const float travel : {0.f, .03f, .15f}) {
                { std::ofstream file(path); file << "max_closed_flex_m = " << travel << "\nbreak_flex_m = 0\n"; }
                viewer::DoorPanel panel;
                panel.hinge = {0, 1.1f, 0};
                panel.center = panel.hinge + glm::vec3(width / 2, 0, 0);
                panel.halfExtents = {width / 2, 1, .02f};
                panel.outward = {0, 0, 1};
                panel.inward = inward;
                auto world = std::make_shared<viewer::PhysicsWorld>();
                viewer::DoorLayer doors({}, world, viewer::loadDoorConfig(path));
                const auto id = doors.reserveRuntimeDoors(1);
                doors.installRuntimeDoor(id, panel);
                const float direction = inward ? -1.f : 1.f;
                float deepest = 0;
                for (int frame = 0; frame < 120; ++frame) {
                    const auto edge = doors.pointState(id, {width / 2, 0, 0}).point;
                    doors.applyForce(id, -direction * panel.outward * 2000.f, edge);
                    world->step(1.f / 60);
                    const auto moved = doors.pointState(id, {width / 2, 0, 0}).point;
                    const float reverseTravel = -direction * glm::dot(moved - panel.hinge, panel.outward);
                    deepest = std::max(deepest, reverseTravel);
                }
                require(deepest <= travel + .015f, "Configured reverse travel must bound the free edge on every panel width");
                if (travel > 0)
                    require(deepest >= travel * .8f, "Configured travel must be reachable under sustained pressure");
                doors.evictRuntimeDoor(id);
                world->step(1.f / 60);
            }
        }
    }
}

void physicalRestraint() {
    struct Impact { bool blocked; float impulse; };
    for (const bool inward : {false, true}) {
        for (const auto test : {Impact{false, 6.f}, Impact{false, 12.f}, Impact{true, 6.f}, Impact{true, 12.f}}) {
            const bool blocked = test.blocked;
            const float impulse = test.impulse;
            auto world = std::make_shared<viewer::PhysicsWorld>();
            viewer::DoorPanel panel;
            panel.hinge = {0, 1.1f, 0}; panel.center = {.5f, 1.1f, 0};
            panel.halfExtents = {.5f, 1, .02f}; panel.outward = {0, 0, 1}; panel.inward = inward;
            viewer::DoorLayer doors({panel}, world);
            const float direction = inward ? -1.f : 1.f;
            b3BodyId blocker{};
            if (blocked) {
                auto definition = b3DefaultBodyDef();
                definition.position = {.5, 1.1, -direction * .523f};
                blocker = b3CreateBody(world->id(), &definition);
                const auto hull = b3MakeBoxHull(.7f, 1.2f, .5f);
                const auto shape = b3DefaultShapeDef();
                b3CreateHullShape(blocker, &shape, &hull.base);
            }
            float previous = doors.restraint(0).maxFlexMeters;
            int stretchedHits = 0;
            for (int hit = 0; hit < 16 && !doors.restraint(0).broken; ++hit) {
                const auto edge = doors.pointState(0, {.45f, 0, 0}).point;
                doors.applyImpulse(0, -direction * panel.outward * impulse, edge);
                for (int frame = 0; frame < 90; ++frame) world->step(1.f / 60);
                const auto state = doors.restraint(0);
                if (state.maxFlexMeters > previous + .0001f) ++stretchedHits;
                require(state.maxFlexMeters >= previous, "Permanent stretch must not heal between hits");
                previous = state.maxFlexMeters;
            }
            const auto state = doors.restraint(0);
            if (blocked || impulse == 6.f) {
                require(!state.broken && state.maxFlexMeters == .01f,
                        "Repeated hits must not weaken a panel that stays within its elastic allowance");
                if (blocked) b3DestroyBody(blocker);
            } else {
                require(state.broken && stretchedHits > 1, "Repeated physical overstretch must progressively release the restraint");
                for (const float sign : {-1.f, 1.f}) {
                    doors.setAngle(0, 0);
                    doors.applyImpulse(0, sign * panel.outward * 20.f, doors.pointState(0, {.45f, 0, 0}).point);
                    for (int frame = 0; frame < 60; ++frame) world->step(1.f / 60);
                    require(sign * doors.angle(0) > .2f, "A released door must swing freely in both directions");
                }
            }
        }
    }
}

void replicatedRestraint() {
    auto world = std::make_shared<viewer::PhysicsWorld>();
    viewer::DoorLayer doors({}, world, {}, true);
    const auto id = doors.reserveRuntimeDoors(1);
    doors.setRestraint(id, {.02f, false});
    viewer::DoorPanel panel;
    panel.hinge = {0, 1, 0}; panel.center = {.5f, 1, 0};
    panel.halfExtents = {.5f, 1, .02f}; panel.outward = {0, 0, 1};
    doors.installRuntimeDoor(id, panel);
    doors.applyImpulse(id, {0, 0, -40}, {.95f, 1, 0});
    for (int i = 0; i < 60; ++i) world->step(1.f / 60);
    require(doors.restraint(id).maxFlexMeters == .02f && !doors.restraint(id).broken,
            "Replicas must not invent strain damage from local physics");
    doors.evictRuntimeDoor(id);
    doors.setRestraint(id, {.05f, true});
    doors.installRuntimeDoor(id, panel);
    doors.applyImpulse(id, {0, 0, -20}, {.95f, 1, 0});
    for (int i = 0; i < 60; ++i) world->step(1.f / 60);
    require(doors.angle(id) < -.2f && doors.restraint(id).broken,
            "Release received while nonresident must survive recreation and permit reverse swing");
    doors.setRestraint(id, {.01f, false});
    require(doors.restraint(id).broken && doors.restraint(id).maxFlexMeters == .05f,
            "Older playback state must not repair permanent restraint damage");
}
} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "Fixture directory missing");
        std::filesystem::create_directories(argv[1]);
        const auto path = std::filesystem::path(argv[1]) / "doors.toml";
        const auto write = [&](const char* contents) { std::ofstream file(path); file << contents; };
        write("max_closed_flex_m = 0.03\n");
        require(viewer::loadDoorConfig(path).maxClosedFlexMeters == .03f, "Configured travel must load in meters");
        write("max_closed_flex_m = 0\n");
        require(viewer::loadDoorConfig(path).maxClosedFlexMeters == 0, "Zero travel must be accepted");
        for (const auto* invalid : {"", "max_closed_flex_m = '10 cm'", "max_closed_flex_m = true",
                                    "max_closed_flex_m = -0.01", "max_closed_flex_m = -1e-100",
                                    "max_closed_flex_m = nan", "max_closed_flex_m = inf",
                                    "max_closed_flex_m = 1e100", "max_closed_flex_m = ["}) {
            write(invalid);
            rejects([&] { viewer::loadDoorConfig(path); });
        }
        rejects([&] { viewer::loadDoorConfig(path.parent_path() / "missing.toml"); });
        for (const auto* invalid : {"break_flex_m = -1", "break_flex_m = -1e-100", "break_flex_m = nan",
                                    "break_flex_m = inf", "break_flex_m = true", "break_flex_m = 0.005"}) {
            const auto text = std::string("max_closed_flex_m = 0.01\n") + invalid;
            write(text.c_str());
            rejects([&] { viewer::loadDoorConfig(path); });
        }
        for (const auto invalid : {-1.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
            rejects([&] { viewer::validateDoorConfig({invalid}); });
        configuredClosedTravel(path);
        physicalRestraint();
        replicatedRestraint();
        std::cout << "Door configuration tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
