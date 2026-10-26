#include "zombie_layer.hpp"
#include "zombie_animation.hpp"
#include "zombie_chase.hpp"
#include "simulation_clock.hpp"
#include "door_layer.hpp"
#include "physics_world.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void quad(viewer::Model& model, const std::string& name, glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d,
          bool detail = false, bool low = false) {
    viewer::Primitive primitive;
    const auto normal = glm::normalize(glm::cross(b - a, c - a));
    for (auto position : {a, b, c, d}) {
        viewer::Vertex vertex;
        vertex.position = position;
        vertex.normal = normal;
        primitive.vertices.push_back(vertex);
    }
    primitive.indices = {0, 1, 2, 0, 2, 3};
    model.draws.push_back({model.primitives.size(), glm::mat4(1), name, detail ? 120.f : 0, low,
                           detail ? "building_trace" : ""});
    model.primitives.push_back(std::move(primitive));
}

viewer::AnimatedModel timingAsset() {
    viewer::AnimatedModel asset;
    asset.duration = 2; asset.frameCount = 60;
    asset.walkClip = viewer::BakedAnimationClip{"walk", 1, 30, 60, {}};
    asset.runClip = viewer::BakedAnimationClip{"running", .8f, 24, 90, {}};
    return asset;
}

struct Previous {
    glm::vec3 feet{0};
    float yaw = 0;
    int clip = 0;
    double lastSwitch = -100;
    bool attacking = false;
    viewer::ZombieLunge::Phase attackPhase = viewer::ZombieLunge::Phase::ready;
    double walkSeconds = 0;
};
struct Metrics {
    std::size_t yawJumps = 0, rapidSwitches = 0, smallBackslides = 0, movingFrames = 0, positionJumps = 0;
    std::size_t attackFrames = 0;
    double maxYaw = 0;
};

