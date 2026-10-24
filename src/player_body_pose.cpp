#include "player_body_pose.hpp"
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace viewer {
namespace {
glm::vec3 dimensions(const glm::mat4& m) {
    return {glm::length(glm::vec3(m[0])), glm::length(glm::vec3(m[1])), glm::length(glm::vec3(m[2]))};
}
glm::quat orientation(const glm::mat4& m, glm::vec3 size) {
    return glm::normalize(glm::quat_cast(glm::mat3(glm::vec3(m[0])/size.x, glm::vec3(m[1])/size.y, glm::vec3(m[2])/size.z)));
}
}
bool validPlayerBodyPose(const PlayerBodyPose& pose) {
    if (!pose.enabled) return pose.striking == 0 && pose.returning == 0;
    if (pose.striking > 3 || pose.returning > 3 || (pose.striking & pose.returning)) return false;
    for (int axis=0; axis<3; ++axis) if (!std::isfinite(pose.eye[axis])) return false;
    for (const auto& m : pose.parts) {
        for (int c=0; c<4; ++c) for (int r=0; r<4; ++r) if (!std::isfinite(m[c][r])) return false;
        if (std::abs(m[0].w)>1e-5f || std::abs(m[1].w)>1e-5f || std::abs(m[2].w)>1e-5f || std::abs(m[3].w-1)>1e-5f ||
            glm::distance(glm::vec3(m[3]),pose.eye)>3) return false;
        const auto size = dimensions(m);
        if (glm::any(glm::lessThan(size,glm::vec3(.02f))) || glm::any(glm::greaterThan(size,glm::vec3(3)))) return false;
        const glm::mat3 rotation{glm::vec3(m[0])/size.x,glm::vec3(m[1])/size.y,glm::vec3(m[2])/size.z};
        if (std::abs(glm::determinant(rotation)-1)>.001f || std::abs(glm::dot(rotation[0],rotation[1]))>.001f ||
            std::abs(glm::dot(rotation[0],rotation[2]))>.001f || std::abs(glm::dot(rotation[1],rotation[2]))>.001f) return false;
    }
    return true;
}
PlayerBodyPose interpolatePlayerBodyPose(const PlayerBodyPose& a, const PlayerBodyPose& b, float t) {
    if (!a.enabled || !b.enabled || glm::distance(a.eye,b.eye)>2) return b;
    auto pose = b;
    pose.eye = glm::mix(a.eye,b.eye,t);
    for (std::size_t p=0; p<pose.parts.size(); ++p) {
        const auto before = dimensions(a.parts[p]), after = dimensions(b.parts[p]);
        pose.parts[p] = glm::translate(glm::mat4(1),glm::mix(glm::vec3(a.parts[p][3]),glm::vec3(b.parts[p][3]),t)) *
            glm::mat4_cast(glm::slerp(orientation(a.parts[p],before),orientation(b.parts[p],after),t)) *
            glm::scale(glm::mat4(1),glm::mix(before,after,t));
    }
    return pose;
}
} // namespace viewer
