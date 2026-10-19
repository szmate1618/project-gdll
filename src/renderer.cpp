#include "renderer.hpp"
#include "door_layer.hpp"
#include "rain_renderer.hpp"
#include "shader_program.hpp"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <numeric>
#include <sstream>
#include <stdexcept>

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace viewer {
namespace {
float fenceAnisotropy() {
    GLint count = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    for (GLint index = 0; index < count; ++index) {
        const auto* name = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, index));
        if (std::string(name) == "GL_EXT_texture_filter_anisotropic" ||
            std::string(name) == "GL_ARB_texture_filter_anisotropic") {
            GLfloat maximum = 1;
            glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &maximum);
            return std::min(4.0f, maximum);
        }
    }
    return 1;
}
} // namespace

Renderer::Renderer(const Model& model, const std::filesystem::path& shaderDirectory,
                   const TreeLayer* trees, const ZombieLayer* zombies, const ForestLayer* forests)
    : shaderDirectory_(shaderDirectory), sceneVisibility_(model) {
    try {
        program_ = createShaderProgram(shaderDirectory / "basic.vert", shaderDirectory / "basic.frag");
        modelLocation_ = glGetUniformLocation(program_, "uModel");
        viewLocation_ = glGetUniformLocation(program_, "uView");
        projectionLocation_ = glGetUniformLocation(program_, "uProjection");
        normalLocation_ = glGetUniformLocation(program_, "uNormalMatrix");
        colorLocation_ = glGetUniformLocation(program_, "uBaseColor");
        textureLocation_ = glGetUniformLocation(program_, "uBaseTexture");
        hasTextureLocation_ = glGetUniformLocation(program_, "uHasTexture");
        unlitLocation_ = glGetUniformLocation(program_, "uUnlit");
        alphaLocation_ = glGetUniformLocation(program_, "uAlphaMode");
        cutoffLocation_ = glGetUniformLocation(program_, "uAlphaCutoff");
        minifiedCutoffLocation_ = glGetUniformLocation(program_, "uMinifiedAlphaCutoff");
        instancedLocation_ = glGetUniformLocation(program_, "uInstanced");
        treeCanopyLocation_ = glGetUniformLocation(program_, "uTreeCanopyShading");
        treeBillboardModeLocation_ = glGetUniformLocation(program_, "uTreeBillboardMode");
        treeBillboardPivotLocation_ = glGetUniformLocation(program_, "uTreeBillboardPivot");
        animatedLocation_ = glGetUniformLocation(program_, "uAnimated");
        animationLocation_ = glGetUniformLocation(program_, "uAnimation");
        animationFrameLocation_ = glGetUniformLocation(program_, "uAnimationFrame");
        animationCountLocation_ = glGetUniformLocation(program_, "uAnimationCount");
        animationVerticesLocation_ = glGetUniformLocation(program_, "uAnimationVertices");
        skinningLocation_ = glGetUniformLocation(program_, "uSkinningEnabled");
        skinningBonesLocation_ = glGetUniformLocation(program_, "uSkinningBones");
        skinningBoneCountLocation_ = glGetUniformLocation(program_, "uSkinningBoneCount");
        shadowPassLocation_ = glGetUniformLocation(program_, "uShadowPass");
        shadowsLocation_ = glGetUniformLocation(program_, "uShadows");
        shadowMapLocation_ = glGetUniformLocation(program_, "uShadowMap");
        sunMatrixLocation_ = glGetUniformLocation(program_, "uSunMatrix");
        eyeLocation_ = glGetUniformLocation(program_, "uEye");
        forestVisibilityLocation_ = glGetUniformLocation(program_, "uForestVisibility");
        sunDirectionLocation_ = glGetUniformLocation(program_, "uSunDirection");
        sunColorLocation_ = glGetUniformLocation(program_, "uSunColor");
        skyAmbientLocation_ = glGetUniformLocation(program_, "uSkyAmbient");
        groundAmbientLocation_ = glGetUniformLocation(program_, "uGroundAmbient");
        shadowDistanceLocation_ = glGetUniformLocation(program_, "uShadowDistance");
        shadowFadeLocation_ = glGetUniformLocation(program_, "uShadowFadeStart");
        hazeColorLocation_ = glGetUniformLocation(program_, "uHazeColor");
        hazeDensityLocation_ = glGetUniformLocation(program_, "uHazeDensity");
        hazeStartLocation_ = glGetUniformLocation(program_, "uHazeStart");
        groundFogLocation_ = glGetUniformLocation(program_, "uGroundFog");
        nightBlendLocation_ = glGetUniformLocation(program_, "uNightBlend");
        unlitLightLocation_ = glGetUniformLocation(program_, "uUnlitLight");
        flashlightPositionLocation_ = glGetUniformLocation(program_, "uFlashlightPosition");
        flashlightShadowsLocation_ = glGetUniformLocation(program_, "uFlashlightShadows");
        flashlightShadowMapLocation_ = glGetUniformLocation(program_, "uFlashlightShadowMap");
        flashlightMatrixLocation_ = glGetUniformLocation(program_, "uFlashlightMatrix");
        flashlightShadowDistancesLocation_ = glGetUniformLocation(program_, "uFlashlightShadowDistances");
        flashlightDirectionLocation_ = glGetUniformLocation(program_, "uFlashlightDirection");
        flashlightColorLocation_ = glGetUniformLocation(program_, "uFlashlightColor");
        flashlightConeLocation_ = glGetUniformLocation(program_, "uFlashlightCone");
        muzzleFlashPositionLocation_ = glGetUniformLocation(program_, "uMuzzleFlashPosition");
        muzzleFlashColorLocation_ = glGetUniformLocation(program_, "uMuzzleFlashColor");
        muzzleFlashRangeLocation_ = glGetUniformLocation(program_, "uMuzzleFlashRange");
        explosionCountLocation_ = glGetUniformLocation(program_, "uExplosionCount");
        for (std::size_t i = 0; i < maxExplosionLights; ++i) {
            const auto index = "[" + std::to_string(i) + "]";
            explosionPositionLocations_[i] = glGetUniformLocation(program_, ("uExplosionPositions" + index).c_str());
            explosionColorLocations_[i] = glGetUniformLocation(program_, ("uExplosionColors" + index).c_str());
            explosionRangeLocations_[i] = glGetUniformLocation(program_, ("uExplosionRanges" + index).c_str());
        }
        fallback_.baseColor = glm::vec4(0.65f, 0.68f, 0.72f, 1.0f);
        uploadModel(model, model_);
        uploadRoofBatches(model);
        uploadWallBatches(model);
        for (const auto& source : model.draws) {
            const auto& mesh = model_.meshes.at(source.primitive);
            DrawGPU draw{source.primitive, source.transform, glm::inverseTranspose(glm::mat3(source.transform)),
                         glm::vec3(source.transform * glm::vec4(mesh.center, 1.0f)), glm::determinant(glm::mat3(source.transform)) < 0};
            draw.buildingDetail = source.detailDistance > 0.0f && !source.lowDetail;
            sceneDraws_.push_back(draw);
        }
        visibleScene_.reserve(sceneDraws_.size());
        opaqueScene_.reserve(sceneDraws_.size());
        transparentScene_.reserve(sceneDraws_.size());
        sceneStats_.total = sceneDraws_.size();
        std::vector<VisibilityBounds> treeBounds;
        if (trees) treeBounds = uploadTrees(*trees, forests);
        if (forests) uploadForests(*forests);
        if (trees || forests) {
            const std::vector<TreeInstance> empty;
            treeVisibility_ = ForestTreeVisibility(trees ? trees->instances : empty, forestPatches_, treeBounds);
            treeAuthoredVisibility_ = ForestTreeVisibility(trees ? trees->instances : empty, forestPatches_);
        }
        if (zombies) uploadZombies(*zombies);
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glGenFramebuffers(1, &framebuffer_);
        glGenTextures(1, &colorBuffer_);
        glGenRenderbuffers(1, &depthBuffer_);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glDisable(GL_FRAMEBUFFER_SRGB);
        checkErrors("model upload");
    } catch (...) {
        release();
        throw;
    }
}

