#pragma once

#include <GL/glcorearb.h>
#include <filesystem>
#include <vector>

#include "camera.hpp"
#include "model.hpp"

namespace viewer {

class Renderer {
public:
    Renderer(const Model& model, const std::filesystem::path& shaderDirectory);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    void resize(int width, int height);
    void render(const Camera& camera);
    void present(int width, int height);
    void writeScreenshot(const std::filesystem::path& path) const;
    static void checkErrors(const char* stage);

private:
    struct MeshGPU {
        GLuint vao = 0, vbo = 0, ebo = 0;
        GLsizei count = 0;
        int material = -1;
        glm::vec3 center{0};
    };
    struct DrawGPU {
        std::size_t primitive;
        glm::mat4 transform;
        glm::mat3 normalMatrix;
        glm::vec3 center;
        bool mirrored;
    };
    void release() noexcept;
    void draw(const DrawGPU& draw);
    const Material& material(int index) const;
    GLuint program_ = 0, framebuffer_ = 0, colorBuffer_ = 0, depthBuffer_ = 0;
    int width_ = 0, height_ = 0;
    GLint modelLocation_, viewLocation_, projectionLocation_, normalLocation_;
    GLint colorLocation_, textureLocation_, hasTextureLocation_, unlitLocation_;
    GLint alphaLocation_, cutoffLocation_;
    std::vector<MeshGPU> meshes_;
    std::vector<DrawGPU> opaque_, transparent_;
    std::vector<GLuint> textures_;
    std::vector<Material> materials_;
    Material fallback_;
};

} // namespace viewer