Metrics scenario(const std::string& name, const viewer::AnimatedModel& asset, const std::filesystem::path& directory) {
    viewer::SimulationClock::instance().reset();
    const bool wall = name == "wall" || name == "single_wall";
    const bool door = name == "door";
    const bool movingTarget = name == "moving_target";
    const int side = name == "single_wall" || movingTarget ? 1 : 10;
    viewer::Model model;
    quad(model, "terrain", {-50, 0, -50}, {-50, 0, 50}, {50, 0, 50}, {50, 0, -50});
    if (wall) quad(model, "building_trace_walls", {0, 0, -30}, {0, 0, 30}, {0, 3, 30}, {0, 3, -30});
    if (door) {
        const auto panel = [&](float left, float bottom, float right, float top, const std::string& suffix, bool low = false) {
            quad(model, "building_trace_" + suffix, {left, bottom, 0}, {right, bottom, 0},
                 {right, top, 0}, {left, top, 0}, true, low);
        };
        panel(-20, 0, 20, 3, "walls", true);
        panel(-20, 0, -.5f, 3, "detailed_walls");
        panel(.5f, 0, 20, 3, "detailed_walls");
        panel(-.5f, 2.1f, .5f, 3, "detailed_walls");
        panel(-.5f, 0, .5f, 2.1f, "doors");
    }
    auto physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::DoorLayer doors(viewer::prepareDoorPanels(model), physics);
    const viewer::CollisionWorld world(model, {}, &doors);
    glm::vec3 target = wall ? glm::vec3(4, 0, 0) : door ? glm::vec3(0, 0, -4) :
        movingTarget ? glm::vec3(0, 0, 12) : glm::vec3(0, 0, 0);
    viewer::ZombieLayer layer;
    layer.assets.push_back(asset);
    std::vector<Previous> previous;
    for (int z = 0; z < side; ++z) for (int x = 0; x < side; ++x) {
        const glm::vec3 feet = wall ? glm::vec3(-.4f - x * .7f, 0, (z - side * .5f) * .7f) :
            door ? glm::vec3((x - side * .5f) * .7f, 0, .8f + z * .7f) :
                   glm::vec3((x - side * .5f) * .7f, 0, -3.f - z * .7f);
        const auto direction = target - feet;
        const float yaw = std::atan2(direction.x, direction.z);
        viewer::ZombieInstance instance;
        instance.restRoot = glm::rotate(glm::translate(glm::mat4(1), feet), yaw, {0, 1, 0});
        instance.transform = instance.restTransform = instance.restRoot;
        instance.phase = float(layer.instances.size() % 17) / 17;
        layer.behaviors.create(layer.instances.size(), 0).state = viewer::ZombieState::chasing;
        layer.behaviors.find(layer.instances.size())->target = target;
        layer.instances.push_back(instance);
        previous.push_back({feet, glm::degrees(yaw), 0, -100});
    }
    layer.rebuildSpatialIndex();
    std::ofstream log;
    if (!directory.empty()) {
        std::filesystem::create_directories(directory);
        log.open(directory / (name + ".csv"));
        if (!log) throw std::runtime_error("Cannot open motion trace");
        log << std::setprecision(10) << "step,id,seconds,dt,x,y,z,yaw_deg,walking,running,clip,frame,frame_count,walk_seconds,state,sight,target_x,target_z,displacement,yaw_delta,neighbor_checks,budget_stops,attack_phase\n";
    }
    Metrics metrics;
    double seconds = 0;
    for (int step = 0; step < 600; ++step) {
        const float dt = 1.f / 60;
        seconds += dt;
        viewer::SimulationClock::instance().advance(dt);
        if (movingTarget) {
            target = {12 * std::sin(seconds * .3), 0, 12 * std::cos(seconds * .3)};
            layer.behaviors.find(0)->target = target;
        }
        if (door) {
            const auto& p = doors.panel(0);
            const auto edge = p.center + glm::cross(glm::vec3(0, 1, 0), p.outward) * (p.halfExtents.x * .9f);
            const auto point = glm::vec3(doors.transform(0) * glm::inverse(p.closedTransform) * glm::vec4(edge, 1));
            const float sign = step >= 180 && step < 300 ? -1.f : 1.f;
            doors.applyForce(0, glm::cross(glm::vec3(0, -sign, 0), point - p.hinge) * 100.f, point);
            physics->step(dt);
        }
        layer.updateBehavior(dt, {target}, world, seconds);
        for (std::size_t id = 0; id < layer.instances.size(); ++id) {
            const auto& instance = layer.instances[id];
            const auto& a = layer.assets[instance.asset];
            const auto* clip = viewer::zombieAnimationClip(a, instance);
            const int type = clip == (a.runClip ? &*a.runClip : nullptr) && clip ? 2 : clip ? 1 : 0;
            const auto count = clip ? clip->frameCount : a.frameCount;
            const auto frame = viewer::animatedFrame(clip ? clip->duration : a.duration, count,
                clip ? instance.walkSeconds : seconds, instance.phase);
            const auto feet = glm::vec3(instance.restRoot[3]);
            const float yaw = glm::degrees(std::atan2(instance.restRoot[2].x, instance.restRoot[2].z));
            auto& old = previous[id];
            const auto* behavior = layer.behaviors.find(id);
            const auto attackPhase = behavior ? behavior->lunge.phase : viewer::ZombieLunge::Phase::ready;
            const bool attacking = behavior && behavior->state == viewer::ZombieState::attacking;
            const bool attackMovement = old.attackPhase != viewer::ZombieLunge::Phase::ready ||
                attackPhase != viewer::ZombieLunge::Phase::ready;
            const float displacement = glm::length(glm::vec2(feet.x - old.feet.x, feet.z - old.feet.z));
            const float delta = std::abs(std::remainder(yaw - old.yaw, 360.f));
            require(delta <= 180 * dt + .001f, "Locomotion must not snap the body's orientation");
            if (old.attackPhase == viewer::ZombieLunge::Phase::striking ||
                    old.attackPhase == viewer::ZombieLunge::Phase::recovering)
                require(delta < .001f, "A committed strike and return must retain their facing");
            if (attackPhase != viewer::ZombieLunge::Phase::ready) {
                require(type == 0 && !instance.walking && !instance.running,
                        "Physical attacks must retain the existing idle animation");
                if (old.attackPhase != viewer::ZombieLunge::Phase::ready)
                    require(instance.walkSeconds == old.walkSeconds,
                            "Attack travel must not advance walk/run clip time");
            }
            if (attackMovement)
                require(displacement <= viewer::zombieLungeSpeed * dt + .001f,
                        "Attack travel must respect its configured maximum speed");
            require(std::isfinite(frame) && frame >= 0 && frame < count,
                    "The displayed animation frame must remain valid");
            if (step > 120) {
                metrics.yawJumps += delta > 45;
                metrics.maxYaw = std::max(metrics.maxYaw, double(delta));
                metrics.movingFrames += displacement / dt > 1;
                const auto offset = feet - old.feet;
                const float backward = offset.x * std::sin(glm::radians(old.yaw)) +
                    offset.z * std::cos(glm::radians(old.yaw));
                if (!attackMovement && displacement > 1e-5f && displacement < .02f &&
                        displacement / dt < .1f && backward < -1e-5f) {
                    ++metrics.smallBackslides;
                    require(delta < .001f, "Tiny backward contact slides must keep facing");
                }
                if (!attackMovement) metrics.positionJumps += displacement > 3 * dt + .001f;
                metrics.attackFrames += attackMovement;
            }
            if (type != old.clip) {
                if (!attacking && !old.attacking && step > 120 && seconds - old.lastSwitch < .15)
                    ++metrics.rapidSwitches;
                old.lastSwitch = seconds;
            }
            if (log) log << step << ',' << id << ',' << seconds << ',' << dt << ',' << feet.x << ',' << feet.y << ','
                << feet.z << ',' << yaw << ',' << instance.walking << ',' << instance.running << ',' << type << ','
                << frame << ',' << count << ',' << instance.walkSeconds << ',' << (behavior ? int(behavior->state) : -1) << ','
                << (behavior ? int(behavior->sight) : -1) << ',' << (behavior ? behavior->target.x : 0) << ','
                << (behavior ? behavior->target.z : 0) << ',' << displacement << ',' << delta << ','
                << layer.crowdStats().neighborChecks << ',' << layer.crowdStats().budgetStops << ','
                << int(attackPhase) << '\n';
            old.feet = feet; old.yaw = yaw; old.clip = type;
            old.attacking = attacking; old.attackPhase = attackPhase; old.walkSeconds = instance.walkSeconds;
        }
    }
    std::cout << name << ": yaw jumps=" << metrics.yawJumps << ", max yaw=" << metrics.maxYaw
              << ", rapid clip switches=" << metrics.rapidSwitches << ", small backslides=" << metrics.smallBackslides
              << ", moving frames=" << metrics.movingFrames
              << ", position jumps=" << metrics.positionJumps << ", attack frames=" << metrics.attackFrames << '\n';
    require(metrics.yawJumps == 0 && metrics.rapidSwitches == 0 && metrics.positionJumps == 0,
            "Crowd/door contact must not cause orientation, clip or position flicker");
    if (name == "crowd") require(metrics.smallBackslides > 100, "Keep physical backward crowd slides");
    if (movingTarget) require(metrics.movingFrames > 400 && metrics.maxYaw > .1,
                             "Stable presentation must still permit continuous pursuit and turning");
    return metrics;
}