void Renderer::uploadModel(const Model& model, ModelGPU& destination) {
    destination.materials = model.materials;
    GLint maximumTexture = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximumTexture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    destination.textures.resize(model.textures.size());
    std::vector<bool> fenceTextures(model.textures.size(), false);
    for (auto& material : destination.materials) {
        if (material.minifiedAlphaCutoff <= 0 || material.baseColorTexture < 0 ||
            static_cast<std::size_t>(material.baseColorTexture) >= fenceTextures.size()) continue;
        const auto textureIndex = static_cast<std::size_t>(material.baseColorTexture);
        fenceTextures[textureIndex] = true;
        const int imageIndex = model.textures[textureIndex].image;
        if (imageIndex < 0 || static_cast<std::size_t>(imageIndex) >= model.images.size()) continue;
        const auto& rgba = model.images[static_cast<std::size_t>(imageIndex)].rgba;
        if (rgba.empty()) continue;
        double alphaSum = 0;
        for (std::size_t pixel = 3; pixel < rgba.size(); pixel += 4) alphaSum += rgba[pixel];
        const float averageOpacity = static_cast<float>(alphaSum / (255.0 * (rgba.size() / 4))) * material.baseColor.a;
        // Tightly spaced posts make denser masks. Keep their distant floor
        // above the averaged opacity as well, without raising the source cutoff.
        material.minifiedAlphaCutoff = std::min(material.alphaCutoff,
            std::max(material.minifiedAlphaCutoff, averageOpacity + .05f));
    }
    const float anisotropy = std::any_of(fenceTextures.begin(), fenceTextures.end(),
                                       [](bool fence) { return fence; }) ? fenceAnisotropy() : 1;
    for (std::size_t i = 0; i < model.textures.size(); ++i) {
        const auto& texture = model.textures[i];
        if (texture.image < 0 || static_cast<std::size_t>(texture.image) >= model.images.size()) continue;
        const auto& image = model.images[static_cast<std::size_t>(texture.image)];
        if (image.rgba.empty()) continue;
        if (image.width > maximumTexture || image.height > maximumTexture)
            throw std::runtime_error("Model texture exceeds this GPU's maximum texture size");
        glGenTextures(1, &destination.textures[i]);
        glBindTexture(GL_TEXTURE_2D, destination.textures[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, image.width, image.height,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, image.rgba.data());
        // glTF UVs have their origin at the image's first row. Do not flip.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, texture.minFilter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, texture.magFilter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, texture.wrapS);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, texture.wrapT);
        if (fenceTextures[i] && anisotropy > 1)
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY, anisotropy);
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    uploadMeshes(model.primitives, destination);
}

void Renderer::uploadMeshes(const std::vector<Primitive>& source, ModelGPU& destination) {
    destination.meshes.resize(source.size());
    for (std::size_t i = 0; i < source.size(); ++i) {
        auto& mesh = destination.meshes[i];
        const auto& input = source[i];
        if (input.indices.size() > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max()))
            throw std::runtime_error("Primitive has too many triangle indices for an OpenGL draw call");
        mesh.count = static_cast<GLsizei>(input.indices.size());
        mesh.material = input.material;
        glm::vec3 lo(std::numeric_limits<float>::max()), hi(std::numeric_limits<float>::lowest());
        for (const auto& vertex : input.vertices) { lo = glm::min(lo, vertex.position); hi = glm::max(hi, vertex.position); }
        mesh.center = (lo + hi) * 0.5f;
        glGenVertexArrays(1, &mesh.vao);
        glGenBuffers(1, &mesh.vbo);
        glGenBuffers(1, &mesh.ebo);
        glBindVertexArray(mesh.vao);
        glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(input.vertices.size() * sizeof(Vertex)), input.vertices.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(input.indices.size() * sizeof(std::uint32_t)), input.indices.data(), GL_STATIC_DRAW);
        const std::size_t offsets[] = {offsetof(Vertex, position), offsetof(Vertex, normal), offsetof(Vertex, uv), offsetof(Vertex, color)};
        const GLint sizes[] = {3, 3, 2, 4};
        for (GLuint attribute = 0; attribute < 4; ++attribute) {
            glEnableVertexAttribArray(attribute);
            glVertexAttribPointer(attribute, sizes[attribute], GL_FLOAT, GL_FALSE,
                                  sizeof(Vertex), reinterpret_cast<const void*>(offsets[attribute]));
        }
    }
}

