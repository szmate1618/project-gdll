#include "model.hpp"
#include "weapons.hpp"
#include <limits>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
} // namespace

int main() {
    try {
        const std::array<const char*, 4> names{"pistol", "submachine_gun", "shotgun", "sniper"};
        const std::array<const char*, 4> bodies{"1911_Body", "UMP_Body", "TacticalPump_Body", "M14_Body"};
        const std::array<float, 4> lengths{.22f, .65f, .95f, 1.16f};
        viewer::Weapons weapons(VIEWER_ITEMS, VIEWER_INVENTORY);
        for (std::size_t slot = 0; slot < names.size(); ++slot) {
            const auto model = viewer::loadModel(std::filesystem::path(VIEWER_WEAPON_ASSETS) /
                                                  (std::string(names[slot]) + ".glb"));
            require(model.warnings.empty(), "Weapon asset must load without warnings");
            require(model.images.empty(), "Weapons must not require external textures");
            const auto size = model.boundsMax - model.boundsMin;
            weapons.equip(slot);
            const auto muzzle = weapons.equipped().config.muzzlePosition;
            glm::vec3 capMin(std::numeric_limits<float>::max()), capMax(std::numeric_limits<float>::lowest());
            for (const auto& draw : model.draws) {
                if (draw.name != bodies[slot]) continue;
                for (const auto& vertex : model.primitives.at(draw.primitive).vertices) {
                    const auto p = glm::vec3(draw.transform * glm::vec4(vertex.position, 1));
                    if (std::abs(p.z - model.boundsMin.z) < 1e-5f) {
                        capMin = glm::min(capMin, p); capMax = glm::max(capMax, p);
                    }
                }
            }
            require(glm::distance(muzzle, (capMin + capMax) * .5f) < 1e-5f,
                    "Configured muzzle must be at the exported barrel's front cap center");
            require(std::abs(size.z - lengths[slot]) < .001f, "Weapon length must be in camera-local meters");
            require(size.z > size.y && size.y > size.x, "Weapon axes must be X width, Y height, Z length");
            require(model.boundsMax.x < .1f && model.boundsMin.x > -.1f &&
                    model.boundsMax.y < .2f && model.boundsMin.y > -.4f,
                    "Source display-grid translations must be removed exactly once");
            bool body = false, scope = false, barrel = false;
            std::size_t triangles = 0;
            for (const auto& draw : model.draws) {
                body |= draw.name == bodies[slot];
                scope |= draw.name == "Sniper_ScopeTube";
                barrel |= draw.name == "Sniper_HeavyBarrel";
                const auto& p = model.primitives.at(draw.primitive);
                triangles += p.indices.size() / 3;
                require(p.material >= 0, "Each weapon part needs its material");
                for (std::size_t i = 0; i < p.indices.size(); i += 3) {
                    const auto& a = p.vertices.at(p.indices[i]);
                    const auto& b = p.vertices.at(p.indices[i+1]);
                    const auto& c = p.vertices.at(p.indices[i+2]);
                    const auto cross = glm::cross(b.position - a.position, c.position - a.position);
                    require(glm::length(cross) > 1e-12f, "No degenerate exported triangles");
                    require(glm::dot(glm::normalize(cross), a.normal) > .99f,
                            "Exported flat normals must match triangle winding");
                }
            }
            require(body && triangles > 100 && triangles < 400, "Expected complete Durango weapon below 400 triangles");
            if (slot == 3) {
                require(scope && barrel, "Sniper must include the M14, scope and extended barrel");
                require(model.boundsMin.z < -.65f, "Sniper muzzle must extend toward camera-local -Z");
            }
            std::cout << names[slot] << ": " << triangles << " triangles\n";
        }
        const auto grenade = viewer::loadModel(std::filesystem::path(VIEWER_WEAPON_ASSETS) / "he_grenade.glb");
        require(glm::distance(grenade.boundsMax - grenade.boundsMin, glm::vec3(.09f, .15f, .09f)) < 1e-6f,
                "Grenade model dimensions must match its octagonal rigid body");
        require(grenade.materials.size() == 5 && grenade.images.empty(), "Grenade must retain its five colored bands without textures");
        require(grenade.materials[1].baseColor.r > .7f && grenade.materials[1].baseColor.g < .05f &&
                grenade.materials[3].baseColor.r > .7f && grenade.materials[0].baseColor.g > .7f,
                "Grey grenade needs red stripes near the top and bottom");
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
    return 0;
}