void locomotionTransitions() {
    // Exercise brief and sustained obstruction independently of crowd packing.
    const viewer::CollisionWorld clear(viewer::Model{});
    const auto asset = timingAsset();
    for (const int fps : {30, 60, 144}) {
        viewer::ZombieInstance instance;
        const float dt = 1.f / fps;
        viewer::moveZombieToward(instance, asset, {0, 0, 10}, 3, dt, clear, 0);
        viewer::Model model;
        const float wallZ = instance.restRoot[3].z + .352f; // Body + wall clearance + collision skin.
        quad(model, "building_trace_walls", {-10, 0, wallZ}, {10, 0, wallZ},
             {10, 3, wallZ}, {-10, 3, wallZ});
        const viewer::CollisionWorld blocked(model);
        const auto clock = instance.walkSeconds;
        for (int step = 0; step < fps / 10; ++step) {
            viewer::moveZombieToward(instance, asset, {0, 0, 10}, 3, dt, blocked, 0);
            require(instance.walking && instance.walkSeconds == clock,
                    "Brief blockage must retain the same locomotion pose");
        }
        for (int step = 0; step < fps; ++step)
            viewer::moveZombieToward(instance, asset, {0, 0, 10}, 3, dt, blocked, 0);
        require(!instance.walking, "Sustained blockage must settle into idle");
        require(glm::length(instance.locomotionVelocity) < 1e-4f,
                "Blocked zombies must not retain their requested running velocity");
        const auto stopped = glm::vec3(instance.restRoot[3]);
        for (int step = 0; step < fps / 2; ++step)
            viewer::moveZombieToward(instance, asset, {0, 0, 10}, 3, dt, clear, 0);
        require(instance.walking && instance.walkSeconds > clock && instance.restRoot[3].z > stopped.z + 1.4f,
                "Removing an obstacle must resume physical motion and its gait");
        const float yaw = std::atan2(instance.restRoot[2].x, instance.restRoot[2].z);
        viewer::moveZombieToward(instance, asset, {0, 0, -10}, 3, dt, clear, 0);
        const float turned = std::atan2(instance.restRoot[2].x, instance.restRoot[2].z);
        require(std::abs(turned - yaw) > .01f && std::abs(turned - yaw) <= glm::radians(180.f) * dt + 1e-4f,
                "A substantial reverse movement must turn gradually");
        viewer::stopZombieLocomotion(instance);
        require(!instance.walking && instance.blockedAnimationSeconds == 0 && instance.animationRestartSeconds == 0,
                "Explicit behavior stops must bypass the obstruction hold");
        require(instance.locomotionVelocity == glm::vec3(0), "Explicit stops must clear travel velocity");
    }
}
}

int main(int argc, char** argv) {
    try {
        require(argc <= 3, "Usage: test_zombie_motion [trace-directory [zombie.glb]]");
        const std::filesystem::path trace = argc > 1 ? argv[1] : "";
        const auto asset = argc > 2 ? viewer::loadZombieModel(argv[2]) : timingAsset();
        require(asset.walkClip.has_value() && asset.runClip.has_value(), "Trace fixture needs idle/walk/run clips");
        std::cout << "Clip timing: idle=" << asset.duration << '/' << asset.frameCount << ", walk="
                  << asset.walkClip->duration << '/' << asset.walkClip->frameCount << ", run="
                  << asset.runClip->duration << '/' << asset.runClip->frameCount << '\n';
        locomotionTransitions();
        for (const std::string name : {"crowd", "wall", "single_wall", "door", "moving_target"}) scenario(name, asset, trace);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Zombie motion trace failed: " << e.what() << '\n';
        return 1;
    }
}
