#include "weapon_view_pose.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>
#include <glm/gtc/matrix_transform.hpp>

namespace viewer {
namespace {
struct Bounds {
    glm::vec3 minimum{std::numeric_limits<float>::max()};
    glm::vec3 maximum{std::numeric_limits<float>::lowest()};
    void add(glm::vec3 p) { minimum = glm::min(minimum, p); maximum = glm::max(maximum, p); }
    glm::vec3 center() const { return (minimum + maximum) * .5f; }
};

bool endsWith(const std::string& name, const std::string& suffix) {
    return name.size() >= suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
}
} // namespace

glm::mat4 weaponViewPose(const Model& model, bool handgun, WeaponViewBody body) {
    if (!std::isfinite(body.height) || !std::isfinite(body.eyeHeight) ||
        body.height <= 0 || body.eyeHeight <= 0 || body.eyeHeight > body.height)
        throw std::invalid_argument("Weapon pose needs valid standing body and eye heights");

    Bounds receiver, trigger;
    std::vector<glm::vec3> receiverVertices;
    bool hasTrigger = false;
    for (const auto& draw : model.draws) {
        const bool isBody = endsWith(draw.name, "_Body"), isTrigger = endsWith(draw.name, "_Trigger");
        if (!isBody && !isTrigger) continue;
        for (const auto& vertex : model.primitives.at(draw.primitive).vertices) {
            const auto point = glm::vec3(draw.transform * glm::vec4(vertex.position, 1));
            if (isBody) { receiver.add(point); receiverVertices.push_back(point); }
            if (isTrigger) { trigger.add(point); hasTrigger = true; }
        }
    }
    if (receiverVertices.empty() || !hasTrigger)
        throw std::runtime_error("Weapon pose needs named body and trigger meshes");

    Bounds butt, muzzle;
    for (const auto point : receiverVertices) {
        // The source stocks end at +Z; average the butt plate's bounds, not
        // the full model center (which changes with barrel/scope length).
        if (point.z >= receiver.maximum.z - .01f) butt.add(point);
        if (point.z <= receiver.minimum.z + .005f) muzzle.add(point);
    }

    // Adult-proportion estimates for the standing player: 46 cm shoulders and
    // 79 cm shoulder-to-hand
    // reach at 1.80 m. Eye-to-shoulder drop follows the configured eye inset.
    const float shoulderWidth = .255f * body.height;
    const float armReach = .44f * body.height;
    const float shoulderY = .82f * body.height - body.eyeHeight;
    // A 25-degree bladed stance puts the support shoulder about 19 cm forward
    // of the firing shoulder. This lets the left hand reach the shotgun pump
    // without pulling the butt away or requiring an overextended arm.
    const float projectedShoulderWidth = shoulderWidth * std::cos(glm::radians(25.0f));
    const glm::vec3 shoulderPocket(.42f * projectedShoulderWidth, shoulderY, .03f * body.height);
    const auto palm = trigger.center() + glm::vec3(0, -.018f * body.height, .014f * body.height);
    const auto mount = handgun ? palm : butt.center();
    // A two-handed pistol hold draws the hands toward the centerline, with
    // bent elbows. Long guns keep the butt seated in the right shoulder.
    const auto target = handgun ? glm::vec3(.175f * shoulderWidth, shoulderY + .015f * body.height,
                                           -.72f * armReach) : shoulderPocket;

    // Converge gently on the crosshair at 25 m. Strong inward yaw would cross
    // the eye ray directly in front of the player.
    glm::mat4 rotation(1);
    for (int i = 0; i < 3; ++i) {
        const auto muzzleAtEye = target + glm::vec3(rotation * glm::vec4(muzzle.center() - mount, 0));
        const auto direction = glm::normalize(glm::vec3(0, 0, -25) - muzzleAtEye);
        rotation = glm::inverse(glm::lookAt(glm::vec3(0), direction, glm::vec3(0, 1, 0)));
    }
    return glm::translate(glm::mat4(1), target) * rotation * glm::translate(glm::mat4(1), -mount);
}

} // namespace viewer
