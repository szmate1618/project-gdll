#include "scene_depth.hpp"
#include "bird_renderer.hpp"
#include "bird_flock.hpp"
#include "shader_program.hpp"
#include "visibility.hpp"
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <glm/gtc/type_ptr.hpp>

namespace viewer {
namespace {
// Element-buffer binding belongs to each VAO and is restored with it.
struct State {
    GLint program = 0, vao = 0, buffer = 0, active = 0, animation = 0, atlas = 0, sampler0 = 0, sampler1 = 0;
    GLboolean write = GL_TRUE;
    GLboolean depth = glIsEnabled(GL_DEPTH_TEST), blend = glIsEnabled(GL_BLEND), cull = glIsEnabled(GL_CULL_FACE);
    State() {
        glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &buffer);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &write);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_BUFFER, &animation);
        glGetIntegerv(GL_SAMPLER_BINDING, &sampler0);
        glActiveTexture(GL_TEXTURE1);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &atlas);
        glGetIntegerv(GL_SAMPLER_BINDING, &sampler1);
    }
    ~State() {
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_BUFFER, static_cast<GLuint>(animation));
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(atlas));
        glBindSampler(0, static_cast<GLuint>(sampler0)); glBindSampler(1, static_cast<GLuint>(sampler1));
        glActiveTexture(static_cast<GLenum>(active));
        glBindVertexArray(static_cast<GLuint>(vao));
        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(buffer));
        glUseProgram(static_cast<GLuint>(program));
        glDepthMask(write);
        if (depth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
        if (blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
        if (cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    }
};
}

BirdRenderer::BirdRenderer(const std::filesystem::path& shaderDirectory, const std::filesystem::path& assetPath) {
    const auto asset = loadBirdAssets(assetPath);
    radius_ = asset.radius; duration_ = asset.duration; frameCount_ = asset.frameCount;
    const State restore;
    try {
        program_ = createShaderProgram(shaderDirectory / "bird.vert", shaderDirectory / "bird.frag");
        projectionViewLocation_ = glGetUniformLocation(program_, "uProjectionView");
        frameLocation_ = glGetUniformLocation(program_, "uFrame");
        frameCountLocation_ = glGetUniformLocation(program_, "uFrameCount");
        vertexCountLocation_ = glGetUniformLocation(program_, "uVertexCount");
        dayNightTintLocation_ = glGetUniformLocation(program_, "uDayNightTint");
        glUseProgram(program_);
        glUniform1i(glGetUniformLocation(program_, "uAnimation"), 0);
        glUniform1i(glGetUniformLocation(program_, "uAtlas"), 1);
        glGenTextures(1, &atlas_);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, atlas_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, asset.atlas.rgba.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        GLint maximumTexels = 0;
        glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE, &maximumTexels);
        for (std::size_t level = 0; level < 3; ++level) {
            const auto& mesh = asset.meshes[level]; auto& batch = batches_[level];
            if (mesh.samples.size() * 2 > static_cast<std::size_t>(maximumTexels))
                throw std::runtime_error("Pigeon animation exceeds GPU texture-buffer capacity");
            batch.indexCount = static_cast<GLsizei>(mesh.indices.size());
            batch.vertexCount = static_cast<GLint>(mesh.uv.size());
            glGenVertexArrays(1, &batch.vao); glBindVertexArray(batch.vao);
            glGenBuffers(1, &batch.uv); glBindBuffer(GL_ARRAY_BUFFER, batch.uv);
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh.uv.size() * sizeof(glm::vec2)), mesh.uv.data(), GL_STATIC_DRAW);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(glm::vec2), nullptr);
            glGenBuffers(1, &batch.indices); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, batch.indices);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(std::uint32_t)), mesh.indices.data(), GL_STATIC_DRAW);
            glGenBuffers(1, &batch.instances); glBindBuffer(GL_ARRAY_BUFFER, batch.instances);
            for (GLuint attribute = 3; attribute <= 5; ++attribute) {
                const auto offset = attribute == 3 ? offsetof(FlockBird, position) :
                    attribute == 4 ? offsetof(FlockBird, velocity) : offsetof(FlockBird, phase);
                glEnableVertexAttribArray(attribute);
                glVertexAttribPointer(attribute, attribute == 5 ? 1 : 3, GL_FLOAT, GL_FALSE, sizeof(FlockBird), reinterpret_cast<void*>(offset));
                glVertexAttribDivisor(attribute, 1);
            }
            glGenBuffers(1, &batch.samples); glBindBuffer(GL_ARRAY_BUFFER, batch.samples);
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh.samples.size() * sizeof(BirdAnimationSample)), mesh.samples.data(), GL_STATIC_DRAW);
            glGenTextures(1, &batch.animation);
            glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_BUFFER, batch.animation);
            glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, batch.samples);
        }
    } catch (...) { release(); throw; }
}

