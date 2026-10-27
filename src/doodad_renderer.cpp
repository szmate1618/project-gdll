#include "renderer.hpp"
#include "doodad_layer.hpp"

#include <glm/gtc/type_ptr.hpp>
#include <stdexcept>

namespace viewer {
void Renderer::setDoodads(const DoodadLayer& doodads, const std::array<Model,3>& models) {
    if (doodadSource_) throw std::logic_error("Doodad meshes already loaded");
    for (std::size_t i=0; i<models.size(); ++i) {
        auto& batch = doodadModels_[i];
        uploadModel(models[i],batch.model);
        batch.draws = models[i].draws;
        setupInstanceMatrices(batch.model,batch.instances);
    }
    updateDoodads(doodads);
}

void Renderer::updateDoodads(const DoodadLayer& doodads) {
    if (doodadSource_ == &doodads && doodadRevision_ == doodads.revision()) return;
    std::vector<VisibilityBounds> bounds;
    bounds.reserve(doodads.instances().size());
    for (const auto& instance : doodads.instances()) bounds.push_back({instance.minimum,instance.maximum});
    doodadVisibility_ = VisibilityIndex(bounds);
    doodadSource_ = &doodads;
    doodadRevision_ = doodads.revision();
}

void Renderer::drawDoodads(const glm::mat4& projectionView, bool shadowPass) {
    if (!shadowPass) doodadVisibleCount_ = 0;
    if (!doodadSource_) return;
    doodadVisibility_.query(projectionView,visibleDoodads_,forestVisibilityRange());
    if (!shadowPass) doodadVisibleCount_ = visibleDoodads_.size();
    if (visibleDoodads_.empty()) return;
    for (auto& batch : doodadModels_) batch.visible.clear();
    for (const auto index : visibleDoodads_) {
        const auto& instance = doodadSource_->instances()[index];
        doodadModels_[static_cast<std::size_t>(instance.kind)].visible.push_back(instance.transform);
    }
    glUniform1i(instancedLocation_,GL_TRUE);
    glUniform1i(animatedLocation_,GL_FALSE);
    glUniform1i(treeCanopyLocation_,GL_FALSE);
    for (auto& batch : doodadModels_) {
        if (batch.visible.empty()) continue;
        glBindBuffer(GL_ARRAY_BUFFER,batch.instances);
        glBufferData(GL_ARRAY_BUFFER,static_cast<GLsizeiptr>(batch.visible.size()*sizeof(glm::mat4)),
                     batch.visible.data(),GL_STREAM_DRAW);
        for (const auto& draw : batch.draws) {
            const auto& mesh = batch.model.meshes.at(draw.primitive);
            applyMaterial(batch.model,mesh.material,glm::determinant(glm::mat3(draw.transform))<0);
            glUniformMatrix4fv(modelLocation_,1,GL_FALSE,glm::value_ptr(draw.transform));
            glBindVertexArray(mesh.vao);
            glDrawElementsInstanced(GL_TRIANGLES,mesh.count,GL_UNSIGNED_INT,nullptr,
                                   static_cast<GLsizei>(batch.visible.size()));
        }
    }
    glUniform1i(instancedLocation_,GL_FALSE);
    glBindBuffer(GL_ARRAY_BUFFER,0);
    glBindVertexArray(0);
    glFrontFace(GL_CCW);
}

void Renderer::releaseDoodads() noexcept {
    for (auto& batch : doodadModels_) {
        releaseModel(batch.model);
        glDeleteBuffers(1,&batch.instances);
    }
}
} // namespace viewer
