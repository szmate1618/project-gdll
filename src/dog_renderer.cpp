#include "renderer.hpp"
#include "dog_layer.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <glm/gtc/type_ptr.hpp>

namespace viewer {
std::optional<DogShotTrace> Renderer::traceDogShot(glm::vec3 origin, glm::vec3 direction, const CollisionWorld& world) const {
    return dogSource_ ? dogSource_->traceShot(origin,direction,world,displayedDogSeconds_,&displayedDogs_) : std::nullopt;
}
void Renderer::uploadDogs(const DogLayer& dogs) {
    dogSource_ = &dogs;
    if (dogs.instances.empty()) return;
    GLint maximumTexels = 0;
    glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE,&maximumTexels);
    dogBatches_.resize(dogs.assets.size());
    for (std::size_t asset = 0; asset < dogs.assets.size(); ++asset) {
        auto& batch = dogBatches_[asset];
        batch.resize(1+dogs.lodAssets.at(asset).size());
        for (std::size_t level = 0; level < batch.size(); ++level)
            uploadZombieGeometry(level ? dogs.lodAssets[asset][level-1] : dogs.assets[asset],
                batch[level],dogs.instances.size(),maximumTexels);
    }
    checkErrors("dog upload");
}
void Renderer::prepareDogDraws(const Frustum& cameraFrustum, const Frustum* sun, const Frustum* flashlight,
        const Camera& camera, double seconds) {
    dogVisibleCount_ = 0;
    if (!dogSource_) return;
    displayedDogs_ = dogSource_->instances;
    displayedDogSeconds_ = seconds;
    for (auto& batch : dogBatches_) for (auto& level : batch) {
        level.cameraInstances.clear(); level.shadowInstances.clear(); level.flashlightInstances.clear();
    }
    for (std::size_t id = 0; id < dogSource_->instances.size(); ++id) {
        const auto& dog = dogSource_->instances[id];
        auto& displayed = displayedDogs_[id];
        displayed.removed = true;
        if (dog.removed) continue;
        auto& batch = dogBatches_.at(dog.asset);
        const auto& model = dogSource_->assets[dog.asset];
        const auto bounds = dogWorldBounds(dogSource_->assetBounds[dog.asset],dog.transform);
        const auto center = (bounds.minimum+bounds.maximum)*.5f;
        const float distance = glm::length(center-camera.position());
        const float radius = glm::length(bounds.maximum-bounds.minimum)*.5f;
        const auto detail = dogCameraDetail(distance);
        const auto makeInstance = [&](std::size_t level, float hz) {
            const auto& asset = level ? dogSource_->lodAssets[dog.asset][level-1] : model;
            const auto* clip = dogAnimationClip(asset,dog);
            ZombieGPUInstance gpu{};
            gpu.transform = dog.transform; gpu.phase = dog.phase;
            gpu.clipFirstFrame = clip ? float(clip->firstFrame) : 0;
            gpu.clipFrameCount = clip ? float(clip->frameCount) : 0;
            gpu.animationFrame = dogAnimationFrame(asset,dog,seconds,hz);
            gpu.animationStep = hz > 0 ? 1.f : 0.f;
            return gpu;
        };
        if (distance-radius <= dogDrawDistance_ && cameraFrustum.intersects(bounds.minimum,bounds.maximum)) {
            const auto level = std::min(detail.level,batch.size()-1);
            const auto gpu = makeInstance(level,detail.animationHz);
            displayed.removed = false; displayed.displayedAnimationFrame = gpu.animationFrame;
            batch[level].cameraInstances.push_back(gpu);
            ++dogVisibleCount_;
        }
        const auto shadowLevel = std::min(std::size_t(1),batch.size()-1);
        if (sun && sun->intersects(bounds.minimum,bounds.maximum))
            batch[shadowLevel].shadowInstances.push_back(makeInstance(shadowLevel,20));
        if (flashlight && flashlight->intersects(bounds.minimum,bounds.maximum))
            batch[shadowLevel].flashlightInstances.push_back(makeInstance(shadowLevel,20));
    }
}
void Renderer::drawDogs(ZombiePass pass) {
    if (!dogSource_ || dogBatches_.empty()) return;
    // The camera's unwounded program and the shadow program are already lit by
    // the caller. Dogs have independent geometry/animation/instance buffers.
    selectBasicShader(pass != ZombiePass::Camera);
    glUniform1i(instancedLocation_,GL_TRUE); glUniform1i(animatedLocation_,GL_TRUE);
    glUniform1i(animationLocation_,1); glUniform1i(skinningLocation_,GL_FALSE);
    glUniform1i(zombieCullBackfacesLocation_,GL_FALSE);
    const glm::mat4 identity(1);
    glUniformMatrix4fv(modelLocation_,1,GL_FALSE,glm::value_ptr(identity));
    for (auto& batch : dogBatches_) for (auto& geometry : batch) {
        const auto& visible = pass == ZombiePass::Camera ? geometry.cameraInstances :
            pass == ZombiePass::SunShadow ? geometry.shadowInstances : geometry.flashlightInstances;
        if (visible.empty()) continue;
        glBindBuffer(GL_ARRAY_BUFFER,geometry.instanceBuffer);
        glBufferData(GL_ARRAY_BUFFER,static_cast<GLsizeiptr>(dogSource_->instances.size()*sizeof(ZombieGPUInstance)),nullptr,GL_STREAM_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER,0,static_cast<GLsizeiptr>(visible.size()*sizeof(ZombieGPUInstance)),visible.data());
        glUniform1i(animationCountLocation_,geometry.frameCount);
        for (std::size_t p = 0; p < geometry.model.meshes.size(); ++p) {
            const auto& mesh = geometry.model.meshes[p];
            applyMaterial(geometry.model,mesh.material,false);
            glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_BUFFER,geometry.animationTextures[p]);
            glActiveTexture(GL_TEXTURE0);
            glUniform1i(animationVerticesLocation_,geometry.vertexCounts[p]);
            glBindVertexArray(mesh.vao);
            glDrawElementsInstanced(GL_TRIANGLES,mesh.count,GL_UNSIGNED_INT,nullptr,static_cast<GLsizei>(visible.size()));
        }
    }
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_BUFFER,0);
    glActiveTexture(GL_TEXTURE0); glBindBuffer(GL_ARRAY_BUFFER,0);
    glUniform1i(animatedLocation_,GL_FALSE); glUniform1i(instancedLocation_,GL_FALSE);
    glFrontFace(GL_CCW);
}
void Renderer::releaseDogs() noexcept {
    for (auto& batch : dogBatches_) for (auto& geometry : batch) {
        releaseModel(geometry.model);
        glDeleteBuffers(1,&geometry.instanceBuffer);
        for (const auto buffer : geometry.animationBuffers) glDeleteBuffers(1,&buffer);
        for (const auto texture : geometry.animationTextures) glDeleteTextures(1,&texture);
        for (const auto buffer : geometry.skinningBuffers) glDeleteBuffers(1,&buffer);
    }
    dogBatches_.clear(); dogSource_ = nullptr;
}
} // namespace viewer