Renderer::~Renderer() { release(); }

void Renderer::releaseModel(ModelGPU& model) noexcept {
    for (const auto& mesh : model.meshes) {
        glDeleteVertexArrays(1, &mesh.vao);
        glDeleteBuffers(1, &mesh.vbo);
        glDeleteBuffers(1, &mesh.ebo);
    }
    for (const GLuint texture : model.textures) glDeleteTextures(1, &texture);
}

void Renderer::release() noexcept {
    rain_.reset();
    releaseZombies();
    releaseModel(model_);
    releaseModel(boxModel_);
    releaseModel(grenadeModel_);
    if (boxInstanceBuffer_) glDeleteBuffers(1, &boxInstanceBuffer_);
    boxInstanceBuffer_ = 0;
    releaseModel(roofModel_);
    releaseModel(wallModel_);
    releaseModel(forestModel_);
    for (auto& batch : treeBatches_) {
        releaseModel(batch.model);
        glDeleteBuffers(1, &batch.instanceBuffer);
    }
    glDeleteFramebuffers(1, &framebuffer_);
    glDeleteTextures(1, &colorBuffer_);
    glDeleteRenderbuffers(1, &depthBuffer_);
    if (program_) glDeleteProgram(program_);
}

const Material& Renderer::material(const ModelGPU& model, int index) const {
    return index >= 0 && static_cast<std::size_t>(index) < model.materials.size()
        ? model.materials[static_cast<std::size_t>(index)] : fallback_;
}

