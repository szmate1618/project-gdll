#include "character_step.hpp"
#include "fps_controller.hpp"
#include "zombie_movement.hpp"
#include "zombie_crowd.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void quad(viewer::Model& model, const std::string& name, glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d) {
    viewer::Primitive primitive;
    for (const auto point : {a, b, c, d}) {
        viewer::Vertex vertex;
        vertex.position = point;
        primitive.vertices.push_back(vertex);
    }
    primitive.indices = {0, 1, 2, 0, 2, 3};
    model.draws.push_back({model.primitives.size(), glm::mat4(1), name});
    model.primitives.push_back(std::move(primitive));
}
viewer::Model ledge(float height, int kind) {
    viewer::Model model;
    quad(model, "terrain", {-10, 0, -10}, {-10, 0, 10}, {10, 0, 10}, {10, 0, -10});
    if (kind == 1) {
        // Real generated bottom caps face down, while their exposed upper side
        // must support walking. Do not change the geometry's winding to test it.
        quad(model, "building_test_walls", {0, height, -3}, {5, height, -3}, {5, height, 3}, {0, height, 3});
    } else {
        quad(model, kind == 0 ? "roads_residential" : "pavement", {0, height, -3},
             {0, height, 3}, {5, height, 3}, {5, height, -3});
        if (kind == 2)
            quad(model, "curb", {0, 0, -3}, {0, 0, 3}, {0, height, 3}, {0, height, -3});
    }
    return model;
}
void clear(glm::vec3 feet, const viewer::CollisionWorld& world, bool terrainSupport = false) {
    for (const auto& contact : world.contacts(feet, .3f, 1.8f)) {
        // Zombie feet follow the rendered terrain rather than a rounded-foot
        // slope offset. Only their classified supporting contacts are allowed.
        if (terrainSupport && contact.terrainSupport) continue;
        if (contact.depth >= .0003f)
            std::cerr << "Penetration feet=" << feet.x << ',' << feet.y << ',' << feet.z
                      << " depth=" << contact.depth << " normal=" << contact.normal.x << ','
                      << contact.normal.y << ',' << contact.normal.z << " terrain=" << contact.terrain << '\n';
        require(contact.depth < .0003f, "Step acceptance must preserve whole-body scene clearance");
    }
}
viewer::FPSController playerAt(const viewer::CollisionWorld& world) {
    viewer::FPSController player;
    require(player.spawn({-1, 1.7f, 0}, world), "Fixture player must spawn on the lower ground");
    return player;
}
void playerWalk(viewer::FPSController& player, const viewer::CollisionWorld& world,
                float seconds, float direction = 1, bool sprint = false, int rate = 120) {
    for (int i = 0; i < int(std::lround(seconds * rate)); ++i)
        player.update(1.f / rate, {direction, 0, 0, sprint}, world);
}

void heightsAndSurfaceKinds() {
    for (int kind = 0; kind < 3; ++kind) {
        for (float rise : {.02f, .087f, .25f, .30f, .31f, .45f}) {
            const viewer::CollisionWorld world(ledge(rise, kind));
            const bool allowed = rise <= .30f;
            auto player = playerAt(world);
            playerWalk(player, world, 1);
            auto zombie = viewer::moveZombieBody({-1, 0, 0}, {2, 0, 0}, world);
            if (allowed) {
                if (player.feet().x < 1.9f || zombie.feet.x < 1.9f)
                    std::cerr << "Failed step kind=" << kind << " rise=" << rise << " player="
                              << player.feet().x << ',' << player.feet().y << " zombie="
                              << zombie.feet.x << ',' << zombie.feet.y << '\n';
                require(player.feet().x > 1.9f && zombie.feet.x > 1.9f,
                        "Both player and zombie must traverse rises up to 30 cm");
                require(std::abs(player.feet().y - rise) < .006f && std::abs(zombie.feet.y - rise) < .006f,
                        "Both characters must stand on the raised surface rather than pass underneath");
                require(player.grounded(), "Stepping must keep the player grounded");
                clear(player.feet(), world);
                clear(zombie.feet, world);
                const auto resting = player.feet();
                playerWalk(player, world, .5f, 0);
                require(glm::length(player.feet() - resting) < .001f, "Standing on a raised surface must remain stable");
                playerWalk(player, world, 1, -1);
                zombie = viewer::moveZombieBody(zombie.feet, {-1, 0, 0}, world);
                if (player.feet().x >= -.9f || player.feet().y >= .01f || zombie.feet.x >= -.9f || zombie.feet.y >= .01f)
                    std::cerr << "Failed descent kind=" << kind << " rise=" << rise << " player="
                              << player.feet().x << ',' << player.feet().y << " zombie="
                              << zombie.feet.x << ',' << zombie.feet.y << '\n';
                require(player.feet().x < -.9f && player.feet().y < .01f && zombie.feet.x < -.9f && zombie.feet.y < .01f,
                        "Both characters must also descend from the raised surface");
            } else {
                if (player.feet().x >= 0 || zombie.feet.x >= 0)
                    std::cerr << "Exceeded step kind=" << kind << " rise=" << rise << " player=" << player.feet().x
                              << ',' << player.feet().y << " zombie=" << zombie.feet.x << ',' << zombie.feet.y << '\n';
                require(player.feet().x < 0 && zombie.feet.x < 0,
                        "Steps above 30 cm must remain obstacles for both characters");
                clear(player.feet(), world);
                clear(zombie.feet, world);
            }
        }
    }
}

