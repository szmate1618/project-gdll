#include "zombie_lunge.hpp"
#include "zombie_layer.hpp"
#include "simulation_clock.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void near(float actual, float expected, const char* message, float tolerance = .0002f) {
    require(std::abs(actual - expected) < tolerance, message);
}

void quad(viewer::Model& model, const char* name,
          glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d) {
    viewer::Primitive primitive;
    for (const auto point : {a, b, c, d}) {
        viewer::Vertex vertex;
        vertex.position = point;
        vertex.normal = glm::normalize(glm::cross(b - a, c - a));
        primitive.vertices.push_back(vertex);
    }
    primitive.indices = {0, 1, 2, 0, 2, 3};
    model.draws.push_back({model.primitives.size(), glm::mat4(1), name});
    model.primitives.push_back(std::move(primitive));
}
viewer::Model floor(bool wall = false) {
    viewer::Model model;
    quad(model, "terrain", {-5, 0, -5}, {-5, 0, 5}, {5, 0, 5}, {5, 0, -5});
    if (wall) quad(model, "building_test_walls", {-5, 0, .5f}, {5, 0, .5f},
        {5, 3, .5f}, {-5, 3, .5f});
    return model;
}

struct Fixture {
    viewer::ZombieInstance zombie;
    viewer::ZombieLunge lunge;
    viewer::ZombieCrowd crowd;
    std::vector<viewer::ZombiePlayer> players{{{0, 0, 1}, 1.7f, 1.8f, 7}};
    std::vector<viewer::ZombieAttackHit> hits;
    std::size_t rays = 0;
    double now = 0;

    Fixture() { crowd.reset(1); crowd.update(0, {0, 0, 0}); }
    glm::vec3 feet() const { return glm::vec3(zombie.restRoot[3]); }
    void begin() {
        require(viewer::beginZombieLunge(lunge, zombie, players[0], now), "Eligible attack did not start");
    }
    void step(float seconds, const viewer::CollisionWorld& world) {
        now += seconds;
        viewer::stepZombieLunge(lunge, zombie, world, crowd, 0,
            players.data(), players.size(), seconds, now, hits, rays);
        crowd.update(0, feet());
    }
};

void relativeVelocityDamage() {
    near(viewer::zombieAttackDamage({0, 0, 10}, {0, 0, 0}), 10, "Stationary target must take base damage");
    near(viewer::zombieAttackDamage({0, 0, 10}, {0, 0, -5}), 15, "Moving into the zombie must increase damage");
    near(viewer::zombieAttackDamage({0, 0, 10}, {0, 0, 5}), 5, "Moving away must reduce damage");
    near(viewer::zombieAttackDamage({0, 0, 10}, {10, 0, 0}), std::sqrt(200.f),
        "Sideways velocity must use the full relative vector length");
    near(viewer::zombieAttackDamage({0, 0, 10}, {0, 0, 10}), 0, "Equal velocities must produce zero damage");
    near(viewer::zombieAttackDamage({0, 0, 10}, {0, 5, 0}), std::sqrt(125.f),
        "Vertical player velocity must participate in damage");
    require(viewer::zombieAttackDamage({0, 0, 10}, {0, 0, std::numeric_limits<float>::infinity()}) == 0,
        "Nonfinite velocity must not produce damage");
}

void contactAndRecovery() {
    const viewer::CollisionWorld world(floor());
    for (const float strikeStep : {1.f / 60, 1.f / 120, .15f}) {
        Fixture f;
        f.begin();
        f.step(.3f, world);
        near(f.feet().z, -.08f, "Wind-up must visibly withdraw the real body");
        require(f.hits.empty() && !f.zombie.walking, "Wind-up must retain idle without damage");
        while (f.lunge.phase == viewer::ZombieLunge::Phase::striking) f.step(strikeStep, world);
        require(f.hits.size() == 1 && f.hits[0].player == 7, "Swept contact must hit the player once");
        near(f.hits[0].damage, 10, "Contact clipping must not reduce the incoming base damage");
        near(f.feet().z, .4f, "Combined body radii must stop a one-meter approach after 40 cm");
        require(!f.zombie.walking && !f.zombie.running && f.zombie.transform == f.zombie.restTransform &&
            f.zombie.transform[3] == f.zombie.restRoot[3], "Idle presentation and collision feet must agree");
        f.step(.4f, world);
        near(f.feet().z, 0, "Unblocked recovery must return to the attack origin");
        require(f.hits.size() == 1 && f.lunge.phase == viewer::ZombieLunge::Phase::ready,
            "Recovery must finish without additional damage");
        require(!viewer::beginZombieLunge(f.lunge, f.zombie, f.players[0], f.now),
            "Recovery completion must retain the attack cooldown");
    }
}

