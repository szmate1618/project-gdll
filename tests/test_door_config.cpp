#include "door_config.hpp"
#include "door_layer.hpp"
#include "physics_world.hpp"
#include <box3d/box3d.h>

#include <algorithm>
#include <cmath>
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

struct RestraintFixture {
    std::shared_ptr<viewer::PhysicsWorld> world = std::make_shared<viewer::PhysicsWorld>();
    viewer::DoorPanel panel;
    viewer::DoorLayer doors;
    float direction;

    explicit RestraintFixture(bool inward)
        : panel(makePanel(inward)), doors({panel}, world), direction(inward ? -1.f : 1.f) {}

    static viewer::DoorPanel makePanel(bool inward) {
        viewer::DoorPanel result;
        result.hinge = {0, 1.1f, 0}; result.center = {.5f, 1.1f, 0};
        result.halfExtents = {.5f, 1, .02f}; result.outward = {0, 0, 1}; result.inward = inward;
        return result;
    }
    glm::vec3 edge() const { return doors.pointState(0, {.45f, 0, 0}).point; }
    void pressure(float force, float applicationStep = 1.f / 60) {
        // A continuous force must reach every fixed physics step, even when the
        // application advances several steps in one call or none at all.
        world->step(applicationStep, [&](float) {
            doors.applyForce(0, -direction * panel.outward * force, edge());
        });
    }
    void settle(int frames = 240) {
        for (int frame = 0; frame < frames; ++frame) world->step(1.f / 60);
    }
};

void physicalSlamNoise() {
    RestraintFixture fixture(false);
    fixture.doors.setAngle(0,.5f); fixture.world->step(1.f/60);
    require(fixture.doors.takeNoises().empty(),"Opening a door must not emit a slam noise");
    fixture.doors.applyImpulse(0,{0,0,-20},fixture.edge());
    bool heard = false;
    for (int i = 0; i < 180; ++i) {
        fixture.world->step(1.f/60);
        for (const auto& noise : fixture.doors.takeNoises()) {
            require(noise.kind == viewer::NoiseEvent::Kind::doorSlam && noise.audience == viewer::NoiseAudience::everyone &&
                noise.decibels >= 52 && noise.decibels <= 70,"Physical slam produced the wrong audience or source level");
            heard = true;
        }
    }
    require(heard,"A real fast closure must emit gameplay noise without an audio system");
}

double pressureBreakTime(bool inward, float force, float applicationStep = 1.f / 60) {
    RestraintFixture fixture(inward);
    double elapsed = 0;
    while (elapsed < 60 && !fixture.doors.restraint(0).broken) {
        fixture.pressure(force, applicationStep);
        elapsed += applicationStep;
        if (elapsed <= 20)
            require(!fixture.doors.restraint(0).broken, "Sustained pressure must not immediately release the restraint");
    }
    require(fixture.doors.restraint(0).broken, "A sustained overload must eventually release the restraint");
    const auto noises = fixture.doors.takeNoises();
    const auto breaking = std::count_if(noises.begin(),noises.end(),[](const auto& noise) {
        return noise.kind == viewer::NoiseEvent::Kind::doorBreak && noise.audience == viewer::NoiseAudience::playersOnly &&
            noise.decibels == viewer::doorBreakDecibels;
    });
    require(breaking == 1,"Breaking a restraint must emit one player-only noise without needing audio playback");
    for (const float sign : {-1.f, 1.f}) {
        fixture.doors.setAngle(0, 0);
        fixture.doors.applyImpulse(0, sign * fixture.panel.outward * 20.f, fixture.edge());
        fixture.settle(60);
        require(sign * fixture.doors.angle(0) > .2f, "A released door must swing freely in both directions");
    }
    const auto after = fixture.doors.takeNoises();
    require(std::none_of(after.begin(),after.end(),[](const auto& noise) { return noise.kind == viewer::NoiseEvent::Kind::doorBreak; }),
        "A broken restraint must not keep emitting break sounds");
    return elapsed;
}

