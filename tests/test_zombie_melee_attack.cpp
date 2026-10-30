#include "zombie_melee_attack.hpp"
#include "zombie_layer.hpp"
#include "zombie_animation.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

viewer::AnimatedModel asset() {
    viewer::AnimatedModel result;
    result.skeleton.resize(2);
    result.skeleton[0].name = "bip L Hand"; result.skeleton[1].name = "bip R Hand";
    for (bool right : {false, true}) {
        viewer::BakedAnimationClip clip;
        clip.duration = 1; clip.frameCount = 5; clip.loop = false;
        for (auto point : {glm::vec3(-.4f, 1, -.1f), {-.4f, 1, 0}, {0, 1, 1.1f},
                           {.4f, 1, .1f}, {-.4f, 1, -.1f}}) {
            if (right) point.x = -point.x;
            clip.boneFrames.push_back(glm::translate(glm::mat4(1), right ? glm::vec3(0) : point));
            clip.boneFrames.push_back(glm::translate(glm::mat4(1), right ? point : glm::vec3(0)));
        }
        (right ? result.attackRightClip : result.attackLeftClip) = clip;
    }
    return result;
}

viewer::Model scene(bool wall = false) {
    viewer::Model result;
    const auto quad = [&](const char* name, std::initializer_list<glm::vec3> points) {
        viewer::Primitive p;
        for (auto point : points) { viewer::Vertex v; v.position = point; p.vertices.push_back(v); }
        p.indices = {0, 1, 2, 0, 2, 3};
        result.draws.push_back({result.primitives.size(), glm::mat4(1), name});
        result.primitives.push_back(p);
    };
    quad("terrain", {{-5, 0, -5}, {-5, 0, 5}, {5, 0, 5}, {5, 0, -5}});
    if (wall) quad("building_test_walls", {{-5, 0, .45f}, {5, 0, .45f}, {5, 3, .45f}, {-5, 3, .45f}});
    return result;
}

void contactAndTiming() {
    const auto model = asset(); const viewer::CollisionWorld world(scene());
    for (float dt : {1.f/120, 1.f/60, .2f}) {
        viewer::ZombieInstance zombie; zombie.phase = .83f;
        zombie.displayedAnimationFrame = .5f;
        viewer::ZombieLunge cycle;
        std::vector<viewer::ZombieAttackHit> hits; std::size_t rays = 0;
        viewer::ZombiePlayer player{{0, 0, 1}, 1.7f, 1.8f, 7};
        require(viewer::beginZombieMeleeAttack(cycle, zombie, model, player, 0), "Authored attack did not start");
        require(zombie.attackClip == 1, "First attack did not select the left hand");
        require(zombie.displayedAnimationFrame == .5f,
            "Simulation discarded the last displayed pose before rendering its replacement");
        double time = 0;
        while (cycle.phase != viewer::ZombieLunge::Phase::ready) {
            time += dt;
            viewer::stepZombieMeleeAttack(cycle, zombie, model, world, 4, &player, 1, dt, time, hits, rays);
            if (time < .25) require(hits.empty(), "Windup injured a player");
        }
        require(hits.size() == 1 && hits[0].player == 7 && hits[0].damage == 10 && hits[0].zombie == 4,
            "Swept hand contact must emit exactly one base-damage hit at every timestep");
        require(glm::length(glm::vec3(zombie.restRoot[3])) < .00001f && !zombie.walking && !zombie.attackClip,
            "Authored attack moved its physical root or retained a finished clip");
        require(!viewer::beginZombieMeleeAttack(cycle, zombie, model, player, time), "Attack skipped cooldown");
        require(viewer::beginZombieMeleeAttack(cycle, zombie, model, player, time + .6), "Recovered attack could not restart");
        require(zombie.attackClip == 2, "Consecutive attacks did not alternate hands");
        zombie.attackSeconds = 100;
        const auto pose = viewer::sampleZombiePose(model, zombie, 123);
        require(pose.size() == 2,
            "Attack pose sampling failed");
        require(glm::distance(glm::vec3(pose[1][3]), { .4f, 1, -.1f }) < .0001f,
            "Attack animation wrapped or used the random locomotion phase");
    }
}

void missesAndObstacles() {
    const auto model = asset();
    for (int mode : {0, 1, 2, 3}) {
        const viewer::CollisionWorld world(scene(mode == 0));
        viewer::ZombieInstance zombie; viewer::ZombieLunge cycle;
        viewer::ZombiePlayer player{{0, 0, 1}, 1.7f, 1.8f, 7};
        require(viewer::beginZombieMeleeAttack(cycle, zombie, model, player, 0), "Miss fixture did not start");
        if (mode == 1) player.feet.x = 2;
        if (mode == 2) player.feet.y = 3;
        if (mode == 3) zombie.ragdoll = true;
        std::vector<viewer::ZombieAttackHit> hits; std::size_t rays = 0;
        for (int tick = 1; tick <= 120; ++tick)
            viewer::stepZombieMeleeAttack(cycle, zombie, model, world, 0, &player, 1, 1.f/60, tick/60., hits, rays);
        require(hits.empty(), "Wall, dodge, separate floor or interrupted attacker still caused injury");
    }
}

void onlyFirstContactInjures() {
    const auto model = asset(); const viewer::CollisionWorld world(scene());
    viewer::ZombieInstance zombie; viewer::ZombieLunge cycle;
    viewer::ZombiePlayer players[2] = {{{0, 0, 1}, 1.7f, 1.8f, 7}, {{0, 0, .8f}, 1.7f, 1.8f, 5}};
    viewer::beginZombieMeleeAttack(cycle, zombie, model, players[0], 0);
    std::vector<viewer::ZombieAttackHit> hits; std::size_t rays = 0;
    viewer::stepZombieMeleeAttack(cycle, zombie, model, world, 0, players, 2, 1.f, 1, hits, rays);
    require(hits.size() == 1 && hits[0].player == 5, "Swing did not hit only the first contacted player");
}

void authoredAssets(const std::filesystem::path& directory) {
    const viewer::CollisionWorld world(scene());
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().extension() != ".glb") continue;
        auto layer = viewer::loadZombieLayer(entry.path(), {glm::mat4(1)});
        auto& zombie = layer.instances[0];
        const auto& model = layer.assets[0];
        require(viewer::hasZombieMeleeAttack(model), "Prepared asset lacks attack clips or hand bones");
        viewer::ZombieLunge cycle;
        viewer::ZombiePlayer player{{0, 0, 1}, 1.7f, 1.8f, 7};
        double now = 0;
        for (int side = 0; side < 2; ++side) {
            require(viewer::beginZombieMeleeAttack(cycle, zombie, model, player, now), "Prepared attack could not start");
            std::vector<viewer::ZombieAttackHit> hits; std::size_t rays = 0;
            while (cycle.phase != viewer::ZombieLunge::Phase::ready) {
                now += 1.0/120;
                viewer::stepZombieMeleeAttack(cycle, zombie, model, world, 0, &player, 1, 1.f/120, now, hits, rays);
            }
            if (hits.size() != 1) throw std::runtime_error("Prepared hand missed at approach range: " + entry.path().string() +
                (side ? " right" : " left"));
            now += .6;
        }
    }
}
}

int main(int argc, char** argv) {
    try {
        contactAndTiming(); missesAndObstacles(); onlyFirstContactInjures();
        if (argc > 1) authoredAssets(argv[1]);
        std::cout << "Baked zombie attacks, swept hand contacts and one-hit injuries passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
