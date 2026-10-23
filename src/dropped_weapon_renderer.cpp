#include "renderer.hpp"
#include <glm/gtc/type_ptr.hpp>

namespace viewer {
void Renderer::setDroppedWeapons(const DroppedWeapons& weapons, const std::array<Model,weaponIds.size()>& models) {
    for (std::size_t i=0; i<models.size(); ++i) {
        auto& gpu = droppedModels_[i];
        if (!gpu.model.meshes.empty()) continue;
        uploadModel(models[i],gpu.model);
        gpu.draws = models[i].draws;
        setupInstanceMatrices(gpu.model,gpu.instances);
    }
    droppedSource_ = &weapons;
}
void Renderer::drawDroppedWeapons(const glm::mat4& projectionView) {
    if (!droppedSource_ || !droppedSource_->count()) return;
    const Frustum frustum(projectionView);
    std::array<std::vector<glm::mat4>,weaponIds.size()> visible;
    for (const auto& pose : droppedSource_->poses())
        if (frustum.intersects(pose.minimum,pose.maximum)) visible[pose.slot].push_back(pose.transform);
    glUniform1i(instancedLocation_,GL_TRUE);
    glUniform1i(animatedLocation_,GL_FALSE);
    glUniform1i(treeCanopyLocation_,GL_FALSE);
    for (std::size_t i=0; i<visible.size(); ++i) {
        if (visible[i].empty()) continue;
        const auto& gpu = droppedModels_[i];
        glBindBuffer(GL_ARRAY_BUFFER,gpu.instances);
        glBufferData(GL_ARRAY_BUFFER,GLsizeiptr(visible[i].size()*sizeof(glm::mat4)),visible[i].data(),GL_STREAM_DRAW);
        for (const auto& draw : gpu.draws) {
            const auto& mesh = gpu.model.meshes.at(draw.primitive);
            applyMaterial(gpu.model,mesh.material,glm::determinant(glm::mat3(draw.transform))<0);
            glUniformMatrix4fv(modelLocation_,1,GL_FALSE,glm::value_ptr(draw.transform));
            glBindVertexArray(mesh.vao);
            glDrawElementsInstanced(GL_TRIANGLES,mesh.count,GL_UNSIGNED_INT,nullptr,GLsizei(visible[i].size()));
        }
    }
    glUniform1i(instancedLocation_,GL_FALSE);
    glBindBuffer(GL_ARRAY_BUFFER,0);
    glFrontFace(GL_CCW);
}
void Renderer::releaseDroppedWeapons() noexcept {
    for (auto& gpu : droppedModels_) {
        releaseModel(gpu.model);
        glDeleteBuffers(1,&gpu.instances);
    }
}
} // namespace viewer
