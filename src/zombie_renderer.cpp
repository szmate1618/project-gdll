#include "renderer.hpp"
#include "zombie_animation.hpp"
#include "zombie_lod.hpp"

#include <cmath>
#include <cstddef>
#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <glm/gtc/type_ptr.hpp>

namespace viewer {
namespace {
static_assert(sizeof(AnimationVertex) == 2 * sizeof(glm::vec4),
              "Animation texture requires packed position/normal vec4 pairs");

struct SkinVertexGPU {
    std::array<std::uint32_t, 4> joints0{};
    std::array<float, 4> weights0{};
    std::array<std::uint32_t, 4> joints1{};
    std::array<float, 4> weights1{};
};

VisibilityBounds emptyBounds() {
    return {glm::vec3(std::numeric_limits<float>::max()),
            glm::vec3(std::numeric_limits<float>::lowest())};
}

void includeBounds(VisibilityBounds& bounds, const VisibilityBounds& other) {
    bounds.minimum = glm::min(bounds.minimum, other.minimum);
    bounds.maximum = glm::max(bounds.maximum, other.maximum);
}

VisibilityBounds transformedBounds(const VisibilityBounds& bounds, const glm::mat4& transform) {
    const auto center = glm::vec3(transform * glm::vec4((bounds.minimum + bounds.maximum) * 0.5f, 1));
    const auto half = (bounds.maximum - bounds.minimum) * 0.5f;
    const auto extent = glm::abs(glm::vec3(transform[0])) * half.x +
                        glm::abs(glm::vec3(transform[1])) * half.y +
                        glm::abs(glm::vec3(transform[2])) * half.z;
    return {center - extent, center + extent};
}
}

Renderer::ZombieGPUInstance Renderer::zombieGPUInstance(const AnimatedModel& asset, const ZombieInstance& instance,
                                                       float boneBase) {
    const auto* clip = zombieAnimationClip(asset, instance);
    return {instance.transform, instance.phase,
            clip ? static_cast<float>(clip->firstFrame) : 0.0f,
            clip ? static_cast<float>(clip->frameCount) : 0.0f,
            clip ? animatedFrame(clip->duration, clip->frameCount, instance.walkSeconds, instance.phase) :
                   (instance.ragdoll ? 0.0f : -1.0f),
            instance.ragdoll ? 1.0f : 0.0f, 0.0f, boneBase};
}

void Renderer::uploadZombies(const ZombieLayer& zombies) {
    if (zombies.instances.empty()) return;
    if (animatedLocation_ < 0 || animationLocation_ < 0 || instancedLocation_ < 0 ||
        animationFrameLocation_ < 0 || animationCountLocation_ < 0 || animationVerticesLocation_ < 0 ||
        skinningLocation_ < 0 || skinningBonesLocation_ < 0 || skinningBoneCountLocation_ < 0)
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
        instances[instance.asset].push_back(zombieGPUInstance(zombies.assets[instance.asset], instance));
        sourceIndices[instance.asset].push_back(sourceIndex);
    }
    // Allocate ownership before creating any GPU resource, so partial failures
    // are covered by the renderer constructor's cleanup path.
    zombieBatches_.resize(zombies.assets.size());
    zombieVisibility_.reset(zombies.instances.size());
    zombieBatchLocals_.resize(zombies.instances.size());
    visibleZombies_.reserve(zombies.instances.size());
    for (std::size_t i = 0; i < zombies.assets.size(); ++i) {
        const auto& source = zombies.assets[i];
        auto& batch = zombieBatches_[i];
        batch.sourceIndices = std::move(sourceIndices[i]);
        for (std::size_t local = 0; local < batch.sourceIndices.size(); ++local)
            zombieBatchLocals_[batch.sourceIndices[local]] = local;
        batch.staging = instances[i];
        if (source.frameCount < 2 || source.frameCount > static_cast<std::size_t>(std::numeric_limits<GLint>::max()) ||
            !std::isfinite(source.duration) || source.duration <= 0 ||
            source.animation.size() != source.model.primitives.size())
            throw std::runtime_error("Invalid baked zombie animation");
        batch.frameCount = static_cast<GLint>(source.frameCount);
        batch.duration = source.duration;
        batch.count = static_cast<GLsizei>(instances[i].size());
        if (source.skeleton.size() > static_cast<std::size_t>(std::numeric_limits<GLint>::max()))
            throw std::runtime_error("Zombie skeleton has too many bones");
        batch.skinningBoneCount = static_cast<GLint>(source.skeleton.size());
        batch.animationBounds = emptyBounds();
        batch.bindBounds = emptyBounds();
        for (const auto& primitive : source.model.primitives)
            for (const auto& vertex : primitive.vertices) {
                batch.bindBounds.minimum = glm::min(batch.bindBounds.minimum, vertex.position);
                batch.bindBounds.maximum = glm::max(batch.bindBounds.maximum, vertex.position);
            }
        for (const auto& primitive : source.animation)
            for (const auto& vertex : primitive.frames) {
                batch.animationBounds.minimum = glm::min(batch.animationBounds.minimum, glm::vec3(vertex.position));
                batch.animationBounds.maximum = glm::max(batch.animationBounds.maximum, glm::vec3(vertex.position));
            }
        // Baked frames are scene-space meters; skinned bind vertices can still
        // be mesh-local centimeters. Keep these boxes separate. Live skinning
        // transforms the bind box through the palette before using it below.
        for (const auto& instance : batch.staging)
            batch.instanceBounds.push_back(transformedBounds(batch.animationBounds, instance.transform));
        batch.cameraInstances.reserve(batch.staging.size());
        batch.shadowInstances.reserve(batch.staging.size());
        batch.poseBounds.assign(batch.staging.size(), batch.animationBounds);
        batch.poseVersions.resize(batch.staging.size());
        batch.livePoses.resize(batch.staging.size());
        batch.palettes = std::make_unique<ZombiePaletteCache>(batch.staging.size(), source.skeleton.size(),
                                                              static_cast<std::size_t>(maximumTexels));
        uploadZombieGeometry(source, batch, batch.staging.size(), maximumTexels);
        if (i < zombies.lodAssets.size()) {
            if (zombies.lodAssets[i].size() > 3) throw std::runtime_error("At most three zombie LOD levels are supported");
            batch.lods.resize(zombies.lodAssets[i].size());
            for (std::size_t level = 0; level < batch.lods.size(); ++level)
                uploadZombieGeometry(zombies.lodAssets[i][level], batch.lods[level], batch.staging.size(), maximumTexels);
        }
    }
    glBindTexture(GL_TEXTURE_BUFFER, 0);
    glBindBuffer(GL_TEXTURE_BUFFER, 0);
    updateZombies(zombies);
    checkErrors("zombie upload");
}

