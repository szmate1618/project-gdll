#include "crafted_melee_geometry.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <stdexcept>

namespace viewer {
glm::mat4 craftedItemToGrip(const Item& item) {
    if (item.grips().empty()) throw std::invalid_argument("Crafted melee item has no grip");
    const auto& grip=item.grips().front();
    return glm::inverse(glm::translate(glm::mat4(1),grip.position)*glm::mat4_cast(grip.rotation));
}
std::vector<std::array<glm::vec3,8>> craftedMeleeParts(const Item& item) {
    const auto toGrip=craftedItemToGrip(item);
    std::vector<std::array<glm::vec3,8>> result;
    result.reserve(item.inventory().model.parts.size());
    for (const auto& part:item.inventory().model.parts) {
        const auto half=part.size*.5f;
        const auto top=part.shape==ItemModelShape::Frustum ? part.topSize*.5f : glm::vec2(half.x,half.z);
        const auto frame=toGrip*glm::translate(glm::mat4(1),part.center)*glm::mat4_cast(part.rotation);
        std::array<glm::vec3,8> points{{
            {-half.x,-half.y,-half.z},{half.x,-half.y,-half.z},{half.x,-half.y,half.z},{-half.x,-half.y,half.z},
            {-top.x,half.y,-top.y},{top.x,half.y,-top.y},{top.x,half.y,top.y},{-top.x,half.y,top.y}}};
        for (auto& point:points) point=glm::vec3(frame*glm::vec4(point,1));
        result.push_back(points);
    }
    return result;
}
}
