#include "renderer.hpp"

#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <glm/gtc/type_ptr.hpp>

namespace viewer {
namespace {
static_assert(sizeof(AnimationVertex) == 2 * sizeof(glm::vec4),
              "Animation texture requires packed position/normal vec4 pairs");
}

void Renderer::uploadZombies(const ZombieLayer& zombies) {
    if (zombies.instances.empty()) return;
    if (animatedLocation_ < 0 || animationLocation_ < 0 || instancedLocation_ < 0 ||
        animationFrameLocation_ < 0 || animationCountLocation_ < 0 || animationVerticesLocation_ < 0)
        throw std::runtime_error("Zombie rendering requires animation support in basic.vert");
    if (zombies.instances.size() > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max()))
        throw std::runtime_error("Too many zombie instances for an OpenGL draw");
    GLint maximumTexels = 0;
    glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE, &maximumTexels);
    std::vector<std::vector<ZombieGPUInstance>> instances(zombies.assets.size());
    std::vector<std::vector<std::size_t>> sourceIndices(zombies.assets.size());
    for (std::size_t sourceIndex = 0; sourceIndex < zombies.instances.size(); ++sourceIndex) {
        const auto& instance = zombies.instances[sourceIndex];
        if (instance.asset >= instances.size()) throw std::runtime_error("Invalid zombie asset index");
        instances[instance.asset].push_back({instance.transform, instance.phase, instance.ragdoll ? 0.0f : -1.0f});
        sourceIndices[instance.asset].push_back(sourceIndex);
    }
    // Allocate ownership before creating any GPU resource, so partial failures
    // are covered by the renderer constructor's cleanup path.
    zombieBatches_.resize(zombies.assets.size());
    for (std::size_t i = 0; i < zombies.assets.size(); ++i) {
        const auto& source = zombies.assets[i];
        auto& batch = zombieBatches_[i];
        batch.sourceIndices = std::move(sourceIndices[i]);
        batch.staging = instances[i];
        if (source.frameCount < 2 || source.frameCount > static_cast<std::size_t>(std::numeric_limits<GLint>::max()) ||
            !std::isfinite(source.duration) || source.duration <= 0 ||
            source.animation.size() != source.model.primitives.size())
            throw std::runtime_error("Invalid baked zombie animation");
        batch.frameCount = static_cast<GLint>(source.frameCount);
        batch.duration = source.duration;
        batch.count = static_cast<GLsizei>(instances[i].size());
        uploadModel(source.model, batch.model);
        glGenBuffers(1, &batch.instanceBuffer);
        glBindBuffer(GL_ARRAY_BUFFER, batch.instanceBuffer);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(instances[i].size() * sizeof(ZombieGPUInstance)),
                     batch.staging.data(), GL_STATIC_DRAW);
        batch.animationBuffers.resize(batch.model.meshes.size());
        batch.animationTextures.resize(batch.model.meshes.size());
        batch.vertexCounts.resize(batch.model.meshes.size());
        for (std::size_t p = 0; p < batch.model.meshes.size(); ++p) {
            const auto& mesh = batch.model.meshes[p];
            const auto& mat = material(batch.model, mesh.material);
            if (mat.alphaMode == "BLEND")
                throw std::runtime_error("Instanced zombies require opaque or alpha-mask materials");
            const auto count = source.model.primitives[p].vertices.size();
            const auto& frames = source.animation[p].frames;
            if (count == 0 || count > static_cast<std::size_t>(std::numeric_limits<GLint>::max()) ||
                frames.size() / source.frameCount != count || frames.size() % source.frameCount != 0 ||
                frames.size() > static_cast<std::size_t>(maximumTexels) / 2)
                throw std::runtime_error("Zombie animation exceeds this GPU's texture buffer capacity or has invalid samples");
            batch.vertexCounts[p] = static_cast<GLint>(count);
            glBindVertexArray(mesh.vao);
            for (GLuint column = 0; column < 4; ++column) {
                glEnableVertexAttribArray(4 + column);
                glVertexAttribPointer(4 + column, 4, GL_FLOAT, GL_FALSE, sizeof(ZombieGPUInstance),
                                      reinterpret_cast<const void*>(offsetof(ZombieGPUInstance, transform) + column * sizeof(glm::vec4)));
                glVertexAttribDivisor(4 + column, 1);
            }
            glEnableVertexAttribArray(8);
            glVertexAttribPointer(8, 1, GL_FLOAT, GL_FALSE, sizeof(ZombieGPUInstance),
                                  reinterpret_cast<const void*>(offsetof(ZombieGPUInstance, phase)));
            glVertexAttribDivisor(8, 1);
            glEnableVertexAttribArray(9);
            glVertexAttribPointer(9, 1, GL_FLOAT, GL_FALSE, sizeof(ZombieGPUInstance),
                                  reinterpret_cast<const void*>(offsetof(ZombieGPUInstance, animationFrame)));
            glVertexAttribDivisor(9, 1);
            glGenBuffers(1, &batch.animationBuffers[p]);
            glBindBuffer(GL_TEXTURE_BUFFER, batch.animationBuffers[p]);
            glBufferData(GL_TEXTURE_BUFFER, static_cast<GLsizeiptr>(frames.size() * sizeof(AnimationVertex)),
                         frames.data(), GL_STATIC_DRAW);
            glGenTextures(1, &batch.animationTextures[p]);
            glBindTexture(GL_TEXTURE_BUFFER, batch.animationTextures[p]);
            glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, batch.animationBuffers[p]);
        }
    }
    glBindTexture(GL_TEXTURE_BUFFER, 0);
    glBindBuffer(GL_TEXTURE_BUFFER, 0);
    zombieCount_ = zombies.instances.size();
    checkErrors("zombie upload");
}