void Renderer::updateDoors(const DoorLayer& doors) {
    for (const auto& pose : doors.poses()) {
        auto& draw = sceneDraws_.at(pose.draw);
        draw.transform = pose.transform;
        draw.normalMatrix = glm::inverseTranspose(glm::mat3(pose.transform));
        draw.center = glm::vec3(pose.transform * glm::vec4(model_.meshes.at(draw.primitive).center, 1));
        draw.mirrored = glm::determinant(glm::mat3(pose.transform)) < 0;
    }
}

void Renderer::resize(int width, int height) {
    if (width <= 0 || height <= 0 || (width == width_ && height == height_)) return;
    width_ = width;
    height_ = height;
    glBindTexture(GL_TEXTURE_2D, colorBuffer_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindRenderbuffer(GL_RENDERBUFFER, depthBuffer_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorBuffer_, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthBuffer_);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("Cannot create the render framebuffer at " + std::to_string(width) + "x" + std::to_string(height));
    checkErrors("framebuffer resize");
}

void Renderer::applyMaterial(const ModelGPU& model, int index, bool mirrored) {
    const auto& mat = material(model, index);
    const int texture = mat.baseColorTexture;
    const bool textured = texture >= 0 && static_cast<std::size_t>(texture) < model.textures.size() && model.textures[static_cast<std::size_t>(texture)] != 0;
    if (mat.doubleSided) glDisable(GL_CULL_FACE); else glEnable(GL_CULL_FACE);
    glFrontFace(mirrored ? GL_CW : GL_CCW);
    glUniform4fv(colorLocation_, 1, glm::value_ptr(mat.baseColor));
    glUniform1i(hasTextureLocation_, textured);
    glUniform1i(unlitLocation_, mat.unlit);
    glUniform1i(alphaLocation_, mat.alphaMode == "MASK" ? 1 : mat.alphaMode == "BLEND" ? 2 : 0);
    glUniform1f(cutoffLocation_, mat.alphaCutoff);
    glUniform1f(minifiedCutoffLocation_, mat.minifiedAlphaCutoff);
    glBindTexture(GL_TEXTURE_2D, textured ? model.textures[static_cast<std::size_t>(texture)] : 0);
}

void Renderer::draw(const DrawGPU& draw) {
    const auto& mesh = model_.meshes[draw.primitive];
    applyMaterial(model_, mesh.material, draw.mirrored);
    glUniformMatrix4fv(modelLocation_, 1, GL_FALSE, glm::value_ptr(draw.transform));
    glUniformMatrix3fv(normalLocation_, 1, GL_FALSE, glm::value_ptr(draw.normalMatrix));
    glBindVertexArray(mesh.vao);
    glDrawElements(GL_TRIANGLES, mesh.count, GL_UNSIGNED_INT, nullptr);
}

void Renderer::gatherVisibleSceneDraws(const glm::mat4& projectionView, const glm::vec3& eye,
                                      const glm::mat4& projection) {
    if (sceneCulling_ || forestViewRange_)
        sceneVisibility_.query(projectionView, visibleScene_, forestVisibilityRange(), sceneCulling_);
    else {
        visibleScene_.resize(sceneDraws_.size());
        std::iota(visibleScene_.begin(), visibleScene_.end(), std::size_t{0});
    }
    sceneVisibility_.filterDetails(eye, projection, visibleScene_);

    opaqueScene_.clear();
    transparentScene_.clear();
    for (const auto index : visibleScene_) {
        const auto& draw = sceneDraws_[index];
        const auto& mesh = model_.meshes[draw.primitive];
        (material(model_, mesh.material).alphaMode == "BLEND" ? transparentScene_ : opaqueScene_).push_back(index);
    }
    // Preserve source order for opaque surfaces, including coplanar geometry.
    std::sort(opaqueScene_.begin(), opaqueScene_.end());
    std::sort(transparentScene_.begin(), transparentScene_.end(), [this, &eye](const auto a, const auto b) {
        const auto da = sceneDraws_[a].center - eye;
        const auto db = sceneDraws_[b].center - eye;
        const auto distanceA = glm::dot(da, da);
        const auto distanceB = glm::dot(db, db);
        if (distanceA != distanceB) return distanceA > distanceB;
        return a < b;
    });
    sceneStats_.visible = visibleScene_.size();
}

void Renderer::setRain(bool enabled) {
    if (enabled && !rain_) rain_ = std::make_unique<RainRenderer>(shaderDirectory_);
    rainEnabled_ = enabled;
}

void Renderer::render(const Camera& camera, double animationSeconds, const std::function<void()>& drawOpaque) {
    sceneStats_ = {sceneDraws_.size(), 0, 0};
    if (width_ <= 0 || height_ <= 0) return;
    treeViewEye_ = camera.position();
    treeBillboardsForFrame_ = camera.viewMode() == Camera::ViewMode::original && treeBillboardDistance_ > 0;
    glUseProgram(program_);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(textureLocation_, 0);
    forestViewRange_ = camera.viewMode() == Camera::ViewMode::original
        ? oldForestVisibility_.rangeAt(camera.position()) : std::nullopt;
    if (forestViewRange_)
        glUniform3f(forestVisibilityLocation_, forestViewRange_->center.x,
                    forestViewRange_->center.y, forestViewRange_->radius);
    else glUniform3f(forestVisibilityLocation_, 0, 0, 0);
    // Assign all active sampler types distinct units in both passes, including
    // empty scenes. Units 3/4 belong to the sun/flashlight depth comparisons.
    glUniform1i(animationLocation_, 1);
    glUniform1i(skinningBonesLocation_, 2);
    glUniform1i(shadowMapLocation_, 3);
    glUniform1i(flashlightShadowMapLocation_, 4);
    SunShadowView shadowView;
    const auto lighting = lightingForFrame();
    const auto view = camera.view();
    const auto projection = camera.projection(static_cast<float>(width_) / static_cast<float>(height_));
    prepareForestView(camera);
    const bool sunlight = glm::any(glm::greaterThan(lighting.sun.color, glm::vec3(0)));
    if (sunlight) shadowView = sunShadowView(camera.position(), lighting.sun.direction);
    const bool castShadows = sunlight && shadows_ && shadowPassLocation_ >= 0;
    const auto flashlight = flashlightView(camera, nightLighting_);
    const bool flashlightCasting = flashlightEnabled_ && lighting.nightBlend > 0 && shadows_ && flashlightShadows_ &&
        shadowPassLocation_ >= 0 && flashlightShadowsLocation_ >= 0;
    const Frustum flashlightFrustum(flashlight.projectionView());
    const Frustum lightFrustum(shadowView.projectionView());
    prepareZombieDraws(Frustum(projection * view), castShadows ? &lightFrustum : nullptr, camera, projection, animationSeconds, flashlightCasting ? &flashlightFrustum : nullptr);
    if (castShadows) drawSunShadows(shadowView, animationSeconds);
    if (flashlightCasting) drawFlashlightShadows(flashlight, animationSeconds);
    applyLighting(camera, shadowView, lighting, flashlight, flashlightCasting);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glViewport(0, 0, width_, height_);
    auto atmosphere = atmosphere_;
    atmosphere.color = lighting.fogColor;
    backgroundSrgb_ = atmosphere.backgroundSrgb();
    glClearColor(backgroundSrgb_.r, backgroundSrgb_.g, backgroundSrgb_.b, 1.0f);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(program_);
    glUniformMatrix4fv(viewLocation_, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(projectionLocation_, 1, GL_FALSE, glm::value_ptr(projection));
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(textureLocation_, 0);
    glDisable(GL_BLEND);
    glUniform1i(instancedLocation_, GL_FALSE);
    glUniform1i(animatedLocation_, GL_FALSE);
    gatherVisibleSceneDraws(projection * view, camera.position(), projection);
    beginTownPass(camera.position(), projection);
    for (const auto index : opaqueScene_) {
        if (drawTownSource(index)) ++sceneStats_.drawCalls;
    }
    drawTrees(projection * view);
    drawForests(projection * view);
    drawZombies(animationSeconds);
    drawBoxes(projection * view);
    drawGrenades(projection * view);
    if (drawOpaque) drawOpaque();
    if (!transparentScene_.empty()) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        for (const auto index : transparentScene_) {
            draw(sceneDraws_[index]);
            ++sceneStats_.drawCalls;
        }
        glDepthMask(GL_TRUE);
    }
    if (rainEnabled_) {
        auto rainLighting = nightLighting_;
        if (!flashlightEnabled_) rainLighting.flashlightColor = glm::vec3(0);
        rain_->draw(camera, static_cast<float>(width_) / static_cast<float>(height_),
                    animationSeconds, lighting.nightBlend, rainLighting);
    }
    glBindVertexArray(0);
}

void Renderer::present(int width, int height) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, width_, height_, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
}

