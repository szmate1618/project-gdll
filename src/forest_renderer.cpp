#include "renderer.hpp"

#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cstdint>
#include <numeric>

namespace viewer {
void Renderer::uploadForests(const ForestLayer& forests) {
    forestPatches_=forests.patches;
    oldForestVisibility_=OldForestVisibility(forestPatches_);
    forestDetailDistance_=forests.detailDistance;
    auto batched = batchForestVolumes(forests);
    uploadModel(batched.model, forestModel_);
    forestBatches_.resize(batched.ranges.size());
    for (std::size_t i = 0; i < forestBatches_.size(); ++i) {
        auto& batch = forestBatches_[i];
        batch.ranges = std::move(batched.ranges[i]);
        batch.counts.reserve(batch.ranges.size());
        batch.offsets.reserve(batch.ranges.size());
    }
    std::vector<VisibilityBounds> bounds;
    for (const auto& patch : forestPatches_) {
        bounds.push_back(patch.bounds);
    }
    forestVisibility_=VisibilityIndex(bounds);
    forestDetail_.resize(forestPatches_.size(),false);
    forestVolumeVisible_.resize(forestPatches_.size(),false);
    visibleForests_.reserve(forestPatches_.size());
    forestStats_.patches=forestPatches_.size();
}

void Renderer::prepareForestView(const Camera& camera) {
    forestStats_.volumes=forestStats_.detailedPatches=forestStats_.drawCalls=0;
    const bool scoped=camera.viewMode()!=Camera::ViewMode::original;
    for (std::size_t i=0;i<forestPatches_.size();++i) {
        forestDetail_[i]=forestUsesTrees(forestPatches_[i],camera.position(),scoped,forestDetailDistance_);
        if (forestDetail_[i]) ++forestStats_.detailedPatches;
    }
}

void Renderer::drawForests(const glm::mat4& projectionView,bool shadowPass) {
    if (forestPatches_.empty()) return;
    if (shadowPass||treeCulling_||forestViewRange_)
        forestVisibility_.query(projectionView,visibleForests_,forestVisibilityRange(),shadowPass||treeCulling_);
    else {
        visibleForests_.resize(forestPatches_.size());
        std::iota(visibleForests_.begin(),visibleForests_.end(),std::size_t{0});
    }
    glUniform1i(instancedLocation_,GL_FALSE);
    glUniform1i(animatedLocation_,GL_FALSE);
    const glm::mat4 identity(1);
    const glm::mat3 normal(1);
    glUniformMatrix4fv(modelLocation_, 1, GL_FALSE, glm::value_ptr(identity));
    glUniformMatrix3fv(normalLocation_, 1, GL_FALSE, glm::value_ptr(normal));
    std::fill(forestVolumeVisible_.begin(), forestVolumeVisible_.end(), false);
    for (const auto index:visibleForests_) {
        if (forestDetail_[index]) continue;
        forestVolumeVisible_[index] = true;
        if (!shadowPass) ++forestStats_.volumes;
    }
    // Keep vertex/index buffers static. OpenGL 3.3 multi-draw submits all visible
    // ranges with one VAO/material binding per batch, independently per pass.
    for (std::size_t i = 0; i < forestBatches_.size(); ++i) {
        auto& batch = forestBatches_[i];
        batch.counts.clear();
        batch.offsets.clear();
        for (const auto& range : batch.ranges) {
            if (!forestVolumeVisible_[range.patch]) continue;
            batch.counts.push_back(static_cast<GLsizei>(range.indexCount));
            batch.offsets.push_back(reinterpret_cast<const void*>(
                static_cast<std::uintptr_t>(range.firstIndex * sizeof(std::uint32_t))));
        }
        if (batch.counts.empty()) continue;
        const auto& mesh = forestModel_.meshes[i];
        applyMaterial(forestModel_, mesh.material, false);
        glBindVertexArray(mesh.vao);
        glMultiDrawElements(GL_TRIANGLES, batch.counts.data(), GL_UNSIGNED_INT,
                           batch.offsets.data(), static_cast<GLsizei>(batch.counts.size()));
        if (!shadowPass) ++forestStats_.drawCalls;
    }
    glFrontFace(GL_CCW);
}
} // namespace viewer