void physicalRestraint() {
    for (const bool inward : {false, true}) {
        const double reference = pressureBreakTime(inward, 180.f);
        require(reference >= 35 && reference <= 50,
                "A finite 180 N push must yield gradually according to its physical load");
        require(pressureBreakTime(inward, 360.f) < reference,
                "A stronger sustained overload must release the restraint sooner");
        {
            RestraintFixture fixture(inward);
            const auto initial = fixture.doors.restraint(0).maxFlexMeters;
            for (int frame = 0; frame < 40 * 60; ++frame) fixture.pressure(80.f);
            require(!fixture.doors.restraint(0).broken && fixture.doors.restraint(0).maxFlexMeters == initial,
                    "A push within the elastic allowance must not gradually damage the restraint");
        }
        {
            RestraintFixture fixture(inward);
            auto definition = b3DefaultBodyDef();
            definition.position = {.5, 1.1, -fixture.direction * .523f};
            const auto blocker = b3CreateBody(fixture.world->id(), &definition);
            const auto hull = b3MakeBoxHull(.7f, 1.2f, .5f);
            const auto shape = b3DefaultShapeDef();
            b3CreateHullShape(blocker, &shape, &hull.base);
            const auto initial = fixture.doors.restraint(0).maxFlexMeters;
            for (int frame = 0; frame < 40 * 60; ++frame) fixture.pressure(360.f);
            require(!fixture.doors.restraint(0).broken && fixture.doors.restraint(0).maxFlexMeters == initial,
                    "Strong pressure must not weaken a panel physically blocked before its elastic travel");
            b3DestroyBody(blocker);
        }
        {
            RestraintFixture fixture(inward);
            float previous = fixture.doors.restraint(0).maxFlexMeters;
            int advances = 0;
            for (int hit = 0; hit < 8 && !fixture.doors.restraint(0).broken; ++hit) {
                fixture.doors.applyImpulse(0, -fixture.direction * fixture.panel.outward * 40.f, fixture.edge());
                fixture.settle(90);
                const auto state = fixture.doors.restraint(0);
                if (hit == 0)
                    require(!state.broken, "A single strong impact must not immediately release the restraint");
                require(state.maxFlexMeters >= previous, "Permanent stretch must not heal between impacts");
                advances += state.maxFlexMeters > previous;
                previous = state.maxFlexMeters;
                if (hit == 0) {
                    fixture.settle();
                    const auto settled = fixture.doors.restraint(0).maxFlexMeters;
                    fixture.settle();
                    require(fixture.doors.restraint(0).maxFlexMeters == settled,
                            "An unloaded panel must retain its damage without continuing to weaken");
                    previous = settled;
                }
            }
            require(advances > 1, "Separate physical impacts must continue accumulating permanent stretch");
        }
    }
    const double lowRate = pressureBreakTime(false, 180.f, 1.f / 30);
    const double highRate = pressureBreakTime(false, 180.f, 1.f / 120);
    require(std::abs(lowRate - highRate) < .1,
            "Material yielding must follow fixed physics time rather than the application frame rate");
}

void replicatedRestraint() {
    auto world = std::make_shared<viewer::PhysicsWorld>();
    viewer::DoorLayer doors({}, world, {}, true);
    const auto id = doors.reserveRuntimeDoors(1);
    doors.setRestraint(id, {.03f, false});
    viewer::DoorPanel panel;
    panel.hinge = {0, 1, 0}; panel.center = {.5f, 1, 0};
    panel.halfExtents = {.5f, 1, .02f}; panel.outward = {0, 0, 1};
    doors.installRuntimeDoor(id, panel);
    doors.applyImpulse(id, {0, 0, -40}, {.95f, 1, 0});
    for (int i = 0; i < 60; ++i) world->step(1.f / 60);
    require(doors.restraint(id).maxFlexMeters == .03f && !doors.restraint(id).broken,
            "Replicas must not invent strain damage from local physics");
    require(doors.takeNoises().empty(),"Replica pose updates must not originate simulation noises");
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
        require(viewer::DoorConfig{}.maxClosedFlexMeters == .02f, "Default elastic travel must be two centimeters");
        configuredClosedTravel(path);
        physicalSlamNoise();
        physicalRestraint();
        replicatedRestraint();
        std::cout << "Door configuration tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
