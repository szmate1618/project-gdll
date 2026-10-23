#include "renderer.hpp"
#include "box_layer.hpp"

#include <glm/gtc/type_ptr.hpp>

namespace viewer {
void Renderer::setupInstanceMatrices(ModelGPU& model, GLuint& buffer) {
    glGenBuffers(1,&buffer);
    glBindBuffer(GL_ARRAY_BUFFER,buffer);
    for (const auto& mesh : model.meshes) {
        glBindVertexArray(mesh.vao);
        for (GLuint column=0; column<4; ++column) {
            glEnableVertexAttribArray(4+column);
            glVertexAttribPointer(4+column,4,GL_FLOAT,GL_FALSE,sizeof(glm::mat4),
                                  reinterpret_cast<const void*>(column*sizeof(glm::vec4)));
            glVertexAttribDivisor(4+column,1);
        }
    }
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER,0);
}
void Renderer::updateBoxes(const BoxLayer& boxes) {
    if (boxSource_ == &boxes && boxRevision_ == boxes.revision()) return;
    if (!boxes.count()) {
        boxTransforms_.clear();
        boxVisibility_ = VisibilityIndex();
        boxSource_ = &boxes; boxRevision_ = boxes.revision();
        return;
    }
    if (boxModel_.meshes.empty()) {
        uploadModel(BoxLayer::unitCube(), boxModel_);
        setupInstanceMatrices(boxModel_,boxInstanceBuffer_);
    }
    boxTransforms_.clear();
    std::vector<VisibilityBounds> bounds;
    bounds.reserve(boxes.count());
    for (std::size_t i = 0; i < boxes.count(); ++i) {
        const auto pose = boxes.pose(i);
        const auto transform = pose.transform();
        boxTransforms_.push_back(transform);
        const auto half = (glm::abs(glm::vec3(transform[0])) + glm::abs(glm::vec3(transform[1])) +
                           glm::abs(glm::vec3(transform[2]))) * .5f;
        bounds.push_back({pose.center - half, pose.center + half});
    }
    boxVisibility_ = VisibilityIndex(bounds);
    boxSource_ = &boxes; boxRevision_ = boxes.revision();
}
void Renderer::drawBoxes(const glm::mat4& projectionView) {
    if (boxTransforms_.empty()) return;
    boxVisibility_.query(projectionView, visibleBoxes_, forestVisibilityRange());
    if (visibleBoxes_.empty()) return;
    visibleBoxTransforms_.clear();
    for (const auto i : visibleBoxes_) visibleBoxTransforms_.push_back(boxTransforms_[i]);
    glBindBuffer(GL_ARRAY_BUFFER, boxInstanceBuffer_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(visibleBoxTransforms_.size() * sizeof(glm::mat4)),
                 visibleBoxTransforms_.data(), GL_STREAM_DRAW);
    const auto& mesh = boxModel_.meshes.front();
    applyMaterial(boxModel_, mesh.material, false);
    const glm::mat4 identity(1);
    glUniformMatrix4fv(modelLocation_, 1, GL_FALSE, glm::value_ptr(identity));
    glUniform1i(instancedLocation_, GL_TRUE);
    glUniform1i(animatedLocation_, GL_FALSE);
    glUniform1i(treeCanopyLocation_, GL_FALSE);
    glBindVertexArray(mesh.vao);
    glDrawElementsInstanced(GL_TRIANGLES, mesh.count, GL_UNSIGNED_INT, nullptr, static_cast<GLsizei>(visibleBoxTransforms_.size()));
    glUniform1i(instancedLocation_, GL_FALSE);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glFrontFace(GL_CCW);
}
} // namespace viewer