void Renderer::uploadZombieGeometry(const AnimatedModel& source, ZombieGeometry& geometry,
                                    std::size_t capacity, GLint maximumTexels) {
    geometry.skinningBoneCount = static_cast<GLint>(source.skeleton.size());
    geometry.frameCount = static_cast<GLint>(source.frameCount);
    geometry.duration = source.duration;
    geometry.count = static_cast<GLsizei>(capacity);
    geometry.cameraInstances.reserve(capacity);
    geometry.shadowInstances.reserve(capacity);
    uploadModel(source.model, geometry.model);
    glGenBuffers(1, &geometry.instanceBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, geometry.instanceBuffer);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(capacity * sizeof(ZombieGPUInstance)),
                 nullptr, GL_STREAM_DRAW);
    geometry.animationBuffers.resize(geometry.model.meshes.size());
    geometry.animationTextures.resize(geometry.model.meshes.size());
    geometry.skinningBuffers.resize(geometry.model.meshes.size());
    geometry.skinningEnabled.resize(geometry.model.meshes.size());
    geometry.vertexCounts.resize(geometry.model.meshes.size());
    for (std::size_t p = 0; p < geometry.model.meshes.size(); ++p) {
        const auto& mesh = geometry.model.meshes[p];
        const auto& mat = material(geometry.model, mesh.material);
        if (mat.alphaMode == "BLEND")
            throw std::runtime_error("Instanced zombies require opaque or alpha-mask materials");
        const auto count = source.model.primitives[p].vertices.size();
        const auto& frames = source.animation[p].frames;
        const auto& skinning = source.animation[p].skinning;
        if (count == 0 || count > static_cast<std::size_t>(std::numeric_limits<GLint>::max()) ||
            frames.size() / source.totalFrameCount() != count || frames.size() % source.totalFrameCount() != 0 ||
            frames.size() > static_cast<std::size_t>(maximumTexels) / 2)
            throw std::runtime_error("Zombie animation exceeds this GPU's texture buffer capacity or has invalid samples");
        geometry.vertexCounts[p] = static_cast<GLint>(count);
        geometry.skinningEnabled[p] = skinning.empty() ? 0 : 1;
        glBindVertexArray(mesh.vao);
        // Attribute pointers capture the current array buffer. The previous
        // primitive leaves its per-vertex skinning buffer bound below.
        glBindBuffer(GL_ARRAY_BUFFER, geometry.instanceBuffer);
        for (GLuint column = 0; column < 4; ++column) {
            glEnableVertexAttribArray(4 + column);
            glVertexAttribPointer(4 + column, 4, GL_FLOAT, GL_FALSE, sizeof(ZombieGPUInstance),
                                  reinterpret_cast<const void*>(offsetof(ZombieGPUInstance, transform) + column * sizeof(glm::vec4)));
            glVertexAttribDivisor(4 + column, 1);
        }
        glEnableVertexAttribArray(8);
        glVertexAttribPointer(8, 3, GL_FLOAT, GL_FALSE, sizeof(ZombieGPUInstance),
                              reinterpret_cast<const void*>(offsetof(ZombieGPUInstance, phase)));
        glVertexAttribDivisor(8, 1);
        glEnableVertexAttribArray(9);
        glVertexAttribPointer(9, 1, GL_FLOAT, GL_FALSE, sizeof(ZombieGPUInstance),
                              reinterpret_cast<const void*>(offsetof(ZombieGPUInstance, animationFrame)));
        glVertexAttribDivisor(9, 1);
        glEnableVertexAttribArray(10);
        glVertexAttribPointer(10, 2, GL_FLOAT, GL_FALSE, sizeof(ZombieGPUInstance),
                              reinterpret_cast<const void*>(offsetof(ZombieGPUInstance, liveSkinning)));
        glVertexAttribDivisor(10, 1);
        glEnableVertexAttribArray(11);
        glVertexAttribPointer(11, 1, GL_FLOAT, GL_FALSE, sizeof(ZombieGPUInstance),
                              reinterpret_cast<const void*>(offsetof(ZombieGPUInstance, boneBase)));
        glVertexAttribDivisor(11, 1);
        std::vector<SkinVertexGPU> skinVertices(count);
        if (!skinning.empty()) {
            if (skinning.size() != count) throw std::runtime_error("Zombie skinning data does not match vertices");
            for (std::size_t vertex = 0; vertex < count; ++vertex) {
                skinVertices[vertex].joints0 = {skinning[vertex].joints[0], skinning[vertex].joints[1],
                                                skinning[vertex].joints[2], skinning[vertex].joints[3]};
                skinVertices[vertex].weights0 = {skinning[vertex].weights[0], skinning[vertex].weights[1],
                                                 skinning[vertex].weights[2], skinning[vertex].weights[3]};
                skinVertices[vertex].joints1 = {skinning[vertex].joints[4], skinning[vertex].joints[5],
                                                skinning[vertex].joints[6], skinning[vertex].joints[7]};
                skinVertices[vertex].weights1 = {skinning[vertex].weights[4], skinning[vertex].weights[5],
                                                 skinning[vertex].weights[6], skinning[vertex].weights[7]};
            }
        }
        glGenBuffers(1, &geometry.skinningBuffers[p]);
        glBindBuffer(GL_ARRAY_BUFFER, geometry.skinningBuffers[p]);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(skinVertices.size() * sizeof(SkinVertexGPU)),
                     skinVertices.data(), GL_STATIC_DRAW);
        glBindVertexArray(mesh.vao);
        glBindBuffer(GL_ARRAY_BUFFER, geometry.skinningBuffers[p]);
        glEnableVertexAttribArray(12);
        glVertexAttribIPointer(12, 4, GL_UNSIGNED_INT, sizeof(SkinVertexGPU),
                               reinterpret_cast<const void*>(offsetof(SkinVertexGPU, joints0)));
        glEnableVertexAttribArray(13);
        glVertexAttribPointer(13, 4, GL_FLOAT, GL_FALSE, sizeof(SkinVertexGPU),
                              reinterpret_cast<const void*>(offsetof(SkinVertexGPU, weights0)));
        glEnableVertexAttribArray(14);
        glVertexAttribIPointer(14, 4, GL_UNSIGNED_INT, sizeof(SkinVertexGPU),
                               reinterpret_cast<const void*>(offsetof(SkinVertexGPU, joints1)));
        glEnableVertexAttribArray(15);
        glVertexAttribPointer(15, 4, GL_FLOAT, GL_FALSE, sizeof(SkinVertexGPU),
                              reinterpret_cast<const void*>(offsetof(SkinVertexGPU, weights1)));
        glGenBuffers(1, &geometry.animationBuffers[p]);
        glBindBuffer(GL_TEXTURE_BUFFER, geometry.animationBuffers[p]);
        glBufferData(GL_TEXTURE_BUFFER, static_cast<GLsizeiptr>(frames.size() * sizeof(AnimationVertex)),
                     frames.data(), GL_STATIC_DRAW);
        glGenTextures(1, &geometry.animationTextures[p]);
        glBindTexture(GL_TEXTURE_BUFFER, geometry.animationTextures[p]);
        glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, geometry.animationBuffers[p]);
    }
}

