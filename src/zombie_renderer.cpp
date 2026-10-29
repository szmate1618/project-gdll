#include "renderer.hpp"
#include "frame_stage_profile.hpp"
#include "collision_world.hpp"
#include "zombie_animation.hpp"
#include "zombie_animation_buffer.hpp"
#include "zombie_lod.hpp"
#include "zombie_bounds.hpp"

#include <cmath>
#include <cstddef>
#include <array>
#include <algorithm>
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
    return transformedZombieBounds(bounds, transform);
}
}

Renderer::ZombieGPUInstance Renderer::zombieGPUInstance(const AnimatedModel& asset, const ZombieInstance& instance,
                                                       float boneBase) {
    const auto* clip = zombieAnimationClip(asset, instance);
    return {instance.transform, instance.phase,
            clip ? static_cast<float>(clip->firstFrame) : 0.0f,
            clip ? static_cast<float>(clip->frameCount) : 0.0f,
            0.0f,
            -1.0f, // Draw preparation assigns the pose after visibility selection.
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
    zombieBatchAssets_.resize(zombies.instances.size());
    zombieRenderStates_.resize(zombies.instances.size());
    zombieCount_ = zombies.instances.size();
    visibleZombies_.reserve(zombies.instances.size());
    displayedZombieSources_.reserve(zombies.instances.size());
    nextDisplayedZombieSources_.reserve(zombies.instances.size());
    for (std::size_t i = 0; i < zombies.assets.size(); ++i) {
        const auto& source = zombies.assets[i];
        auto& batch = zombieBatches_[i];
        batch.sourceIndices = std::move(sourceIndices[i]);
        for (std::size_t local = 0; local < batch.sourceIndices.size(); ++local) {
            zombieBatchLocals_[batch.sourceIndices[local]] = local;
            zombieBatchAssets_[batch.sourceIndices[local]] = i;
        }
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
        batch.woundSurfaces.push_back(prepareZombieWoundSurface(source));
        if (i < zombies.dismembermentAssets.size())
            for (const auto& mesh : zombies.dismembermentAssets[i].meshes)
                batch.woundSurfaces.push_back(prepareZombieWoundSurface(mesh));
        batch.closures.resize(batch.woundSurfaces.size());
        for (std::size_t variant=0; variant<batch.woundSurfaces.size(); ++variant) {
            const auto& asset = variant == 0 ? source : zombies.dismembermentAssets[i].meshes[variant-1];
            std::size_t count = batch.woundSurfaces[variant].closures.size();
            for (const auto& primitive : asset.model.primitives) count += primitive.indices.size()/3;
            batch.woundStride = std::max(batch.woundStride,count);
        }
        batch.woundLimit = std::min(std::size_t(maximumTexels), std::size_t(1 << 24));
        batch.animationBounds = emptyBounds();
        batch.bindBounds = emptyBounds();
        includeZombieGeometryBounds(source, batch.animationBounds, batch.bindBounds);
        // Fitted LODs can extend beyond the original mesh. Include every level
        // before publishing living bounds, and retain its bind extent for live poses.
        if (i < zombies.lodAssets.size())
            for (const auto& lod : zombies.lodAssets[i])
                includeZombieGeometryBounds(lod, batch.animationBounds, batch.bindBounds);
        // The GPU may round baked positions to FP16. Enclose both canonical
        // CPU poses and decoded GPU endpoints before publishing culling bounds.
        batch.animationBounds = zombieAnimationHalfBounds(batch.animationBounds);
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
        batch.removedPoses.resize(batch.staging.size());
        batch.palettes = std::make_unique<ZombiePaletteCache>(batch.staging.size(), source.skeleton.size(),
                                                              static_cast<std::size_t>(maximumTexels));
        uploadZombieGeometry(source, batch, batch.staging.size(), maximumTexels);
        if (i < zombies.dismembermentAssets.size()) {
            const auto& prepared = zombies.dismembermentAssets[i].meshes;
            batch.severed.resize(prepared.size());
            for (std::size_t j = 0; j < prepared.size(); ++j) {
                uploadZombieGeometry(prepared[j], batch.severed[j], batch.staging.size(), maximumTexels);
                for (const auto& primitive : prepared[j].model.primitives)
                    for (const auto& vertex : primitive.vertices) {
                        batch.bindBounds.minimum = glm::min(batch.bindBounds.minimum, vertex.position);
                        batch.bindBounds.maximum = glm::max(batch.bindBounds.maximum, vertex.position);
                    }
            }
        }
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
        glVertexAttribPointer(8, 4, GL_FLOAT, GL_FALSE, sizeof(ZombieGPUInstance),
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
        const auto packed = packZombieAnimation(frames);
        const auto bytes = frames.size() * (packed ? sizeof(PackedZombieAnimationSample) : sizeof(AnimationVertex));
        const void* data = packed ? static_cast<const void*>(packed->data()) : static_cast<const void*>(frames.data());
        glBufferData(GL_TEXTURE_BUFFER, static_cast<GLsizeiptr>(bytes), data, GL_STATIC_DRAW);
        glGenTextures(1, &geometry.animationTextures[p]);
        glBindTexture(GL_TEXTURE_BUFFER, geometry.animationTextures[p]);
        glTexBuffer(GL_TEXTURE_BUFFER, packed ? GL_RGBA16F : GL_RGBA32F, geometry.animationBuffers[p]);
    }
}

void Renderer::updateZombies(const ZombieLayer& zombies) {
    if (zombieBatches_.size() != zombies.assets.size())
        throw std::runtime_error("Zombie layer changed after renderer upload");
    if (zombieBatchLocals_.size() != zombies.instances.size())
        throw std::runtime_error("Zombie population changed after renderer upload");
    for (std::size_t asset = 0; asset < zombieBatches_.size(); ++asset)
        if (zombies.assets[asset].skeleton.size() != static_cast<std::size_t>(zombieBatches_[asset].skinningBoneCount))
            throw std::runtime_error("Zombie skeleton changed after renderer upload");
    const auto& changes = zombies.renderUpdates();
    const auto update = changes.since(zombieUpdateRevision_);
    const bool full = zombieSource_ != &zombies || update.full;
    zombieSource_ = &zombies;
    zombieUpdateStats_ = {0, 0, full};
    if (full) {
        for (std::size_t asset = 0; asset < zombies.assets.size(); ++asset) {
            const auto& source = zombies.assets[asset];
            auto& timings = zombieBatches_[asset].clipTimings;
            timings[0] = {source.duration, source.frameCount};
            timings[1] = source.walkClip ? ZombieClipTiming{source.walkClip->duration, source.walkClip->frameCount} : timings[0];
            timings[2] = source.runClip ? ZombieClipTiming{source.runClip->duration, source.runClip->frameCount} : timings[0];
        }
        for (std::size_t id = 0; id < zombies.instances.size(); ++id)
            updateZombieInstance(id, true, true);
    } else if (update.ids) {
        for (const auto id : *update.ids)
            updateZombieInstance(id, changes.boundsChanged(id), false);
    }
    // Publishing the cursor and retiring the batch happen only after all
    // instances succeed. Other consumers that missed it refresh conservatively.
    zombieUpdateRevision_ = update.revision;
    changes.acknowledge(update.revision);
}

void Renderer::updateZombieInstance(std::size_t id, bool boundsChanged, bool full) {
    if (id >= zombieBatchLocals_.size()) throw std::runtime_error("Invalid zombie source index");
    const auto asset = zombieBatchAssets_[id];
    auto& batch = zombieBatches_[asset];
    const auto local = zombieBatchLocals_[id];
    const auto& source = zombieSource_->instances[id];
    if (source.asset != asset) throw std::runtime_error("Zombie asset changed after renderer upload");
    auto& render = zombieRenderStates_[id];
    render.asset = asset;
    render.local = local;
    render.displayedFrame = -1;
    render.severedJoint = source.severedJoint;
    render.ragdoll = source.ragdoll;
    const auto& model = zombieSource_->assets[asset];
    const auto* clip = zombieAnimationClip(model, source);
    render.clip = !clip ? ZombieRenderClip::Idle :
        model.runClip && clip == &*model.runClip ? ZombieRenderClip::Run : ZombieRenderClip::Walk;
    render.animationSeconds = clip ? source.walkSeconds : source.idleSeconds;
    ++zombieUpdateStats_.instances;
    if (source.removed) {
        updateZombieWound(id, batch, batch.staging[local]);
        if (!batch.removedPoses[local]) --zombieCount_;
        batch.removedPoses[local] = true;
        zombieVisibility_.erase(id);
        batch.palettes->release(local);
        return;
    }
    if (batch.removedPoses[local]) ++zombieCount_;
    batch.removedPoses[local] = false;
    const bool live = source.ragdoll;
    if (full || !live) batch.palettes->release(local);
    if (live && source.ragdollBones.size() != static_cast<std::size_t>(batch.skinningBoneCount))
        throw std::runtime_error("Live skeleton data does not match zombie asset");
    batch.staging[local] = zombieGPUInstance(zombieSource_->assets[asset], source);
    updateZombieWound(id, batch, batch.staging[local]);
    const bool poseChanged = full || live != batch.livePoses[local] ||
        (live && source.ragdollPoseVersion != batch.poseVersions[local]);
    if (poseChanged) {
        auto bounds = batch.animationBounds;
        if (live)
            for (const auto& bone : source.ragdollBones)
                includeBounds(bounds, transformedBounds(batch.bindBounds, bone));
        batch.poseBounds[local] = bounds;
        batch.poseVersions[local] = source.ragdollPoseVersion;
        batch.livePoses[local] = live;
    }
    if (boundsChanged || poseChanged || !zombieVisibility_.contains(id)) {
        ++zombieUpdateStats_.bounds;
        const auto bounds = transformedBounds(batch.poseBounds[local], source.transform);
        auto& previous = batch.instanceBounds[local];
        const bool changed = bounds.minimum != previous.minimum || bounds.maximum != previous.maximum;
        previous = bounds;
        render.center = (previous.minimum + previous.maximum) * .5f;
        if (changed || !zombieVisibility_.contains(id)) zombieVisibility_.update(id, previous);
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
    nextDisplayedZombieSources_.clear();
    for (auto& batch : zombieBatches_) {
        const auto& idle = batch.clipTimings[static_cast<std::size_t>(ZombieRenderClip::Idle)];
        batch.idleFrameBase = animatedFrameBase(idle.duration, idle.frameCount, seconds);
        batch.cameraInstances.clear();
        batch.woundedCameraInstances.clear();
        batch.shadowInstances.clear();
        batch.flashlightInstances.clear();
        for (auto& lod : batch.lods) {
            lod.cameraInstances.clear(); lod.woundedCameraInstances.clear();
            lod.shadowInstances.clear(); lod.flashlightInstances.clear();
        }
        for (auto& mesh : batch.severed) {
            mesh.cameraInstances.clear(); mesh.woundedCameraInstances.clear();
            mesh.shadowInstances.clear(); mesh.flashlightInstances.clear();
        }
        batch.palettes->beginFrame();
    }
    {
        frame_stage_profile::Timer timer(frame_stage_profile::Stage::zombieQuery);
        std::optional<VisibilityBounds> cameraRegion;
        if (const auto radius = zombieCameraDrawRadius(projection[1][1], zombieDistanceDetail_, zombieDrawDistance_)) {
            const auto eye = camera.position();
            // Include coordinate-rounding error as well as policy rounding.
            const float padding = 8 * std::numeric_limits<float>::epsilon() *
                std::max({1.f, std::abs(eye.x), std::abs(eye.y), std::abs(eye.z)});
            const glm::vec3 extent(*radius + padding);
            cameraRegion = VisibilityBounds{eye - extent, eye + extent};
        }
        zombieVisibilityStats_ = zombieVisibility_.query(cameraFrustum, zombieShadows_ ? lightFrustum : nullptr,
            zombieCulling_, visibleZombies_, zombieShadows_ ? flashlightFrustum : nullptr,
            cameraRegion ? &*cameraRegion : nullptr);
    }
    {
        frame_stage_profile::Timer timer(frame_stage_profile::Stage::zombieSelection);
        for (const auto& visible : visibleZombies_) {
            auto& render = zombieRenderStates_[visible.instance];
            auto& batch = zombieBatches_[render.asset];
            const auto local = render.local;
            auto instance = batch.staging[local];
            if (forestViewRange_ && !forestViewRange_->intersects(batch.instanceBounds[local])) continue;
            const float distance = glm::length(render.center - camera.position());
            const auto detail = zombieCameraDetail(distance, projection[1][1], zombieDistanceDetail_, zombieDrawDistance_);
            const bool cameraVisible = visible.camera && detail.visible;
            if (visible.camera && !detail.visible) ++zombieRenderStats_.distanceCulled;
            const bool shadowVisible = visible.shadow && zombieShadows_;
            const bool flashlightVisible = visible.flashlight && zombieShadows_;
            if (!cameraVisible && !shadowVisible && !flashlightVisible) continue;
            if (render.ragdoll && batch.skinningBoneCount > 0) {
                const auto& source = zombieSource_->instances[visible.instance];
                instance.boneBase = batch.palettes->request(local, source.ragdollPoseVersion, source.ragdollBones);
            }
            const auto& timing = batch.clipTimings[static_cast<std::size_t>(render.clip)];
            const float duration = timing.duration;
            const auto frames = timing.frameCount;
            const float frame = render.ragdoll ? 0 : render.clip == ZombieRenderClip::Idle && render.animationSeconds < 0 ?
                animatedFrameFromBase(batch.idleFrameBase, frames, instance.phase) :
                animatedFrame(duration, frames, render.animationSeconds, instance.phase);
            if (render.severedJoint >= 0) {
                if (!render.ragdoll || std::size_t(render.severedJoint) >= batch.severed.size())
                    throw std::runtime_error("Invalid severed zombie geometry");
                auto& mesh = batch.severed[std::size_t(render.severedJoint)];
                instance.animationFrame = 0;
                if (cameraVisible) { mesh.woundedCameraInstances.push_back(instance); ++zombieRenderStats_.cameraLevels[0]; }
                if (shadowVisible) { mesh.shadowInstances.push_back(instance); ++zombieRenderStats_.shadowLevels[0]; }
                if (flashlightVisible) { mesh.flashlightInstances.push_back(instance); ++zombieRenderStats_.flashlightLevels[0]; }
                continue;
            }
            auto cameraInstance = instance;
            if (cameraVisible) {
                cameraInstance.animationFrame = frame;
                if (detail.animationHz > 0 && !render.ragdoll) {
                    // Snap to authored baked frames on a lower-rate cycle grid. Both
                    // GPU and hit tests use this same published pose.
                    const auto samples = std::max(1.0f, std::floor(duration * detail.animationHz));
                    const auto bucket = std::floor(frame / static_cast<float>(frames) * samples);
                    cameraInstance.animationFrame = std::floor(bucket / samples * static_cast<float>(frames));
                    cameraInstance.animationStep = 1;
                }
                nextDisplayedZombieSources_.push_back(visible.instance);
                render.displayedFrame = cameraInstance.animationFrame;
                const auto& source = zombieSource_->instances[visible.instance];
                const auto clip = static_cast<int>(render.clip);
                // Another renderer can publish a different pose into this layer.
                // Compare authoritative fields rather than a private last value.
                if (source.displayedAnimationFrame != cameraInstance.animationFrame || source.displayedAnimationClip != clip) {
                    source.displayedAnimationFrame = cameraInstance.animationFrame;
                    source.displayedAnimationClip = clip;
                    ++zombieRenderStats_.posePublications;
                }
                const auto level = cameraInstance.woundBase > 0 ? 0 : std::min(detail.level, batch.lods.size());
                auto& geometry = level ? batch.lods[level - 1] : static_cast<ZombieGeometry&>(batch);
                auto& cameraInstances = cameraInstance.woundBase > 0 ? geometry.woundedCameraInstances : geometry.cameraInstances;
                cameraInstances.push_back(cameraInstance);
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
        // Only actors that left this renderer's camera list need retirement.
        // Visibility already returns unique ascending source IDs, so merge the
        // two retained lists without another sort or a population-wide clear.
        auto current = nextDisplayedZombieSources_.begin();
        for (const auto id : displayedZombieSources_) {
            while (current != nextDisplayedZombieSources_.end() && *current < id) ++current;
            if (current != nextDisplayedZombieSources_.end() && *current == id) continue;
            zombieRenderStates_[id].displayedFrame = -1;
            const auto& source = zombieSource_->instances[id];
            if (source.displayedAnimationFrame != -1) {
                source.displayedAnimationFrame = -1;
                ++zombieRenderStats_.poseRetirements;
            }
        }
        displayedZombieSources_.swap(nextDisplayedZombieSources_);
    }
    {
        frame_stage_profile::Timer timer(frame_stage_profile::Stage::zombiePalettes);
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
    }
    {
        frame_stage_profile::Timer timer(frame_stage_profile::Stage::zombieErrorCheck);
        checkErrors("zombie palette update");
    }
}

std::optional<ZombieShotTrace> Renderer::traceZombieShot(glm::vec3 origin, glm::vec3 direction,
                                                       const CollisionWorld& world) {
    if (!zombieSource_) return std::nullopt;
    for (int axis = 0; axis < 3; ++axis)
        if (!std::isfinite(origin[axis]) || !std::isfinite(direction[axis])) return std::nullopt;
    const double length = glm::length(glm::dvec3(direction));
    if (length < 1e-12) return std::nullopt;
    direction = glm::vec3(glm::dvec3(direction) / length);
    zombieVisibility_.queryRay(origin, direction, zombieShotCandidates_);
    std::optional<ZombieShotTrace> nearest;
    for (const auto id : zombieShotCandidates_) {
        const auto& render = zombieRenderStates_[id];
        if (render.ragdoll || render.displayedFrame < 0) continue;
        const auto& batch = zombieBatches_[render.asset];
        const auto& cached = batch.staging[render.local];
        const auto& source = zombieSource_->instances[id];
        const auto currentModel = glm::dmat4(source.restTransform);
        if (!std::isfinite(glm::determinant(currentModel)) || glm::determinant(currentModel) == 0) continue;
        ZombieInstance displayed;
        displayed.asset = render.asset;
        displayed.transform = displayed.restTransform = cached.transform;
        // Fixed affine asset normalization cancels here. Preserve its scale,
        // shear and reflection while recovering the old rigid gameplay root.
        displayed.restRoot = glm::mat4(glm::dmat4(cached.transform) * glm::inverse(currentModel) *
                                      glm::dmat4(source.restRoot));
        displayed.displayedAnimationFrame = render.displayedFrame;
        displayed.displayedAnimationClip = static_cast<int>(render.clip);
        displayed.walking = render.clip != ZombieRenderClip::Idle;
        displayed.running = render.clip == ZombieRenderClip::Run;
        const auto& timing = zombieBatches_[render.asset].clipTimings[static_cast<std::size_t>(render.clip)];
        const double seconds = timing.frameCount ? double(render.displayedFrame) * timing.duration / timing.frameCount : 0;
        displayed.walkSeconds = displayed.idleSeconds = seconds;
        const ZombieWound* displayedWound = nullptr;
        if (cached.woundBase > 0) {
            const auto row = batch.woundRows.find(id);
            if (row == batch.woundRows.end()) continue;
            displayedWound = &row->second.displayedWound;
        }
        const auto hit = zombieSource_->traceShot(id, displayed, origin, direction, seconds, displayedWound);
        if (hit && (!nearest || hit->distance < nearest->distance)) nearest = hit;
    }
    if (nearest && !world.lineOfSight(origin, origin + direction * nearest->distance)) return std::nullopt;
    return nearest;
}

void Renderer::drawZombies(double seconds, ZombiePass pass) {
    frame_stage_profile::Timer timer(frame_stage_profile::Stage::zombieDraws);
    const bool shadowPass = pass != ZombiePass::Camera;
    if (!shadowPass) {
        zombieDrawCalls_ = 0;
        zombieVisibleCount_ = 0;
    }
    if (zombieBatches_.empty()) return;
    if (!std::isfinite(seconds) || seconds < 0) seconds = 0;
    // Keep program changes outside the asset/LOD loops. An unwounded crowd
    // retains one draw per mesh, while mixed geometry uses one per group.
    if (!shadowPass) drawZombieGroup(seconds, pass, false);
    drawZombieGroup(seconds, pass, true);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_BUFFER, 0);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_BUFFER, 0);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_BUFFER, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUniform1i(animatedLocation_, GL_FALSE);
    glUniform1i(zombieCullBackfacesLocation_, GL_FALSE);
    glUniform1i(instancedLocation_, GL_FALSE);
    glFrontFace(GL_CCW);
}

void Renderer::drawZombieGroup(double seconds, ZombiePass pass, bool wounds) {
    const bool shadowPass = pass != ZombiePass::Camera;
    selectBasicShader(wounds);
    glUniform1i(instancedLocation_, GL_TRUE);
    glUniform1i(animatedLocation_, GL_TRUE);
    glUniform1i(animationLocation_, 1);
    glUniform1i(skinningBonesLocation_, 2);
    const glm::mat4 identity(1);
    glUniformMatrix4fv(modelLocation_, 1, GL_FALSE, glm::value_ptr(identity));
    for (auto& batch : zombieBatches_) {
        if (wounds) {
            glActiveTexture(GL_TEXTURE5);
            glBindTexture(GL_TEXTURE_BUFFER, batch.woundTexture);
            glActiveTexture(GL_TEXTURE0);
        }
        const auto surfaceLevels = 1+batch.lods.size()+batch.severed.size();
        const auto geometryLevels = wounds ? surfaceLevels+batch.closures.size() : 1+batch.lods.size();
        for (std::size_t level = 0; level < geometryLevels; ++level) {
            const bool closure = level >= surfaceLevels;
            if (closure && shadowPass) continue;
            const auto variant = closure ? level-surfaceLevels : 0;
            auto& geometry = closure ? batch.closures[variant] : level == 0 ? static_cast<ZombieGeometry&>(batch) :
                level <= batch.lods.size() ? batch.lods[level-1] : batch.severed[level-batch.lods.size()-1];
            if (geometry.count == 0) continue;
            if (closure) {
                const auto& exterior = variant == 0 ? static_cast<ZombieGeometry&>(batch) : batch.severed[variant-1];
                geometry.cameraInstances.clear();
                for (const auto& instance : exterior.woundedCameraInstances)
                    if (instance.woundBase > 0) geometry.cameraInstances.push_back(instance);
            }
            const auto& visible = pass == ZombiePass::FlashlightShadow ? geometry.flashlightInstances :
                shadowPass ? geometry.shadowInstances :
                wounds && !closure ? geometry.woundedCameraInstances : geometry.cameraInstances;
            const auto count = static_cast<GLsizei>(visible.size());
            if (!shadowPass && !closure) zombieVisibleCount_ += visible.size();
            if (count == 0) continue;
            const bool torsoBackfaces = !shadowPass && wounds && std::any_of(visible.begin(),visible.end(),
                [](const auto& instance) { return instance.woundBase > 0; });
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
            GLint triangleOffset = closure ? static_cast<GLint>(batch.woundSurfaces[variant].triangleCount) : 0;
            for (std::size_t p = 0; p < geometry.model.meshes.size(); ++p) {
                const auto& mesh = geometry.model.meshes[p];
                glUniform1i(zombieTriangleOffsetLocation_, triangleOffset);
                triangleOffset += mesh.count/3;
                applyMaterial(geometry.model, mesh.material, false);
                glUniform1i(zombieCullBackfacesLocation_, torsoBackfaces && !material(geometry.model,mesh.material).doubleSided);
                // Keep torso backs visible; the fragment shader preserves culling
                // for every other triangle and for unwounded instances.
                if (torsoBackfaces) glDisable(GL_CULL_FACE);
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
}

void Renderer::releaseZombies() noexcept {
    for (auto& batch : zombieBatches_) {
        glDeleteBuffers(1, &batch.woundBuffer);
        glDeleteTextures(1, &batch.woundTexture);
        const auto surfaceLevels = 1+batch.lods.size()+batch.severed.size();
        for (std::size_t level = 0; level < surfaceLevels+batch.closures.size(); ++level) {
            auto& geometry = level >= surfaceLevels ? batch.closures[level-surfaceLevels] :
                level == 0 ? static_cast<ZombieGeometry&>(batch) : level <= batch.lods.size() ?
                batch.lods[level-1] : batch.severed[level-batch.lods.size()-1];
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
