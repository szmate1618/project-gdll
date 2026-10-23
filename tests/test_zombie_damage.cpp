#include "zombie_layer.hpp"
#include "weapons.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

viewer::ZombieLayer target(float distance = 3) {
    viewer::ZombieLayer layer;
    layer.assets.emplace_back();
    layer.instances.emplace_back();
    auto& zombie = layer.instances[0];
    zombie.transform = zombie.restTransform = zombie.restRoot =
        glm::translate(glm::mat4(1), glm::vec3(0, 0, -distance));
    return layer;
}

void injuryRecordsAndDeath() {
    using viewer::ShotCaliber;
    viewer::ZombieDamage damage;
    require(!damage.injury(42), "An untouched zombie must have no injury record");
    require(damage.hit(42, ShotCaliber::low, false) == 9, "First injury starts at 10 HP and applies the hit");
    require(damage.hit(42, ShotCaliber::high, false) == 7, "High caliber removes two HP from the same record");
    require(damage.hit(42, ShotCaliber::low, true) == 5, "Low-caliber headshots remove two HP");
    require(damage.hit(42, ShotCaliber::high, true) == 1, "High-caliber headshots remove four HP");
    require(damage.hit(7, ShotCaliber::low, false) == 9 && damage.injury(42)->health == 1,
            "Stable IDs own independent injury records");
    require(damage.hit(42, ShotCaliber::high, true) == 0, "Overkill must clamp health to zero");
    damage.remove(42);
    require(!damage.injury(42) && damage.injury(7)->health == 9, "Death cleanup must remove only its own record");

    for (const auto caliber : {ShotCaliber::low, ShotCaliber::high}) {
        for (const bool headshot : {false, true}) {
            auto layer = target();
            layer.behaviors.create(0, 0);
            const float height = headshot ? 1.65f : .9f;
            const int damagePerHit = (caliber == ShotCaliber::low ? 1 : 2) * (headshot ? 2 : 1);
            int health = 10;
            while (health > 0) {
                require(layer.shoot({0, height, 0}, {0, 0, -1}, 0, caliber), "Each living hit must register");
                health = std::max(0, health - damagePerHit);
                if (health > 0) {
                    require(!layer.instances[0].ragdoll && layer.behaviors.contains(0),
                            "Nonlethal hits must preserve living behavior");
                    require(layer.damage().injury(0) && layer.damage().injury(0)->health == health,
                            "Each hit must update the same injury record");
                    layer.rebuildSpatialIndex();
                    require(layer.damage().injury(0)->health == health, "Index rebuild must preserve injuries");
                }
            }
            require(layer.instances[0].physicsRagdoll && !layer.behaviors.contains(0) &&
                    !layer.damage().injury(0), "Only death starts physics, removes behavior and erases the injury");
            require(!layer.shoot({0, height, 0}, {0, 0, -1}) && !layer.damage().injury(0),
                    "Shooting a corpse must not recreate an injury record");
        }
    }
}

void missesAndExplosions() {
    auto layer = target();
    require(!layer.shoot({3, .9f, 0}, {0, 0, -1}) && !layer.damage().injury(0),
            "A miss must not create an injury record");
    require(!layer.shoot({0, .9f, 0}, {0, 0, 0}) && !layer.damage().injury(0),
            "An invalid ray must not create an injury record");
    viewer::Model model;
    viewer::Primitive wall;
    for (const auto p : {glm::vec3(-2, 0, -1), {2, 0, -1}, {2, 3, -1}, {-2, 3, -1}}) {
        viewer::Vertex vertex; vertex.position = p; wall.vertices.push_back(vertex);
    }
    wall.indices = {0, 1, 2, 0, 2, 3};
    model.primitives.push_back(wall); model.draws.push_back({});
    const viewer::CollisionWorld blocked(model), clear(viewer::Model{});
    require(!layer.shootWithOcclusion({0, .9f, 0}, {0, 0, -1}, 0, blocked) && !layer.damage().injury(0),
            "A blocked hit must not create an injury record");
    require(layer.shoot({0, .9f, 0}, {0, 0, -1}), "Injury setup must hit");
    require(!layer.shootWithOcclusion({0, .9f, 0}, {0, 0, -1}, 0, blocked) &&
            layer.damage().injury(0)->health == 9, "Blocked shots must preserve existing health");
    require(layer.explode({0, .9f, -2}, 5, 0, clear) == 1 && !layer.damage().injury(0),
            "Grenade deaths must also erase existing injury records");
}

void configuredPellets() {
    viewer::Weapons weapons(VIEWER_ITEMS, VIEWER_INVENTORY);
    for (const auto slot : {0u, 1u, 2u, 3u}) {
        weapons.equip(slot);
        require(weapons.equipped().config.caliber == (slot == 3 ? viewer::ShotCaliber::high : viewer::ShotCaliber::low),
                "Pistol, SMG and shotgun pellets are low caliber; sniper is high caliber");
    }
    weapons.equip(2);
    const auto& config = weapons.equipped().config;
    const auto directions = viewer::weaponShotDirections(config, {0, 0, -1});
    require(directions.size() == 8 && config.spreadDegrees == 4, "Shotgun retains eight pellets at four degrees");
    auto layer = target(2);
    const viewer::CollisionWorld clear(viewer::Model{});
    for (std::size_t pellet = 0; pellet < directions.size(); ++pellet) {
        require(layer.shootWithOcclusion({0, .9f, 0}, directions[pellet], 0, clear, config.caliber),
                "Every close body pellet must independently register on the same zombie");
        require(!layer.instances[0].ragdoll && layer.damage().injury(0)->health == 9 - int(pellet),
                "Each shotgun pellet must remove exactly one HP without deduplicating the target");
    }
    require(layer.damage().injury(0)->health == 2, "Eight body pellets must leave two HP");
    auto head = target(1);
    for (int pellet = 0; pellet < 4; ++pellet) {
        require(head.shoot({0, 1.65f, 0}, directions[pellet], 0, config.caliber), "Close head pellet must hit");
        require(head.damage().injury(0)->health == 8 - 2 * pellet, "Pellet headshots must double one base damage");
    }
    require(head.shoot({0, 1.65f, 0}, directions[4], 0, config.caliber) && head.instances[0].ragdoll &&
            !head.damage().injury(0), "Fifth pellet headshot must kill and erase the record");
}
}

int main() {
    try {
        injuryRecordsAndDeath(); missesAndExplosions(); configuredPellets();
        std::cout << "Zombie damage tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
