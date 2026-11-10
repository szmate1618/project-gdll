#pragma once

#include "box_layer.hpp"
#include "inventory/item_model.hpp"
#include <optional>
#include <string>

namespace viewer {
// A borrowed owner's immutable capture. Geometry is in the body's local metre
// frame; pose locates that same frame in the world. Capture never removes it.
struct PropItemGeometry {
    ItemModelConfig model;
    BoxPose pose;
    std::string name,acoustics;
    std::vector<std::string> tags;
};
std::optional<PropItemGeometry> captureBoxPropItem(const BoxLayer&,const PhysicsProp&,
    std::string name="Object",glm::vec3 color={.48f,.34f,.22f});
// Flatten opaque, texture-free authored draws into the item's local frame.
// Preserves flat normals, vertex/material colors and reflected winding.
std::vector<ItemModelVertex> propItemVisual(const Model&,const glm::mat4& modelToItem);
}
