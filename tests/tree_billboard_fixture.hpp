#pragma once
#include "tree_layer.hpp"
#include <array>
#include <glm/gtc/matrix_transform.hpp>

namespace billboard_test {
inline viewer::Model asset() {
    viewer::Model model;
    const std::array<glm::vec4,4> colors{{{1,0,0,1},{0,1,0,1},{0,0,1,1},{1,1,0,1}}};
    const auto card=[&](const std::array<glm::vec3,4>& positions,glm::vec3 normal) {
        const auto index=model.primitives.size();
        viewer::Material material;material.baseColor=colors[index];material.unlit=true;
        material.doubleSided=true;material.alphaMode="MASK";model.materials.push_back(material);
        viewer::Primitive mesh;mesh.material=static_cast<int>(index);
        for(const auto p:positions){viewer::Vertex v;v.position=p;v.normal=normal;mesh.vertices.push_back(v);}
        mesh.indices={0,1,2,0,2,3};model.primitives.push_back(mesh);model.draws.push_back({index,glm::mat4(1),"card"});
    };
    card({{{-1,4,0},{-1,0,0},{1,0,0},{1,4,0}}},{0,0,1});
    card({{{0,4,1},{0,0,1},{0,0,-1},{0,4,-1}}},{1,0,0});
    card({{{-1,2,-1},{-1,2,1},{1,2,1},{1,2,-1}}},{0,1,0});
    card({{{-1,3,-1},{-1,3,1},{1,3,1},{1,3,-1}}},{0,1,0});
    model.boundsMin={-1,0,-1};model.boundsMax={1,4,1};return model;
}
inline viewer::TreeInstance instance(const viewer::Model& model,const glm::mat4& transform) {
    viewer::TreeInstance tree;tree.transform=transform;tree.dimensions={10,20,10};
    tree.boundsMin=glm::vec3(1e20f);tree.boundsMax=glm::vec3(-1e20f);
    for(const auto& draw:model.draws)for(const auto& v:model.primitives[draw.primitive].vertices){
        const glm::vec3 p(transform*draw.transform*glm::vec4(v.position,1));
        tree.boundsMin=glm::min(tree.boundsMin,p);tree.boundsMax=glm::max(tree.boundsMax,p);
    }
    return tree;
}
}
