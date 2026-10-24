#include "vehicle_traction.hpp"
#include <box3d/box3d.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

namespace viewer {
namespace {
glm::vec3 vector(b3Vec3 v) { return {v.x, v.y, v.z}; }
}
WheelTraction sampleWheelTraction(b3BodyId wheel, const WheelTraction& previous) {
    WheelTraction result;
    const auto center = b3Body_GetWorldCenter(wheel);
    const auto axle = vector(b3Body_GetWorldVector(wheel, {0, 1, 0}));
    const auto centerVelocity = vector(b3Body_GetLinearVelocity(wheel));
    std::vector<b3ContactData> contacts(b3Body_GetContactCapacity(wheel));
    const int count = b3Body_GetContactData(wheel, contacts.data(), static_cast<int>(contacts.size()));
    float totalWeight = 0;
    for (int i = 0; i < count; ++i) {
        const auto& c = contacts[i];
        const bool isB = B3_ID_EQUALS(b3Shape_GetBody(c.shapeIdB), wheel);
        const auto support = b3Shape_GetBody(isB ? c.shapeIdA : c.shapeIdB);
        for (int j = 0; j < c.manifoldCount; ++j) {
            const auto& m = c.manifolds[j];
            const auto normal = vector(m.normal) * (isB ? 1.f : -1.f);
            if (normal.y < .45f) continue; // Side walls cannot provide drive traction.
            const auto direction = glm::cross(normal, axle);
            const float directionLength = glm::length(direction);
            if (directionLength < .001f) continue; // Tire lying on its side.
            const auto forward = direction / directionLength;
            const auto lateral = glm::cross(forward, normal);
            for (int k = 0; k < m.pointCount; ++k) {
                const auto& p = m.points[k];
                if (p.separation >= .015f) continue;
                const auto anchor = isB ? p.anchorB : p.anchorA;
                const b3Pos point{center.x + anchor.x, center.y + anchor.y, center.z + anchor.z};
                const auto groundVelocity = vector(b3Body_GetWorldPointVelocity(support, point));
                const auto slip = vector(b3Body_GetWorldPointVelocity(wheel, point)) - groundVelocity;
                const float weight = std::max(p.totalNormalImpulse, .01f);
                result.forwardSpeed += weight * glm::dot(centerVelocity - groundVelocity, forward);
                result.longitudinalSlip += weight * glm::dot(slip, forward);
                result.lateralSlip += weight * glm::dot(slip, lateral);
                totalWeight += weight;
            }
        }
    }
    if (totalWeight == 0) return result;
    result.forwardSpeed /= totalWeight;
    result.longitudinalSlip /= totalWeight;
    result.lateralSlip /= totalWeight;
    // Longitudinal and lateral slip have separate hysteresis. In particular,
    // sideways sliding must not switch off static grip along a rolling tire.
    const auto threshold = [&](bool sliding) {
        return sliding ? std::max(.3f, std::abs(result.forwardSpeed) * .08f) :
                         std::max(.8f, std::abs(result.forwardSpeed) * .18f);
    };
    result.longitudinalSliding = std::abs(result.longitudinalSlip) > threshold(previous.longitudinalSliding);
    result.lateralSliding = std::abs(result.lateralSlip) > threshold(previous.lateralSliding);
    result.motion = result.longitudinalSliding || result.lateralSliding ? WheelMotion::Sliding : WheelMotion::Rolling;
    result.friction = result.longitudinalSliding ? slidingTireFriction : rollingTireFriction;
    return result;
}
WheelTraction sampleWheelTraction(b3BodyId wheel, WheelMotion previous) {
    WheelTraction history;
    history.longitudinalSliding = history.lateralSliding = previous == WheelMotion::Sliding;
    return sampleWheelTraction(wheel, history);
}
} // namespace viewer
