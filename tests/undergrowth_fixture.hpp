#pragma once
#include "undergrowth.hpp"
#include <glm/gtc/matrix_transform.hpp>

namespace undergrowth_fixture {
inline viewer::TreeLayer trees(bool dry=false,int count=1) {
    viewer::TreeLayer trees;
    viewer::TreeAsset asset; asset.model.boundsMin={-2,0,-2}; asset.model.boundsMax={2,1,2};
    trees.assets.push_back(asset);
    for (int i=0;i<count;++i) {
        viewer::TreeInstance shrub; shrub.id="bush-"+std::to_string(i);
        shrub.undergrowth=dry?viewer::UndergrowthKind::dry:viewer::UndergrowthKind::leafy;
        shrub.collidable=false; shrub.transform=glm::translate(glm::mat4(1),{i*10.f,0,0});
        trees.instances.push_back(shrub);
    }
    return trees;
}
inline viewer::UndergrowthMotion motion(glm::vec3 feet,bool running=false) { return {feet,1.8f,.3f,true,running}; }
}
