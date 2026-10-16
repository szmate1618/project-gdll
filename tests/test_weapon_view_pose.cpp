#include "weapon_view_pose.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

glm::vec3 point(const glm::mat4& transform, glm::vec3 p) {
    return glm::vec3(transform * glm::vec4(p, 1));
}
} // namespace

int main() {
    try {
        const std::array<const char*, 4> names{"pistol", "submachine_gun", "sniper", "shotgun"};
        // Measured contact landmarks on the prepared assets, in source meters.
        // These are independent of the body's estimated target positions.
        const std::array<glm::vec3, 4> butts{{{0, -.08869f, .11f}, {-.010056f, -.004321f, .325f},
                                            {0, -.082334f, .5f}, {0, -.111084f, .475f}}};
        const std::array<glm::vec3, 4> triggerCenters{{{-.000682f, -.013693f, .012113f},
            {-.010823f, -.040339f, .012254f}, {-.001254f, -.054477f, .159822f},
            {-.001044f, -.076077f, .157418f}}};
        const glm::vec3 rightShoulder(.23f, -.224f, .054f);
        for (std::size_t slot = 0; slot < names.size(); ++slot) {
            auto model = viewer::loadModel(std::filesystem::path(VIEWER_WEAPON_ASSETS) /
                                           (std::string(names[slot]) + ".glb"));
            const auto transform = viewer::weaponViewPose(model, slot == 0, {});
            for (int axis = 0; axis < 3; ++axis)
                require(std::abs(glm::length(glm::vec3(transform[axis])) - 1) < 1e-5f,
                        "Camera positioning must preserve real weapon dimensions");
            require(std::abs(glm::determinant(glm::mat3(transform)) - 1) < 1e-5f,
                    "Weapon pose must preserve handedness");
            const auto hand = point(transform, triggerCenters[slot] + glm::vec3(0, -.0324f, .0252f));
            const float reach = glm::distance(hand, rightShoulder);
            require(reach > .25f && reach < .79f, "Trigger hand must fit the adult player's arm reach");
            if (slot == 0) {
                require(hand.x > .04f && hand.x < .12f && hand.y > -.25f && hand.y < -.15f &&
                        hand.z > -.65f && hand.z < -.5f, "Two-handed pistol hold must be forward and near the eye centerline");
                require(reach > .55f && reach < .72f, "Pistol hold should retain an elbow bend");
            } else {
                const auto stock = point(transform, butts[slot]);
                require(stock.x > .16f && stock.x < .23f && stock.y > -.26f && stock.y < -.19f &&
                        stock.z > 0 && stock.z < .08f, "Long-gun butt must sit in the right shoulder pocket");
                require(hand.z > -.40f && hand.z < -.20f, "Long-gun trigger must stay within a bent arm's reach");
                if (slot == 3) {
                    // Rear half of the pump is available to the support hand.
                    const auto support = point(transform, {0, -.045f, -.29f});
                    // Left shoulder is forward in the stocked-weapon stance;
                    // measured shoulder span remains approximately 46 cm.
                    const glm::vec3 supportShoulder(-.208f, -.224f, -.140f);
                    require(glm::distance(support, supportShoulder) < .79f,
                            "Shotgun pump must remain reachable by the support arm");
                }
                const auto lowerEye = viewer::weaponViewPose(model, false, {1.8f, 1.6f});
                const auto stockAtLowerEye = point(lowerEye, butts[slot]);
                require(std::abs(stockAtLowerEye.y - stock.y - .10f) < .001f &&
                        std::abs(stockAtLowerEye.z - stock.z) < .001f,
                        "Eye inset must change shoulder offset without changing arm or stock distance");
                // Lengthening the muzzle cannot push the butt or grip forward.
                for (auto& p : model.primitives)
                    for (auto& v : p.vertices) if (v.position.z < -.2f) v.position.z -= .4f;
                const auto longer = viewer::weaponViewPose(model, false, {});
                require(glm::distance(point(longer, butts[slot]), stock) < .001f &&
                        glm::distance(point(longer, triggerCenters[slot]), point(transform, triggerCenters[slot])) < .001f,
                        "Barrel length must not change shoulder or trigger-hand placement");
            }
            const auto forward = glm::vec3(transform * glm::vec4(0, 0, -1, 0));
            require(glm::dot(forward, glm::vec3(0, 0, -1)) > .999f,
                    "Bores should converge gently, not cross directly in front of the eyes");
            std::cout << names[slot] << ": trigger-hand reach " << reach << " m\n";
        }
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
    return 0;
}