void Renderer::updateZombies(const ZombieLayer& zombies) {
    if (zombieBatches_.size() != zombies.assets.size())
        throw std::runtime_error("Zombie layer changed after renderer upload");
    std::size_t population = 0;
    for (const auto& batch : zombieBatches_) population += batch.sourceIndices.size();
    if (population != zombies.instances.size())
        throw std::runtime_error("Zombie population changed after renderer upload");
    zombieSource_ = &zombies;
    zombieCount_ = 0;
    for (std::size_t i = 0; i < zombieBatches_.size(); ++i) {
        auto& batch = zombieBatches_[i];
        if (zombies.assets[i].skeleton.size() != static_cast<std::size_t>(batch.skinningBoneCount))
            throw std::runtime_error("Zombie skeleton changed after renderer upload");
        for (std::size_t local = 0; local < batch.sourceIndices.size(); ++local) {
            const auto sourceIndex = batch.sourceIndices[local];
            if (sourceIndex >= zombies.instances.size()) throw std::runtime_error("Invalid zombie source index");
            const auto& source = zombies.instances[sourceIndex];
            if (source.asset != i) throw std::runtime_error("Zombie asset changed after renderer upload");
            zombieCount_ += !source.removed;
            if (source.removed) {
                zombieVisibility_.erase(sourceIndex);
                batch.palettes->release(local);
                continue;
            }
            const bool live = source.ragdoll;
            if (!live) batch.palettes->release(local);
            if (live && source.ragdollBones.size() != static_cast<std::size_t>(batch.skinningBoneCount))
                throw std::runtime_error("Live skeleton data does not match zombie asset");
            batch.staging[local] = zombieGPUInstance(zombies.assets[i], source);
            // Bone-transformed bind boxes contain every weighted skin vertex;
            // animation bounds also cover primitives without skin attributes.
            // Cache the expensive union by pose revision. World placement
            // is still applied each update, including ordinary living movement.
            if (live != batch.livePoses[local] || (live && source.ragdollPoseVersion != batch.poseVersions[local])) {
                auto bounds = batch.animationBounds;
                if (live)
                    for (const auto& bone : source.ragdollBones)
                        includeBounds(bounds, transformedBounds(batch.bindBounds, bone));
                batch.poseBounds[local] = bounds;
                batch.poseVersions[local] = source.ragdollPoseVersion;
                batch.livePoses[local] = live;
            }
            const auto bounds = transformedBounds(batch.poseBounds[local], source.transform);
            auto& previous = batch.instanceBounds[local];
            const bool changed = bounds.minimum != previous.minimum || bounds.maximum != previous.maximum;
            previous = bounds;
            if (changed || !zombieVisibility_.contains(sourceIndex))
                zombieVisibility_.update(sourceIndex, previous);
        }
    }
}

