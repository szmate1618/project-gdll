#include "renderer.hpp"

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace viewer {
namespace {
GLuint compileShader(GLenum type, const std::filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream) throw std::runtime_error("Cannot open shader: " + path.string());
    std::ostringstream contents;
    contents << stream.rdbuf();
    const std::string text = contents.str();
    const char* source = text.c_str();
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint success = 0, size = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &size);
    std::string log(static_cast<std::size_t>(std::max(size, 1)), '\0');
    if (size > 1) {
        glGetShaderInfoLog(shader, size, nullptr, log.data());
        std::cerr << "Shader " << path << ":\n" << log << '\n';
    }
    if (!success) {
        glDeleteShader(shader);
        throw std::runtime_error("Shader compilation failed: " + path.string() + "\n" + log);
    }
    return shader;
}

GLuint createProgram(const std::filesystem::path& directory) {
    const GLuint vertex = compileShader(GL_VERTEX_SHADER, directory / "basic.vert");
    GLuint fragment = 0, program = 0;
    try {
        fragment = compileShader(GL_FRAGMENT_SHADER, directory / "basic.frag");
        program = glCreateProgram();
        glAttachShader(program, vertex);
        glAttachShader(program, fragment);
        glLinkProgram(program);
        GLint linked = 0;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if (!linked) {
            GLint length = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
            std::string log(static_cast<std::size_t>(std::max(length, 1)), '\0');
            glGetProgramInfoLog(program, length, nullptr, log.data());
            throw std::runtime_error("Shader linking failed (" + directory.string() + "): " + log);
        }
    } catch (...) {
        if (program) glDeleteProgram(program);
        if (fragment) glDeleteShader(fragment);
        glDeleteShader(vertex);
        throw;
    }
    glDetachShader(program, vertex);
    glDetachShader(program, fragment);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    return program;
}
} // namespace

Renderer::Renderer(const Model& model, const std::filesystem::path& shaderDirectory)
    : materials_(model.materials) {
    try {
        program_ = createProgram(shaderDirectory);
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
        fallback_.baseColor = glm::vec4(0.65f, 0.68f, 0.72f, 1.0f);
        GLint maximumTexture = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximumTexture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        textures_.resize(model.textures.size());
        for (std::size_t i = 0; i < model.textures.size(); ++i) {
            const auto& texture = model.textures[i];
            if (texture.image < 0 || static_cast<std::size_t>(texture.image) >= model.images.size()) continue;
            const auto& image = model.images[static_cast<std::size_t>(texture.image)];
            if (image.rgba.empty()) continue;
            if (image.width > maximumTexture || image.height > maximumTexture)
                throw std::runtime_error("Model texture exceeds this GPU's maximum texture size");
            glGenTextures(1, &textures_[i]);
            glBindTexture(GL_TEXTURE_2D, textures_[i]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, image.width, image.height,
                         0, GL_RGBA, GL_UNSIGNED_BYTE, image.rgba.data());
            // glTF UVs have their origin at the image's first row. Do not flip.
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, texture.minFilter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, texture.magFilter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, texture.wrapS);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, texture.wrapT);
            glGenerateMipmap(GL_TEXTURE_2D);
        }
        meshes_.resize(model.primitives.size());
        for (std::size_t i = 0; i < model.primitives.size(); ++i) {
            auto& mesh = meshes_[i];
            const auto& input = model.primitives[i];
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
        for (const auto& source : model.draws) {
            const auto& mesh = meshes_.at(source.primitive);
            DrawGPU draw{source.primitive, source.transform, glm::inverseTranspose(glm::mat3(source.transform)),
                         glm::vec3(source.transform * glm::vec4(mesh.center, 1.0f)), glm::determinant(glm::mat3(source.transform)) < 0};
            (material(mesh.material).alphaMode == "BLEND" ? transparent_ : opaque_).push_back(draw);
        }
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

Renderer::~Renderer() { release(); }

void Renderer::release() noexcept {
    for (const auto& mesh : meshes_) {
        glDeleteVertexArrays(1, &mesh.vao);
        glDeleteBuffers(1, &mesh.vbo);
        glDeleteBuffers(1, &mesh.ebo);
    }
    for (const GLuint texture : textures_) glDeleteTextures(1, &texture);
    glDeleteFramebuffers(1, &framebuffer_);
    glDeleteTextures(1, &colorBuffer_);
    glDeleteRenderbuffers(1, &depthBuffer_);
    if (program_) glDeleteProgram(program_);
}

const Material& Renderer::material(int index) const {
    return index >= 0 && static_cast<std::size_t>(index) < materials_.size()
        ? materials_[static_cast<std::size_t>(index)] : fallback_;
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

void Renderer::draw(const DrawGPU& draw) {
    const auto& mesh = meshes_[draw.primitive];
    const auto& mat = material(mesh.material);
    const int texture = mat.baseColorTexture;
    const bool textured = texture >= 0 && static_cast<std::size_t>(texture) < textures_.size() && textures_[static_cast<std::size_t>(texture)] != 0;
    if (mat.doubleSided) glDisable(GL_CULL_FACE); else glEnable(GL_CULL_FACE);
    glFrontFace(draw.mirrored ? GL_CW : GL_CCW);
    glUniformMatrix4fv(modelLocation_, 1, GL_FALSE, glm::value_ptr(draw.transform));
    glUniformMatrix3fv(normalLocation_, 1, GL_FALSE, glm::value_ptr(draw.normalMatrix));
    glUniform4fv(colorLocation_, 1, glm::value_ptr(mat.baseColor));
    glUniform1i(hasTextureLocation_, textured);
    glUniform1i(unlitLocation_, mat.unlit);
    glUniform1i(alphaLocation_, mat.alphaMode == "MASK" ? 1 : mat.alphaMode == "BLEND" ? 2 : 0);
    glUniform1f(cutoffLocation_, mat.alphaCutoff);
    glBindTexture(GL_TEXTURE_2D, textured ? textures_[static_cast<std::size_t>(texture)] : 0);
    glBindVertexArray(mesh.vao);
    glDrawElements(GL_TRIANGLES, mesh.count, GL_UNSIGNED_INT, nullptr);
}

void Renderer::render(const Camera& camera) {
    if (width_ <= 0 || height_ <= 0) return;
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glViewport(0, 0, width_, height_);
    glClearColor(0.10f, 0.14f, 0.20f, 1.0f);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(program_);
    const auto view = camera.view();
    const auto projection = camera.projection(static_cast<float>(width_) / static_cast<float>(height_));
    glUniformMatrix4fv(viewLocation_, 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(projectionLocation_, 1, GL_FALSE, glm::value_ptr(projection));
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(textureLocation_, 0);
    glDisable(GL_BLEND);
    for (const auto& item : opaque_) draw(item);
    if (!transparent_.empty()) {
        const auto eye = camera.position();
        std::sort(transparent_.begin(), transparent_.end(), [&eye](const auto& a, const auto& b) {
            const auto da = a.center - eye, db = b.center - eye;
            return glm::dot(da, da) > glm::dot(db, db);
        });
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        for (const auto& item : transparent_) draw(item);
        glDepthMask(GL_TRUE);
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
    for (std::size_t p = 0; p < pixels.size(); p += 3)
        if (std::abs(static_cast<int>(pixels[p]) - 26) > 2 ||
            std::abs(static_cast<int>(pixels[p + 1]) - 36) > 2 ||
            std::abs(static_cast<int>(pixels[p + 2]) - 51) > 2) ++foreground;
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
