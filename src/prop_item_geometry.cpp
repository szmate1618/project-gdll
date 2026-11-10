#include "prop_item_geometry.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace viewer {
namespace {
std::string materialName(std::uint64_t material) {
    return material==propMaterialId("wood") ? "wood" : material==propMaterialId("cardboard") ? "cardboard" : "unknown";
}
bool finite(glm::vec3 value) { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }
}
std::optional<PropItemGeometry> captureBoxPropItem(const BoxLayer& boxes,const PhysicsProp& prop,
        std::string name,glm::vec3 color) {
    const auto index=boxes.indexOf(prop);
    if (!index || !boxes.active(*index)) return {};
    if (!finite(color) || glm::any(glm::lessThan(color,glm::vec3(0))) || glm::any(glm::greaterThan(color,glm::vec3(1))))
        throw std::invalid_argument("Invalid captured prop color");
    PropItemGeometry result;
    result.pose=boxes.pose(*index); result.name=std::move(name); result.acoustics="crate";
    result.model.massKg=boxes.mass(*index); result.tags={"crafting"};
    for (const auto& collider:boxes.colliders(*index)) {
        if (collider.shape!=PropColliderShape::Box) return {};
        ItemModelPart part;
        part.name="part_"+std::to_string(result.model.parts.size());
        part.center=collider.center; part.size=collider.halfExtents*2.f; part.rotation=collider.rotation;
        part.material=materialName(collider.materialId); part.color=color;
        if (part.material!="unknown" && std::find(result.tags.begin(),result.tags.end(),part.material)==result.tags.end())
            result.tags.push_back(part.material);
        result.model.parts.push_back(std::move(part));
    }
    return result;
}
std::vector<ItemModelVertex> propItemVisual(const Model& model,const glm::mat4& modelToItem) {
    std::vector<ItemModelVertex> result;
    for (const auto& draw:model.draws) {
        const auto& primitive=model.primitives.at(draw.primitive);
        if (primitive.indices.size()%3 || primitive.indices.size()>4095-result.size())
            throw std::invalid_argument("Captured prop exceeds the custom triangle limit");
        const Material fallback;
        const auto& material=primitive.material<0 ? fallback : model.materials.at(static_cast<std::size_t>(primitive.material));
        if (material.baseColorTexture>=0 || material.alphaMode!="OPAQUE" || material.baseColor.a!=1)
            throw std::invalid_argument("Captured prop requires opaque texture-free materials");
        const auto transform=modelToItem*draw.transform;
        const auto determinant=glm::determinant(glm::mat3(transform));
        if (!std::isfinite(determinant) || std::abs(determinant)<1e-9f)
            throw std::invalid_argument("Captured prop has a singular visual transform");
        const auto normals=glm::transpose(glm::inverse(glm::mat3(transform)));
        for (std::size_t triangle=0;triangle<primitive.indices.size();triangle+=3) for (int corner=0;corner<3;++corner) {
            const int winding=determinant<0 && corner ? 3-corner : corner;
            const auto& vertex=primitive.vertices.at(primitive.indices[triangle+static_cast<std::size_t>(winding)]);
            const auto normal=normals*vertex.normal;
            const auto position=glm::vec3(transform*glm::vec4(vertex.position,1));
            const auto color=glm::vec3(material.baseColor)*glm::vec3(vertex.color);
            if (!finite(position) || !finite(normal) || glm::length(normal)<1e-6f || !finite(color))
                throw std::invalid_argument("Captured prop has invalid visual vertices");
            result.push_back({position,glm::normalize(normal),color});
        }
    }
    return result;
}
}