void Renderer::prepareZombieDraws(const Frustum& cameraFrustum, const Frustum* lightFrustum,
                                  const Camera& camera, const glm::mat4& projection, double seconds, const Frustum* flashlightFrustum) {
    zombiePaletteStats_ = {};
    zombieRenderStats_ = {};
    zombieVisibilityStats_ = {};
    if (!zombieSource_) return;
    if (!std::isfinite(seconds) || seconds < 0) seconds = 0;
    // Keep the last published camera pose until a new frame replaces it, even
    // when owning simulation updates occur between input and rendering.
    for (const auto id : displayedZombieSources_) zombieSource_->instances[id].displayedAnimationFrame = -1;
    displayedZombieSources_.clear();
    for (auto& batch : zombieBatches_) {
        batch.cameraInstances.clear();
        batch.shadowInstances.clear();
        batch.flashlightInstances.clear();
        for (auto& lod : batch.lods) { lod.cameraInstances.clear(); lod.shadowInstances.clear(); lod.flashlightInstances.clear(); }
        batch.palettes->beginFrame();
    }
    zombieVisibilityStats_ = zombieVisibility_.query(cameraFrustum, lightFrustum, zombieCulling_, visibleZombies_, flashlightFrustum);
    for (const auto& visible : visibleZombies_) {
        const auto& source = zombieSource_->instances[visible.instance];
        auto& batch = zombieBatches_[source.asset];
        const auto local = zombieBatchLocals_[visible.instance];
        auto instance = batch.staging[local];
        const auto& bounds = batch.instanceBounds[local];
        const float distance = glm::length((bounds.minimum + bounds.maximum) * .5f - camera.position());
        const auto detail = zombieCameraDetail(distance, projection[1][1], zombieDistanceDetail_, zombieDrawDistance_);
        const bool cameraVisible = visible.camera && detail.visible;
        if (visible.camera && !detail.visible) ++zombieRenderStats_.distanceCulled;
        const bool shadowVisible = visible.shadow && zombieShadows_;
        const bool flashlightVisible = visible.flashlight && zombieShadows_;
        if (!cameraVisible && !shadowVisible && !flashlightVisible) continue;
        if (source.ragdoll && batch.skinningBoneCount > 0)
            instance.boneBase = batch.palettes->request(local, source.ragdollPoseVersion, source.ragdollBones);
        const auto& asset = zombieSource_->assets[source.asset];
        const auto* clip = zombieAnimationClip(asset, source);
        const float duration = clip ? clip->duration : asset.duration;
        const auto frames = clip ? clip->frameCount : asset.frameCount;
        const double time = clip ? source.walkSeconds : seconds;
        const float frame = source.ragdoll ? 0 : animatedFrame(duration, frames, time, source.phase);
        auto cameraInstance = instance;
        if (cameraVisible) {
            cameraInstance.animationFrame = frame;
            if (detail.animationHz > 0 && !source.ragdoll) {
                // Snap to authored baked frames on a lower-rate cycle grid. Both
                // GPU and hit tests use this same published pose.
                const auto samples = std::max(1.0f, std::floor(duration * detail.animationHz));
                const auto bucket = std::floor(frame / static_cast<float>(frames) * samples);
                cameraInstance.animationFrame = std::floor(bucket / samples * static_cast<float>(frames));
                cameraInstance.animationStep = 1;
            }
            displayedZombieSources_.push_back(visible.instance);
            source.displayedAnimationFrame = cameraInstance.animationFrame;
            source.displayedAnimationClip = clip == (asset.runClip ? &*asset.runClip : nullptr) && clip ? 2 : clip ? 1 : 0;
            const auto level = std::min(detail.level, batch.lods.size());
            auto& geometry = level ? batch.lods[level - 1] : static_cast<ZombieGeometry&>(batch);
            geometry.cameraInstances.push_back(cameraInstance);
            ++zombieRenderStats_.cameraLevels[level];
        }
        if (shadowVisible) {
            // Lowest mesh is adequate for the limited-resolution sun map.
            // Keep the camera pose when available so near silhouettes align.
            auto shadowInstance = cameraVisible ? cameraInstance : instance;
            if (!cameraVisible) { shadowInstance.animationFrame = std::floor(frame); shadowInstance.animationStep = 1; }
            const auto level = zombieDistanceDetail_ ? batch.lods.size() : 0;
            auto& geometry = level ? batch.lods[level - 1] : static_cast<ZombieGeometry&>(batch);
            geometry.shadowInstances.push_back(shadowInstance);
            ++zombieRenderStats_.shadowLevels[level];
        }
        if (flashlightVisible) {
            auto caster = cameraVisible ? cameraInstance : instance;
            if (!cameraVisible) { caster.animationFrame = std::floor(frame); caster.animationStep = 1; }
            // Nearby flashlight silhouettes need more detail than sun shadows.
            const auto level = zombieDistanceDetail_ && distance >= 12 ? std::min(std::size_t(1), batch.lods.size()) : 0;
            auto& geometry = level ? batch.lods[level - 1] : static_cast<ZombieGeometry&>(batch);
            geometry.flashlightInstances.push_back(caster);
            ++zombieRenderStats_.flashlightLevels[level];
        }
    }
    for (auto& batch : zombieBatches_) {
        batch.palettes->flush();
        const auto& stats = batch.palettes->stats();
        zombiePaletteStats_.needed += stats.needed;
        zombiePaletteStats_.uploaded += stats.uploaded;
        zombiePaletteStats_.uploadCalls += stats.uploadCalls;
        zombiePaletteStats_.uploadBytes += stats.uploadBytes;
        zombiePaletteStats_.resident += stats.resident;
        zombiePaletteStats_.storageBytes += stats.storageBytes;
    }
    checkErrors("zombie palette update");
}