void missesAndCurrentBodies() {
    const viewer::CollisionWorld world(floor());
    Fixture dodge;
    dodge.begin();
    dodge.step(.3f, world);
    dodge.players[0].feet.x = 2;
    dodge.step(.15f, world);
    require(dodge.hits.empty(), "A sidestep must miss a committed straight lunge");
    near(dodge.feet().x, 0, "A committed lunge must not turn after its player");
    near(dodge.feet().z, 1.42f, "A miss must travel at most 1.5 m from its withdrawn position");
    dodge.step(.4f, world);
    near(dodge.feet().z, 0, "A missed attack must still recover");

    Fixture jumping;
    jumping.begin();
    jumping.step(.3f, world);
    jumping.players[0].feet.y = 2;
    jumping.step(.15f, world);
    require(jumping.hits.empty(), "A vertically separated player must not collide with a horizontal lunge");

    Fixture nearer;
    nearer.players.push_back({{0, 0, .8f}, 1.7f, 1.8f, 3});
    nearer.begin();
    nearer.step(.3f, world);
    nearer.step(.15f, world);
    require(nearer.hits.size() == 1 && nearer.hits[0].player == 3,
        "The first player body must stop the strike, including a player other than its original target");
    near(nearer.feet().z, .2f, "The strike must stop at the nearest current player body");

    Fixture moving;
    moving.players[0].velocity = {0, 0, -5};
    moving.begin();
    moving.step(.3f, world);
    moving.step(.15f, world);
    require(moving.hits.size() == 1, "Moving target fixture did not collide");
    near(moving.hits[0].damage, 15, "Actual contact must apply relative-velocity damage");
}

void blockersAndInterruptedRecovery() {
    const viewer::CollisionWorld clear(floor()), wall(floor(true));
    Fixture blocked;
    blocked.begin();
    blocked.step(.3f, clear);
    blocked.step(.15f, wall);
    require(blocked.hits.empty() && blocked.feet().z < .16f,
        "A newly closed wall must stop motion without hitting a player on the rejected path");
    require(blocked.lunge.phase == viewer::ZombieLunge::Phase::recovering,
        "An obstacle must end the forward strike");

    Fixture crowd;
    crowd.crowd.reset(2);
    crowd.crowd.update(0, {0, 0, 0});
    crowd.crowd.update(1, {0, 0, .8f});
    crowd.begin();
    crowd.step(.3f, clear);
    crowd.step(.15f, clear);
    require(crowd.hits.empty() && crowd.feet().z < .21f,
        "A living crowd body must block the lunge before player contact");

    Fixture recovery;
    recovery.begin();
    recovery.step(.3f, clear);
    recovery.step(.15f, clear);
    const auto contactFeet = recovery.feet();
    recovery.players[0].feet = {0, 0, 0}; // The return path became occupied.
    recovery.step(.4f, clear);
    require(recovery.hits.size() == 1 && recovery.feet() == contactFeet,
        "Blocked recovery must stop safely and must not damage the blocking player again");
    require(recovery.lunge.phase == viewer::ZombieLunge::Phase::ready,
        "A blocked return must finish without teleporting to the old origin");
}

void supportedContactAndSlopeDamage() {
    auto model = floor();
    quad(model, "platform", {-5, 0, .55f}, {5, 0, .55f}, {5, .25f, .55f}, {-5, .25f, .55f});
    quad(model, "platform", {-5, .25f, .55f}, {-5, .25f, 5}, {5, .25f, 5}, {5, .25f, .55f});
    const viewer::CollisionWorld steps(model), clear(floor());
    bool climbed = false;
    for (int i = 0; i < 31; ++i) {
        Fixture f;
        f.players[0].feet.z = .8f + i * .01f;
        f.begin();
        f.step(.3f, clear);
        f.step(.15f, steps);
        climbed = climbed || f.feet().y > .05f;
        for (const auto& contact : steps.contacts(f.feet(), .3f, 1.8f))
            require(contact.terrainSupport || contact.depth < .0004f,
                "Contact partway through a walking step must publish a collision-validated endpoint");
        if (!f.hits.empty()) near(f.hits[0].damage, 10, "Step contact against a stationary player must retain base damage");
    }
    require(climbed, "Step contact regression never exercised a raised accepted endpoint");

    viewer::Model slope;
    quad(slope, "terrain", {-5, -1, -5}, {-5, 1, 5}, {5, 1, 5}, {5, -1, -5});
    const viewer::CollisionWorld slopeWorld(slope);
    Fixture uphill;
    uphill.players[0].feet.y = .2f;
    uphill.begin();
    uphill.step(.3f, slopeWorld);
    uphill.step(.15f, slopeWorld);
    require(uphill.hits.size() == 1, "Slope fixture must collide with its player");
    near(uphill.hits[0].damage, 10, "Slope direction must not increase stationary-player base damage");
}