void roadsOnSlopes() {
    for (const float slope : {-.15f, .07f, .11f, .25f}) {
        for (const float rise : {.25f, .28f, .30f, .31f, .40f}) {
            viewer::Model model;
            quad(model, "terrain", {-10, -10 * slope, -10}, {-10, -10 * slope, 10},
                 {10, 10 * slope, 10}, {10, 10 * slope, -10});
            quad(model, "roads_asphalt", {0, rise, -3}, {0, rise, 3},
                 {5, rise + 5 * slope, 3}, {5, rise + 5 * slope, -3});
            const viewer::CollisionWorld world(model);
            auto player = playerAt(world);
            const auto start = player.feet();
            playerWalk(player, world, 1);
            const auto zombie = viewer::moveZombieBody(start, {2, start.y, 0}, world);
            if (rise <= .30f) {
                if (player.feet().x < 1.9f || zombie.feet.x < 1.9f)
                    std::cerr << "Blocked sloped road: slope=" << slope << " rise=" << rise
                              << " player=" << player.feet().x << " zombie=" << zombie.feet.x << '\n';
                require(player.feet().x > 1.9f && zombie.feet.x > 1.9f && player.grounded(),
                        "A road raised by at most 30 cm must remain walkable on sloped terrain");
            } else {
                if (player.feet().x >= 0 || zombie.feet.x >= 0)
                    std::cerr << "Exceeded sloped road: slope=" << slope << " rise=" << rise
                              << " player=" << player.feet().x << " zombie=" << zombie.feet.x << '\n';
                require(player.feet().x < 0 && zombie.feet.x < 0,
                        "Slope compensation must not allow road ledges higher than 30 cm");
            }
            clear(player.feet(), world);
            clear(zombie.feet, world, true);
        }
    }
}

void headroomAndWalls() {
    for (const auto heights : {glm::vec2(.25f, 1.95f), glm::vec2(.087f, 1.95f)}) {
        auto model = ledge(heights.x, 1);
        quad(model, "ceiling", {-.6f, heights.y, -3}, {5, heights.y, -3}, {5, heights.y, 3}, {-.6f, heights.y, 3});
        const viewer::CollisionWorld world(model);
        auto player = playerAt(world);
        playerWalk(player, world, 1);
        const auto zombie = viewer::moveZombieBody({-1, 0, 0}, {2, 0, 0}, world);
        if (heights.x > .1f)
            require(player.feet().x < 0 && zombie.feet.x < 0, "Insufficient headroom must block a step");
        else require(player.feet().x > 1.9f && zombie.feet.x > 1.9f,
                     "A small step must fit when only the unnecessary full 30 cm lift would hit a ceiling");
        clear(player.feet(), world);
        clear(zombie.feet, world);
        require(!world.walkingGroundAt(1, 0, 1, 2), "Walking support must not reinterpret a ceiling as a foundation");
    }
    auto model = ledge(.2f, 0);
    quad(model, "building_test_walls", {.2f, 0, -3}, {.2f, 0, 3}, {.2f, 3, 3}, {.2f, 3, -3});
    const viewer::CollisionWorld world(model);
    auto player = playerAt(world);
    playerWalk(player, world, 1);
    const auto zombie = viewer::moveZombieBody({-1, 0, 0}, {2, 0, 0}, world);
    require(player.feet().x < -.09f && zombie.feet.x < -.09f,
            "Stepping must not bypass a thin wall directly behind the ledge");
    require(zombie.feet.x <= -.149f,
            "Stepping must also preserve the zombie's additional 5 cm wall clearance");
    clear(player.feet(), world);
    clear(zombie.feet, world);
}