void Renderer::writeScreenshot(const std::filesystem::path& path) const {
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 3);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer_);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width_, height_, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    checkErrors("screenshot readback");
    std::ofstream output(path, std::ios::binary);
    if (!output) throw std::runtime_error("Cannot write screenshot: " + path.string());
    output << "P6\n" << width_ << ' ' << height_ << "\n255\n";
    const auto stride = static_cast<std::size_t>(width_) * 3;
    for (int row = height_ - 1; row >= 0; --row)
        output.write(reinterpret_cast<const char*>(pixels.data() + static_cast<std::size_t>(row) * stride), static_cast<std::streamsize>(stride));
    if (!output) throw std::runtime_error("Error writing screenshot: " + path.string());
    // A deterministic readback confirms a draw produced pixels beyond the sky.
    std::size_t foreground = 0;
    const auto background = glm::clamp(backgroundSrgb_, glm::vec3(0), glm::vec3(1));
    for (std::size_t p = 0; p < pixels.size(); p += 3)
        if (std::abs(static_cast<int>(pixels[p]) - static_cast<int>(std::lround(background.r * 255))) > 2 ||
            std::abs(static_cast<int>(pixels[p + 1]) - static_cast<int>(std::lround(background.g * 255))) > 2 ||
            std::abs(static_cast<int>(pixels[p + 2]) - static_cast<int>(std::lround(background.b * 255))) > 2) ++foreground;
    std::cout << "Screenshot: " << path << " (" << width_ << 'x' << height_
              << ", " << foreground << " foreground pixels)\n";
    if (!foreground) throw std::runtime_error("Render readback contains only background; model may be out of view");
}

void Renderer::checkErrors(const char* stage) {
    std::ostringstream errors;
    for (GLenum error = glGetError(); error != GL_NO_ERROR; error = glGetError())
        errors << " 0x" << std::hex << error;
    if (!errors.str().empty()) throw std::runtime_error(std::string("OpenGL error during ") + stage + ":" + errors.str());
}
} // namespace viewer
