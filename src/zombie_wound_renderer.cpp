#include "renderer.hpp"
#include "box_layer.hpp"
#include "zombie_wound_surface.hpp"

#include <algorithm>
#include <stdexcept>
#include <glm/gtc/type_ptr.hpp>

namespace viewer {
void Renderer::updateZombieWound(std::size_t id, ZombieBatch& batch, ZombieGPUInstance& instance) {
    const auto& source = zombieSource_->instances[id];
    const auto* wound = source.removed ? nullptr : zombieSource_->wounds().find(id);
    auto row = batch.woundRows.find(id);
    instance.woundBase = 0;
    if (!wound) {
        if (row != batch.woundRows.end()) {
            batch.freeWoundRows.push_back(row->second.offset);
            batch.woundRows.erase(row);
        }
        return;
    }
    if (row != batch.woundRows.end() && row->second.revision == wound->revision &&
        row->second.severedJoint == source.severedJoint) {
        instance.woundBase = float(row->second.offset + 1);
        return;
    }
    const auto variant = std::size_t(source.severedJoint+1);
    const auto& asset = source.severedJoint < 0 ? zombieSource_->assets.at(source.asset) :
        zombieSource_->dismembermentAssets.at(source.asset).meshes.at(std::size_t(source.severedJoint));
    const auto& surface = batch.woundSurfaces.at(variant);
    const auto mask = ZombieWounds::surfaceMask(asset,surface,*wound,source.severedJoint < 0);
    if (mask.size() > batch.woundStride || batch.woundStride == 0)
        throw std::runtime_error("Zombie wound mask does not match its geometry");
    ZombieWound displayedWound;
    displayedWound.revision = wound->revision;
    displayedWound.removedTriangles = wound->removedTriangles;
    displayedWound.vertexMask = wound->vertexMask;
    bool grew = false;
    if (row == batch.woundRows.end()) {
        if (batch.freeWoundRows.empty()) {
            const auto oldSize = batch.woundBytes.size();
            const auto slots = std::max(std::size_t(1), oldSize / batch.woundStride);
            const auto available = batch.woundLimit / batch.woundStride;
            const auto newSlots = std::min(slots * 2, available);
            if (newSlots <= oldSize / batch.woundStride)
                throw std::runtime_error("Zombie wound masks exceed GPU texture-buffer capacity");
            batch.woundBytes.resize(newSlots * batch.woundStride);
            for (std::size_t offset = oldSize; offset < batch.woundBytes.size(); offset += batch.woundStride)
                batch.freeWoundRows.push_back(offset);
            grew = true;
        }
        const auto offset = batch.freeWoundRows.back();
        batch.freeWoundRows.pop_back();
        row = batch.woundRows.emplace(id, ZombieBatch::WoundRow{offset, 0, -1, {}}).first;
    }
    const auto offset = row->second.offset;
    std::copy(mask.begin(), mask.end(), batch.woundBytes.begin() + offset);
    if (!batch.woundBuffer) {
        glGenBuffers(1, &batch.woundBuffer);
        glGenTextures(1, &batch.woundTexture);
    }
    glBindBuffer(GL_TEXTURE_BUFFER, batch.woundBuffer);
    if (grew) glBufferData(GL_TEXTURE_BUFFER, GLsizeiptr(batch.woundBytes.size()), batch.woundBytes.data(), GL_DYNAMIC_DRAW);
    else glBufferSubData(GL_TEXTURE_BUFFER, GLintptr(offset), GLsizeiptr(mask.size()), mask.data());
    glBindTexture(GL_TEXTURE_BUFFER, batch.woundTexture);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_R8UI, batch.woundBuffer);
    glBindTexture(GL_TEXTURE_BUFFER, 0);
    glBindBuffer(GL_TEXTURE_BUFFER, 0);
    row->second.revision = wound->revision;
    row->second.severedJoint = source.severedJoint;
    row->second.displayedWound = std::move(displayedWound);
    instance.woundBase = float(offset + 1);
}

void Renderer::drawWoundContents(const glm::mat4& projectionView) {
    if (!zombieSource_) return;
    const auto objects = zombieSource_->woundContents();
    if (objects.empty()) return;
    const Frustum frustum(projectionView);
    std::vector<glm::mat4> visible;
    for (const auto& matrix : objects) {
        const auto center = glm::vec3(matrix[3]);
        const auto extent = (glm::abs(glm::vec3(matrix[0]))+glm::abs(glm::vec3(matrix[1]))+glm::abs(glm::vec3(matrix[2])))*.5f;
        if (frustum.intersects(center-extent,center+extent)) visible.push_back(matrix);
    }
    if (visible.empty()) return;
    if (woundContentsModel_.meshes.empty()) {
        auto model = BoxLayer::unitCube();
        model.materials.front().baseColor = {.5f, .025f, .035f, 1};
        uploadModel(model, woundContentsModel_);
        glGenBuffers(1, &woundContentsBuffer_);
        glBindBuffer(GL_ARRAY_BUFFER, woundContentsBuffer_);
        glBindVertexArray(woundContentsModel_.meshes.front().vao);
        for (GLuint column = 0; column < 4; ++column) {
            glEnableVertexAttribArray(4 + column);
            glVertexAttribPointer(4 + column, 4, GL_FLOAT, GL_FALSE, sizeof(glm::mat4),
                                  reinterpret_cast<const void*>(column * sizeof(glm::vec4)));
            glVertexAttribDivisor(4 + column, 1);
        }
    }
    glBindBuffer(GL_ARRAY_BUFFER, woundContentsBuffer_);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(visible.size() * sizeof(glm::mat4)), visible.data(), GL_STREAM_DRAW);
    const auto& mesh = woundContentsModel_.meshes.front();
    applyMaterial(woundContentsModel_, mesh.material, false);
    const glm::mat4 identity(1);
    glUniformMatrix4fv(modelLocation_, 1, GL_FALSE, glm::value_ptr(identity));
    glUniform1i(instancedLocation_, GL_TRUE);
    glUniform1i(animatedLocation_, GL_FALSE);
    glUniform1i(treeCanopyLocation_, GL_FALSE);
    glBindVertexArray(mesh.vao);
    glDrawElementsInstanced(GL_TRIANGLES, mesh.count, GL_UNSIGNED_INT, nullptr, GLsizei(visible.size()));
    glUniform1i(instancedLocation_, GL_FALSE);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glFrontFace(GL_CCW);
}
void Renderer::releaseWoundContents() noexcept {
    releaseModel(woundContentsModel_);
    glDeleteBuffers(1, &woundContentsBuffer_);
    woundContentsBuffer_ = 0;
}
} // namespace viewer