void ratesCrowdsAndQueryContracts() {
    const viewer::CollisionWorld world(ledge(.25f, 0));
    for (const int rate : {30, 60, 144}) {
        auto player = playerAt(world);
        playerWalk(player, world, .5f, 1, true, rate);
        require(player.feet().x > 1.9f && player.grounded(), "Running step-up must work across render rates");
    }
    const viewer::CollisionWorld caps(ledge(.087f, 1));
    require(std::abs(caps.groundAt(1, 0, -1, 1)->height) < .001f &&
            std::abs(caps.groundAt(1, 0, -1, 1, true)->height) < .001f,
            "Existing ordinary/terrain placement queries must retain their cap exclusion");
    require(std::abs(caps.walkingGroundAt(1, 0, -1, 1)->height - .087f) < .001f,
            "Only walking support should include the exposed foundation's upper side");
    viewer::ZombieCrowd crowd;
    crowd.reset(3);
    glm::vec3 feet[3]{{-.4f, 0, 0}, {-1.1f, 0, 0}, {-1.8f, 0, 0}};
    for (std::size_t id = 0; id < 3; ++id) crowd.update(id, feet[id]);
    for (int tick = 0; tick < 90; ++tick) {
        crowd.beginStep();
        for (std::size_t id = 0; id < 3; ++id) {
            feet[id] = viewer::moveZombieBody(feet[id], feet[id] + glm::vec3(.05f, 0, 0), caps, &crowd, id).feet;
            crowd.update(id, feet[id]);
            clear(feet[id], caps);
        }
        require(crowd.stats().neighborChecks <= 3 * viewer::ZombieCrowd::neighborBudget,
                "Stepping must retain the bounded crowd budget");
        for (int i = 0; i < 3; ++i) for (int j = i + 1; j < 3; ++j)
            require(glm::length(glm::vec2(feet[i].x - feet[j].x, feet[i].z - feet[j].z)) >= .5998f,
                    "Crowded step-up must preserve torso separation");
    }
    require(feet[2].x > 1, "A crowd must be able to follow onto an exposed foundation");
}

void mapPatch(const std::filesystem::path& path, glm::vec2 point) {
    const auto model = viewer::loadModel(path);
    const viewer::CollisionWorld world(model);
    const float low = std::numeric_limits<float>::lowest(), high = std::numeric_limits<float>::max();
    const auto ground = world.groundAt(point.x, point.y, low, high, true);
    require(ground.has_value(), "Reported location must have rendered terrain");
    const auto cap = world.walkingGroundAt(point.x, point.y, ground->height, ground->height + .30f);
    require(cap && cap->height > ground->height + .01f, "Location must contain a reachable elevated surface");
    for (float reach : {1.f, 2.f, 3.f, 4.f}) {
        for (const auto direction : {glm::vec2(1, 0), glm::vec2(-1, 0), glm::vec2(0, 1), glm::vec2(0, -1)}) {
            const auto xy = point + reach * direction;
            const auto floor = world.groundAt(xy.x, xy.y, low, high, true);
            if (!floor || floor->normal.y < .7071f) continue;
            glm::vec3 start(xy.x, floor->height + .3f * (1 / floor->normal.y - 1) + .002f / floor->normal.y, xy.y);
            if (!world.contacts(start, .3f, 1.8f).empty()) continue;
            viewer::FPSController player;
            player.teleport(start);
            player.update(.05f, {}, world);
            if (!player.grounded()) continue;
            const float yaw = glm::degrees(std::atan2(-direction.y, -direction.x));
            for (int tick = 0; tick < int(std::ceil(reach / 3 * 120)); ++tick)
                player.update(1.f / 120, {1, 0, yaw}, world);
            const auto zombie = viewer::moveZombieBody(start, {point.x, start.y, point.y}, world);
            if (glm::length(glm::vec2(player.feet().x, player.feet().z) - point) > .1f ||
                glm::length(glm::vec2(zombie.feet.x, zombie.feet.z) - point) > .1f) continue;
            require(player.feet().y >= cap->height - .002f && zombie.feet.y >= cap->height - .002f,
                    "Both characters must stand above the actual reported foundation cap");
            for (const auto feet : {player.feet(), zombie.feet})
                for (const auto& contact : world.contacts(feet, .3f, 1.8f))
                    require(contact.terrain || contact.depth < .001f, "Actual-map traversal must clear the building cap");
            std::cout << "Actual map patch: terrain=" << ground->height << ", support=" << cap->height
                      << ", player feet=" << player.feet().y << ", zombie feet=" << zombie.feet.y
                      << ", approach=" << reach << " m\n";
            return;
        }
    }
    throw std::runtime_error("Neither tested approach traversed the reported patch with both characters");
}
}

int main(int argc, char** argv) {
    try {
        if (argc == 4) {
            mapPatch(argv[1], {std::stof(argv[2]), std::stof(argv[3])});
            return 0;
        }
        require(argc == 1, "Usage: test_character_step [map.glb X Z]");
        heightsAndSurfaceKinds();
        roadsOnSlopes();
        headroomAndWalls();
        ratesCrowdsAndQueryContracts();
        std::cout << "Shared player/zombie steps, foundations, roads, limits, headroom and crowd checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Character step test failed: " << e.what() << '\n';
        return 1;
    }
}
