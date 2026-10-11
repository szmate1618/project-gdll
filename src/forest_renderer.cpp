#include "renderer.hpp"

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace viewer {
void Renderer::uploadForests(const ForestLayer& forests) {
    forestPatches_=forests.patches;
    forestDetailDistance_=forests.detailDistance;
    uploadModel(forests.volumes,forestModel_);
    for (const auto& source:forests.volumes.draws) {
        const auto& mesh=forestModel_.meshes.at(source.primitive);
        forestDraws_.push_back({source.primitive,source.transform,
            glm::inverseTranspose(glm::mat3(source.transform)),
            glm::vec3(source.transform*glm::vec4(mesh.center,1)),
            glm::determinant(glm::mat3(source.transform))<0});
    }
    treeForestPatches_.assign(treeInstances_.size(),std::numeric_limits<std::size_t>::max());
    std::vector<VisibilityBounds> bounds;
    for (std::size_t i=0;i<forestPatches_.size();++i) {
        const auto& patch=forestPatches_[i];
        if (patch.firstTree>treeInstances_.size()||patch.treeCount>treeInstances_.size()-patch.firstTree||
            patch.firstDraw>forestDraws_.size()||patch.drawCount>forestDraws_.size()-patch.firstDraw)
            throw std::runtime_error("Forest rendering ranges exceed the uploaded layer");
        for (std::size_t tree=patch.firstTree;tree<patch.firstTree+patch.treeCount;++tree) {
            if (treeForestPatches_[tree]!=std::numeric_limits<std::size_t>::max())
                throw std::runtime_error("Forest tree ranges overlap");
            treeForestPatches_[tree]=i;
        }
        for (const auto tree : patch.detectedTrees) {
            if (tree >= treeInstances_.size() || treeForestPatches_[tree] != std::numeric_limits<std::size_t>::max())
                throw std::runtime_error("Invalid or overlapping detected forest tree index");
            treeForestPatches_[tree] = i;
        }
        bounds.push_back(patch.bounds);
    }
    forestVisibility_=VisibilityIndex(bounds);
    forestDetail_.resize(forestPatches_.size(),false);
    visibleForests_.reserve(forestPatches_.size());
    forestStats_.patches=forestPatches_.size();
}

void Renderer::prepareForestView(const Camera& camera) {
    forestStats_.volumes=forestStats_.detailedPatches=0;
    const bool scoped=camera.viewMode()!=Camera::ViewMode::original;
    for (std::size_t i=0;i<forestPatches_.size();++i) {
        forestDetail_[i]=forestUsesTrees(forestPatches_[i],camera.position(),scoped,forestDetailDistance_);
        if (forestDetail_[i]) ++forestStats_.detailedPatches;
    }
}

void Renderer::drawForests(const glm::mat4& projectionView,bool shadowPass) {
    if (forestPatches_.empty()) return;
    if (shadowPass||treeCulling_) forestVisibility_.query(projectionView,visibleForests_);
    else {
        visibleForests_.resize(forestPatches_.size());
        std::iota(visibleForests_.begin(),visibleForests_.end(),std::size_t{0});
    }
    glUniform1i(instancedLocation_,GL_FALSE);
    glUniform1i(animatedLocation_,GL_FALSE);
    for (const auto index:visibleForests_) {
        if (forestDetail_[index]) continue;
        const auto& patch=forestPatches_[index];
        for (std::size_t i=patch.firstDraw;i<patch.firstDraw+patch.drawCount;++i) {
            const auto& draw=forestDraws_[i];
            const auto& mesh=forestModel_.meshes[draw.primitive];
            applyMaterial(forestModel_,mesh.material,draw.mirrored);
            glUniformMatrix4fv(modelLocation_,1,GL_FALSE,glm::value_ptr(draw.transform));
            glUniformMatrix3fv(normalLocation_,1,GL_FALSE,glm::value_ptr(draw.normalMatrix));
            glBindVertexArray(mesh.vao);
            glDrawElements(GL_TRIANGLES,mesh.count,GL_UNSIGNED_INT,nullptr);
        }
        if (!shadowPass) ++forestStats_.volumes;
    }
    glFrontFace(GL_CCW);
}
} // namespace viewer