void Renderer::drawZombies(double seconds, ZombiePass pass) {
    const bool shadowPass = pass != ZombiePass::Camera;
    if (!shadowPass) {
        zombieDrawCalls_ = 0;
        zombieVisibleCount_ = 0;
    }
    if (zombieBatches_.empty()) return;
    if (!std::isfinite(seconds) || seconds < 0) seconds = 0;
    glUniform1i(instancedLocation_, GL_TRUE);
    glUniform1i(animatedLocation_, GL_TRUE);
    glUniform1i(animationLocation_, 1);
    glUniform1i(skinningBonesLocation_, 2);
    const glm::mat4 identity(1);
    glUniformMatrix4fv(modelLocation_, 1, GL_FALSE, glm::value_ptr(identity));
    for (auto& batch : zombieBatches_) {
        for (std::size_t level = 0; level <= batch.lods.size(); ++level) {
            auto& geometry = level ? batch.lods[level - 1] : static_cast<ZombieGeometry&>(batch);
            if (geometry.count == 0) continue;
            const auto& visible = pass == ZombiePass::FlashlightShadow ? geometry.flashlightInstances :
                shadowPass ? geometry.shadowInstances : geometry.cameraInstances;
            const auto count = static_cast<GLsizei>(visible.size());
            if (!shadowPass) zombieVisibleCount_ += visible.size();
            if (count == 0) continue;
            glBindBuffer(GL_ARRAY_BUFFER, geometry.instanceBuffer);
            // Orphan storage before streaming the compact list to avoid overwriting
            // an in-flight camera or shadow draw. Keep full-population capacity.
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(batch.staging.size() * sizeof(ZombieGPUInstance)),
                         nullptr, GL_STREAM_DRAW);
            glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(visible.size() * sizeof(ZombieGPUInstance)),
                            visible.data());
            // Reduce in double precision before converting to float to preserve
            // smooth playback even after the application has run for many hours.
            const float frame = static_cast<float>(std::fmod(seconds, geometry.duration) / geometry.duration * geometry.frameCount);
            glUniform1f(animationFrameLocation_, frame);
            glUniform1i(animationCountLocation_, geometry.frameCount);
            for (std::size_t p = 0; p < geometry.model.meshes.size(); ++p) {
                const auto& mesh = geometry.model.meshes[p];
                applyMaterial(geometry.model, mesh.material, false);
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_BUFFER, geometry.animationTextures[p]);
                glActiveTexture(GL_TEXTURE2);
                glBindTexture(GL_TEXTURE_BUFFER, batch.palettes->texture());
                glActiveTexture(GL_TEXTURE0);
                glUniform1i(animationVerticesLocation_, geometry.vertexCounts[p]);
                glUniform1i(skinningLocation_, geometry.skinningEnabled[p]);
                glUniform1i(skinningBoneCountLocation_, geometry.skinningBoneCount);
                glBindVertexArray(mesh.vao);
                glDrawElementsInstanced(GL_TRIANGLES, mesh.count, GL_UNSIGNED_INT, nullptr, count);
                if (!shadowPass) {
                    ++zombieDrawCalls_;
                    zombieRenderStats_.cameraTriangles += visible.size() * static_cast<std::size_t>(mesh.count / 3);
                } else if (pass == ZombiePass::SunShadow) zombieRenderStats_.shadowTriangles += visible.size() * static_cast<std::size_t>(mesh.count / 3);
                else zombieRenderStats_.flashlightTriangles += visible.size() * static_cast<std::size_t>(mesh.count / 3);
            }
        }
    }
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_BUFFER, 0);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_BUFFER, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUniform1i(animatedLocation_, GL_FALSE);
    glUniform1i(instancedLocation_, GL_FALSE);
    glFrontFace(GL_CCW);
}

void Renderer::releaseZombies() noexcept {
    for (auto& batch : zombieBatches_) {
        for (std::size_t level = 0; level <= batch.lods.size(); ++level) {
            auto& geometry = level ? batch.lods[level - 1] : static_cast<ZombieGeometry&>(batch);
            releaseModel(geometry.model);
            glDeleteBuffers(1, &geometry.instanceBuffer);
            for (const auto buffer : geometry.animationBuffers) glDeleteBuffers(1, &buffer);
            for (const auto texture : geometry.animationTextures) glDeleteTextures(1, &texture);
            for (const auto buffer : geometry.skinningBuffers) glDeleteBuffers(1, &buffer);
        }
        batch.palettes.reset();
    }
}

} // namespace viewer