void phaseTimingAndDeadAttackers() {
    const viewer::CollisionWorld world(floor());
    for (const int fps : {30, 60, 144}) {
        Fixture f;
        f.begin();
        f.players[0].feet.x = 3;
        while (f.lunge.phase != viewer::ZombieLunge::Phase::ready && f.now < 2) f.step(1.f / fps, world);
        require(f.now >= .85 - 1e-6 && f.now <= .85 + 1.0 / fps + 1e-6,
            "Phase boundaries must carry partial timesteps without accumulating extra delay");
        near(f.feet().z, 0, "Every timestep policy must complete the same miss/recovery travel");
        require(f.hits.empty(), "A missed attack must not produce catch-up hits");
    }
    for (const bool removed : {false, true}) {
        Fixture f;
        f.begin();
        f.step(.3f, world);
        f.zombie.removed = removed;
        f.zombie.ragdoll = !removed;
        const auto stopped = f.feet();
        f.step(.15f, world);
        require(f.hits.empty() && f.feet() == stopped && f.lunge.phase == viewer::ZombieLunge::Phase::ready,
            "A removed or ragdoll attacker must cancel its pending strike before moving or dealing damage");
    }
}

void behaviorEventsAndLifetime() {
    auto& clock = viewer::SimulationClock::instance();
    clock.reset();
    const viewer::CollisionWorld world(floor());
    viewer::ZombieLayer layer;
    layer.assets.emplace_back();
    layer.instances.emplace_back();
    layer.setPerceptionStaggering(false);
    const viewer::ZombiePlayer player{{0, 0, 1}};
    bool hit = false;
    for (int frame = 0; frame < 60 && !hit; ++frame) {
        clock.advance(1.0 / 60);
        layer.updateBehavior(1.f / 60, player, world);
        hit = !layer.attackHits().empty();
    }
    require(hit, "Behavior dispatch must advance the prototype attack and publish its hit");
    layer.updateBehavior(0, player, world);
    require(layer.attackHits().empty(), "Even an invalid/zero update must clear old hit events");
    layer.setPerceptionEnabled(false);
    clock.advance(1);
    layer.updateBehavior(1.f / 60, player, world);
    require(layer.attackHits().empty() && layer.behaviors.entries().empty(),
        "Disabling perception must stop pending attacks and publish no hits");
}

void disconnectCancelsCommittedAttack() {
    auto& clock = viewer::SimulationClock::instance();
    clock.reset();
    const viewer::CollisionWorld world(floor());
    viewer::ZombieLayer layer;
    layer.assets.emplace_back();
    layer.instances.emplace_back();
    layer.setPerceptionStaggering(false);
    std::vector<viewer::ZombiePlayer> players{{{0, 0, 1}, 1.7f, 1.8f, 7}};
    for (int frame = 0; frame < 30; ++frame) {
        clock.advance(1.0 / 60);
        layer.updateBehaviorForPlayers(1.f / 60, players, world, clock.seconds());
        const auto* behavior = layer.behaviors.find(0);
        if (behavior && behavior->lunge.phase == viewer::ZombieLunge::Phase::striking) break;
    }
    require(layer.behaviors.find(0)->lunge.phase == viewer::ZombieLunge::Phase::striking,
        "Disconnect fixture must reach its committed forward phase");
    const auto stopped = layer.instances[0].restRoot;
    clock.advance(1.0 / 60);
    layer.updateBehaviorForPlayers(1.f / 60, {}, world, clock.seconds());
    require(layer.attackHits().empty() && layer.instances[0].restRoot == stopped &&
        layer.behaviors.find(0)->lunge.phase == viewer::ZombieLunge::Phase::ready,
        "The last disconnect must cancel a committed attack immediately");
    players[0].id = 9;
    clock.advance(1.0 / 60);
    layer.updateBehaviorForPlayers(1.f / 60, players, world, clock.seconds());
    require(layer.attackHits().empty() && layer.behaviors.find(0)->lunge.phase == viewer::ZombieLunge::Phase::ready,
        "A newly joined player must not resume the disconnected player's old strike or bypass cooldown");
}
}

int main() {
    try {
        relativeVelocityDamage();
        contactAndRecovery();
        missesAndCurrentBodies();
        blockersAndInterruptedRecovery();
        supportedContactAndSlopeDamage();
        phaseTimingAndDeadAttackers();
        behaviorEventsAndLifetime();
        disconnectCancelsCommittedAttack();
        std::cout << "Zombie lunge tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Zombie lunge test failed: " << error.what() << '\n';
        return 1;
    }
}