BirdRenderer::~BirdRenderer() { release(); }
void BirdRenderer::release() noexcept {
    for (auto& batch : batches_) {
        glDeleteBuffers(1, &batch.uv); glDeleteBuffers(1, &batch.indices);
        glDeleteBuffers(1, &batch.instances); glDeleteBuffers(1, &batch.samples);
        glDeleteTextures(1, &batch.animation); glDeleteVertexArrays(1, &batch.vao);
    }
    glDeleteTextures(1, &atlas_);
    if (program_) glDeleteProgram(program_);
}

BirdDrawStats BirdRenderer::draw(const Camera& camera, float aspect, double seconds,
                                 const std::vector<FlockBird>& birds, const glm::vec3& tint) {
    BirdDrawStats stats;
    if (birds.empty()) return stats;
    const auto projection = camera.projection(aspect);
    const auto projectionView = projection * camera.view();
    const Frustum frustum(projectionView);
    GLint viewport[4]; glGetIntegerv(GL_VIEWPORT, viewport);
    for (auto& list : visible_) list.clear();
    for (const auto& bird : birds) {
        const glm::vec3 extent(radius_);
        if (!frustum.intersects(bird.position - extent, bird.position + extent)) continue;
        const auto level = birdDetailLevel(glm::length(bird.position - camera.position()), radius_, projection[1][1], viewport[3]);
        visible_[level].push_back(bird);
    }
    const State restore;
    glUseProgram(program_);
    const auto gpuProjectionView = sceneProjection(camera, aspect) * camera.view();
    glUniformMatrix4fv(projectionViewLocation_, 1, GL_FALSE, glm::value_ptr(gpuProjectionView));
    // Reduce double time before conversion; all levels share the same cycle.
    const double cycles = seconds / duration_;
    glUniform1f(frameLocation_, static_cast<float>((cycles - std::floor(cycles)) * frameCount_));
    glUniform1i(frameCountLocation_, static_cast<GLint>(frameCount_));
    glUniform3fv(dayNightTintLocation_, 1, glm::value_ptr(tint));
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, atlas_);
    glBindSampler(0, 0); glBindSampler(1, 0);
    glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
    for (std::size_t level = 0; level < 3; ++level) {
        const auto& list = visible_[level];
        if (list.empty()) continue;
        const auto& batch = batches_[level];
        glUniform1i(vertexCountLocation_, batch.vertexCount);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_BUFFER, batch.animation);
        glBindVertexArray(batch.vao); glBindBuffer(GL_ARRAY_BUFFER, batch.instances);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(list.size() * sizeof(FlockBird)), list.data(), GL_STREAM_DRAW);
        glDrawElementsInstanced(GL_TRIANGLES, batch.indexCount, GL_UNSIGNED_INT, nullptr, static_cast<GLsizei>(list.size()));
        stats.instances[level] = list.size(); stats.triangles += list.size() * static_cast<std::size_t>(batch.indexCount / 3);
    }
    return stats;
}
}