void Renderer::updateZombies(const ZombieLayer& zombies) {
    if (zombieBatches_.size() != zombies.assets.size())
        throw std::runtime_error("Zombie layer changed after renderer upload");
    for (std::size_t i = 0; i < zombieBatches_.size(); ++i) {
        auto& batch = zombieBatches_[i];
        if (batch.count != static_cast<GLsizei>(batch.sourceIndices.size()))
            throw std::runtime_error("Zombie population changed after renderer upload");
        for (std::size_t instanceIndex = 0; instanceIndex < batch.sourceIndices.size(); ++instanceIndex) {
            const auto sourceIndex = batch.sourceIndices[instanceIndex];
            if (sourceIndex >= zombies.instances.size()) throw std::runtime_error("Invalid zombie source index");
            const auto& source = zombies.instances[sourceIndex];
            batch.staging[instanceIndex] = {source.transform, source.phase, source.ragdoll ? 0.0f : -1.0f};
        }
        if (!batch.staging.empty()) {
            glBindBuffer(GL_ARRAY_BUFFER, batch.instanceBuffer);
            glBufferSubData(GL_ARRAY_BUFFER, 0,
                            static_cast<GLsizeiptr>(batch.staging.size() * sizeof(ZombieGPUInstance)),
                            batch.staging.data());
        }
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    checkErrors("zombie update");
}

void Renderer::drawZombies(double seconds) {
    zombieDrawCalls_ = 0;
    if (zombieBatches_.empty()) return;
    if (!std::isfinite(seconds) || seconds < 0) seconds = 0;
    glUniform1i(instancedLocation_, GL_TRUE);
    glUniform1i(animatedLocation_, GL_TRUE);
    glUniform1i(animationLocation_, 1);
    const glm::mat4 identity(1);
    glUniformMatrix4fv(modelLocation_, 1, GL_FALSE, glm::value_ptr(identity));
    for (const auto& batch : zombieBatches_) {
        if (batch.count == 0) continue;
        // Reduce in double precision before converting to float to preserve
        // smooth playback even after the application has run for many hours.
        const float frame = static_cast<float>(std::fmod(seconds, batch.duration) / batch.duration * batch.frameCount);
        glUniform1f(animationFrameLocation_, frame);
        glUniform1i(animationCountLocation_, batch.frameCount);
        for (std::size_t p = 0; p < batch.model.meshes.size(); ++p) {
            const auto& mesh = batch.model.meshes[p];
            applyMaterial(batch.model, mesh.material, false);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_BUFFER, batch.animationTextures[p]);
            glActiveTexture(GL_TEXTURE0);
            glUniform1i(animationVerticesLocation_, batch.vertexCounts[p]);
            glBindVertexArray(mesh.vao);
            glDrawElementsInstanced(GL_TRIANGLES, mesh.count, GL_UNSIGNED_INT, nullptr, batch.count);
            ++zombieDrawCalls_;
        }
    }
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_BUFFER, 0);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(animatedLocation_, GL_FALSE);
    glUniform1i(instancedLocation_, GL_FALSE);
    glFrontFace(GL_CCW);
}

void Renderer::releaseZombies() noexcept {
    for (auto& batch : zombieBatches_) {
        releaseModel(batch.model);
        glDeleteBuffers(1, &batch.instanceBuffer);
        for (const auto buffer : batch.animationBuffers) glDeleteBuffers(1, &buffer);
        for (const auto texture : batch.animationTextures) glDeleteTextures(1, &texture);
    }
}

} // namespace viewer
