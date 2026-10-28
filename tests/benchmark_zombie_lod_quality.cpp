// Opt-in image-quality experiment, not a frame-time benchmark. Uses the normal
// loader, VAT vertex shader, material sampling and sunlight fragment shader.
#include "animated_model.hpp"
#include "file_fingerprint.hpp"
#include "shader_program.hpp"
#include "sun_lighting.hpp"

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <json.hpp>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Json = nlohmann::json;
constexpr int sceneWidth = 1280, sceneHeight = 720, cropSize = 192, maskScale = 4;
constexpr int cropX = (sceneWidth - cropSize) / 2, cropY = (sceneHeight - cropSize) / 2 + 40;
constexpr int angles = 8, poses = 8;
constexpr std::array<float, 2> distances{12, 35};

void checkGL(const char* where) {
    if (const auto error = glGetError(); error != GL_NO_ERROR)
        throw std::runtime_error(std::string(where) + ": OpenGL error " + std::to_string(error));
}

struct Window {
    GLFWwindow* handle = nullptr;
    Window() {
        if (!glfwInit()) throw std::runtime_error("Cannot initialize GLFW; display/GPU access is required");
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        handle = glfwCreateWindow(64, 64, "Zombie LOD quality capture", nullptr, nullptr);
        if (!handle) { glfwTerminate(); throw std::runtime_error("Cannot create OpenGL 3.3 context"); }
        glfwMakeContextCurrent(handle);
        glfwSwapInterval(0);
        viewer::initializeOpenGL();
    }
    ~Window() { glfwDestroyWindow(handle); glfwTerminate(); }
};

struct Target {
    GLuint fbo = 0, color = 0, depth = 0;
    int size;
    explicit Target(int side) : size(side) {
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glGenTextures(1, &color);
        glBindTexture(GL_TEXTURE_2D, color);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
        glGenRenderbuffers(1, &depth);
        glBindRenderbuffer(GL_RENDERBUFFER, depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, size, size);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("Incomplete comparison framebuffer");
    }
    ~Target() {
        glDeleteRenderbuffers(1, &depth); glDeleteTextures(1, &color); glDeleteFramebuffers(1, &fbo);
    }
    void bind() const {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, size, size);
        glClearColor(.12f, .15f, .19f, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }
};

struct Mesh {
    GLuint vao = 0, vertices = 0, indices = 0, animationBuffer = 0, animationTexture = 0;
    GLsizei count = 0;
    GLint vertexCount = 0;
    int material = -1;
};

class Capture {
public:
    Capture(const viewer::AnimatedModel& asset, const std::filesystem::path& shaders)
        : asset_(asset), program_(viewer::createShaderProgram(shaders / "basic.vert", shaders / "basic.frag",
                                                             "#define ZOMBIE_WOUNDS 0")) {
        glUseProgram(program_);
        integer("uInstanced", 1); integer("uAnimated", 1);
        integer("uAnimation", 1); integer("uBaseTexture", 0);
        integer("uSkinningBones", 2); integer("uShadowMap", 3); integer("uFlashlightShadowMap", 4);
        integer("uShadows", 0); integer("uFlashlightShadows", 0);
        integer("uAnimationCount", static_cast<int>(asset.frameCount));
        const viewer::SunLighting light;
        vector("uSunDirection", glm::normalize(light.direction));
        vector("uSunColor", light.color); vector("uSkyAmbient", light.skyAmbient);
        vector("uGroundAmbient", light.groundAmbient); vector("uUnlitLight", glm::vec3(1));
        matrix("uModel", glm::mat4(1));
        // Crop below the reticle to include the feet from the normal eye height.
        // This preserves pixel size/alignment without drawing the full viewport.
        auto crop = glm::mat4(1);
        crop[0][0] = float(sceneWidth) / cropSize;
        crop[1][1] = float(sceneHeight) / cropSize;
        crop[3][0] = float(sceneWidth - 2 * cropX - cropSize) / cropSize;
        crop[3][1] = float(2 * cropY + cropSize - sceneHeight) / cropSize;
        matrix("uProjection", crop * glm::perspective(glm::radians(60.f), float(sceneWidth) / sceneHeight, .1f, 10000.f));
        textures_.resize(asset.model.textures.size());
        glGenTextures(static_cast<GLsizei>(textures_.size()), textures_.data());
        for (std::size_t i = 0; i < textures_.size(); ++i) {
            const auto& texture = asset.model.textures[i];
            const auto& image = asset.model.images.at(static_cast<std::size_t>(texture.image));
            glBindTexture(GL_TEXTURE_2D, textures_[i]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, image.width, image.height, 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, image.rgba.data());
            glGenerateMipmap(GL_TEXTURE_2D);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, texture.minFilter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, texture.magFilter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, texture.wrapS);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, texture.wrapT);
        }
        meshes_.resize(asset.model.primitives.size());
        for (std::size_t p = 0; p < meshes_.size(); ++p) {
            const auto& primitive = asset.model.primitives[p];
            const auto& frames = asset.animation[p].frames;
            auto& mesh = meshes_[p];
            mesh.count = static_cast<GLsizei>(primitive.indices.size());
            mesh.vertexCount = static_cast<GLint>(primitive.vertices.size());
            mesh.material = primitive.material;
            if (frames.size() != primitive.vertices.size() * asset.totalFrameCount())
                throw std::runtime_error("Invalid baked animation frame layout");
            glGenVertexArrays(1, &mesh.vao); glBindVertexArray(mesh.vao);
            glGenBuffers(1, &mesh.vertices); glBindBuffer(GL_ARRAY_BUFFER, mesh.vertices);
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(primitive.vertices.size() * sizeof(viewer::Vertex)),
                         primitive.vertices.data(), GL_STATIC_DRAW);
            const std::array<int, 4> components{3, 3, 2, 4};
            const std::array<std::size_t, 4> offsets{offsetof(viewer::Vertex, position), offsetof(viewer::Vertex, normal),
                                                   offsetof(viewer::Vertex, uv), offsetof(viewer::Vertex, color)};
            for (GLuint a = 0; a < 4; ++a) {
                glEnableVertexAttribArray(a);
                glVertexAttribPointer(a, components[a], GL_FLOAT, GL_FALSE, sizeof(viewer::Vertex),
                                      reinterpret_cast<const void*>(offsets[a]));
            }
            glGenBuffers(1, &mesh.indices); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.indices);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(primitive.indices.size() * sizeof(std::uint32_t)),
                         primitive.indices.data(), GL_STATIC_DRAW);
            glGenBuffers(1, &mesh.animationBuffer); glBindBuffer(GL_TEXTURE_BUFFER, mesh.animationBuffer);
            glBufferData(GL_TEXTURE_BUFFER, static_cast<GLsizeiptr>(frames.size() * sizeof(viewer::AnimationVertex)),
                         frames.data(), GL_STATIC_DRAW);
            glGenTextures(1, &mesh.animationTexture); glBindTexture(GL_TEXTURE_BUFFER, mesh.animationTexture);
            glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, mesh.animationBuffer);
        }
        glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE);
        glDisable(GL_BLEND); glDisable(GL_DITHER); glDisable(GL_FRAMEBUFFER_SRGB);
        glCullFace(GL_BACK); glFrontFace(GL_CCW);
        checkGL("comparison geometry upload");
    }
    ~Capture() {
        for (const auto& mesh : meshes_) {
            glDeleteTextures(1, &mesh.animationTexture); glDeleteBuffers(1, &mesh.animationBuffer);
            glDeleteBuffers(1, &mesh.indices); glDeleteBuffers(1, &mesh.vertices); glDeleteVertexArrays(1, &mesh.vao);
        }
        glDeleteTextures(static_cast<GLsizei>(textures_.size()), textures_.data()); glDeleteProgram(program_);
    }
    void draw(const Target& target, const glm::mat4& transform, float distance,
              const viewer::BakedAnimationClip& clip, float frame, bool silhouette) const {
        target.bind(); glUseProgram(program_);
        const glm::vec3 eye{0, 1.65f, distance};
        vector("uEye", eye);
        matrix("uView", glm::lookAt(eye, eye + glm::vec3(0, 0, -1), {0, 1, 0}));
        integer("uShadowPass", silhouette ? 1 : 0);
        for (GLuint column = 0; column < 4; ++column) glVertexAttrib4fv(4 + column, glm::value_ptr(transform[column]));
        glVertexAttrib4f(8, 0, static_cast<float>(clip.firstFrame), static_cast<float>(clip.frameCount), 0);
        glVertexAttrib1f(9, frame);
        glVertexAttrib2f(10, 0, 1); // Normal shared VAT, one displayed stepped pose.
        for (const auto& mesh : meshes_) {
            const auto& mat = asset_.model.materials.at(static_cast<std::size_t>(mesh.material));
            if (mat.alphaMode == "BLEND") throw std::runtime_error("Unexpected blended crowd material");
            glUniform4fv(location("uBaseColor"), 1, glm::value_ptr(mat.baseColor));
            integer("uUnlit", mat.unlit ? 1 : 0);
            integer("uAlphaMode", mat.alphaMode == "MASK" ? 1 : 0);
            glUniform1f(location("uAlphaCutoff"), mat.alphaCutoff);
            integer("uHasTexture", mat.baseColorTexture >= 0 ? 1 : 0);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, mat.baseColorTexture >= 0 ? textures_.at(static_cast<std::size_t>(mat.baseColorTexture)) : 0);
            glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_BUFFER, mesh.animationTexture);
            integer("uAnimationVertices", mesh.vertexCount);
            if (mat.doubleSided) glDisable(GL_CULL_FACE); else glEnable(GL_CULL_FACE);
            glBindVertexArray(mesh.vao);
            glDrawElementsInstanced(GL_TRIANGLES, mesh.count, GL_UNSIGNED_INT, nullptr, 1);
        }
        checkGL("comparison render");
    }
private:
    GLint location(const char* name) const { return glGetUniformLocation(program_, name); }
    void integer(const char* name, int value) const { glUniform1i(location(name), value); }
    void vector(const char* name, const glm::vec3& value) const { glUniform3fv(location(name), 1, glm::value_ptr(value)); }
    void matrix(const char* name, const glm::mat4& value) const { glUniformMatrix4fv(location(name), 1, GL_FALSE, glm::value_ptr(value)); }
    const viewer::AnimatedModel& asset_;
    GLuint program_;
    std::vector<Mesh> meshes_;
    std::vector<GLuint> textures_;
};

void saveColor(const std::filesystem::path& path, int size) {
    std::vector<unsigned char> pixels(static_cast<std::size_t>(size * size * 3));
    glPixelStorei(GL_PACK_ALIGNMENT, 1); glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(0, 0, size, size, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    // GL readback is bottom-up; the dataset uses top-left image coordinates.
    for (int y = 0; y < size / 2; ++y)
        std::swap_ranges(pixels.begin() + y * size * 3, pixels.begin() + (y + 1) * size * 3,
                         pixels.begin() + (size - 1 - y) * size * 3);
    if (!stbi_write_png(path.string().c_str(), size, size, 3, pixels.data(), size * 3))
        throw std::runtime_error("Cannot save comparison image: " + path.string());
}

void saveMask(const std::filesystem::path& path, int size) {
    std::vector<float> depth(static_cast<std::size_t>(size * size));
    std::vector<unsigned char> mask(depth.size());
    glReadPixels(0, 0, size, size, GL_DEPTH_COMPONENT, GL_FLOAT, depth.data());
    for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x)
        mask[static_cast<std::size_t>(y * size + x)] = depth[static_cast<std::size_t>((size - 1 - y) * size + x)] < 1 ? 255 : 0;
    const auto occupied = std::count(mask.begin(), mask.end(), static_cast<unsigned char>(255));
    if (!occupied) throw std::runtime_error("Capture has no foreground pixels");
    for (int i = 0; i < size; ++i)
        if (mask[static_cast<std::size_t>(i)] || mask[static_cast<std::size_t>((size - 1) * size + i)] ||
            mask[static_cast<std::size_t>(i * size)] || mask[static_cast<std::size_t>(i * size + size - 1)])
            throw std::runtime_error("Character touches capture border; increase common crop size");
    if (!stbi_write_png(path.string().c_str(), size, size, 1, mask.data(), size))
        throw std::runtime_error("Cannot save silhouette: " + path.string());
}

glm::mat4 normalization(const viewer::AnimatedModel& original) {
    const auto low = original.model.boundsMin, high = original.model.boundsMax;
    const float scale = 1.8f / (high.y - low.y);
    const glm::vec3 origin{(low.x + high.x) * .5f, low.y, (low.z + high.z) * .5f};
    return glm::scale(glm::mat4(1), glm::vec3(scale)) * glm::translate(glm::mat4(1), -origin);
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "Usage: benchmark_zombie_lod_quality MANIFEST OUTPUT_DIRECTORY SHADER_DIRECTORY\n";
        return 1;
    }
    try {
        const auto manifestPath = std::filesystem::absolute(argv[1]);
        const auto output = std::filesystem::absolute(argv[2]);
        const auto shaders = std::filesystem::absolute(argv[3]);
        std::ifstream input(manifestPath);
        Json manifest; input >> manifest;
        std::map<std::string, std::vector<Json>> sources;
        for (const auto& record : manifest.at("records")) sources[record.at("source_name").get<std::string>()].push_back(record);
        std::filesystem::create_directories(output / "images");
        stbi_write_png_compression_level = 1;
        stbi_write_force_png_filter = 0;
        Window window;
        const auto gpu = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        if (std::string(gpu).find("llvmpipe") != std::string::npos || std::string(gpu).find("softpipe") != std::string::npos)
            throw std::runtime_error("Hardware comparison requires a hardware GPU");
        Target color(cropSize), mask(cropSize * maskScale);
        Json metadata{{"scenario", "zombie-lod-quality-v1"}, {"gpu", gpu},
            {"opengl", reinterpret_cast<const char*>(glGetString(GL_VERSION))},
            {"scene_width", sceneWidth}, {"scene_height", sceneHeight}, {"crop_size", cropSize},
            {"mask_scale", maskScale}, {"vertical_fov_degrees", 60}, {"normalized_idle_height_m", 1.8},
            {"camera_eye_height_m", 1.65}, {"distances_m", distances}, {"angles", angles}, {"poses_per_clip", poses},
            {"animation", "Matched stepped baked frame for original and all candidates; 15 Hz grid at 12 m, 10 Hz at 35 m"},
            {"lighting", "Default SunLighting, authored sRGB textures/materials; no shadows, haze, fog, scene or postprocessing"},
            {"projection", "192x192 crop at x=544,y=304 of normal 1280x720 60-degree vertical projection; no image registration"},
            {"mask", "Separate 4x depth silhouette capture; distances in metrics must be divided by mask_scale"},
            {"scope", "Geometry appearance experiment, not a human-perception study or GPU timing benchmark"},
            {"manifest_sha256", viewer::fileSha256(manifestPath)}};
        for (const auto* file : {"basic.vert", "basic.frag"}) metadata["shaders"][file] = viewer::fileSha256(shaders / file);
        std::ofstream(output / "metadata.json") << metadata.dump(2) << '\n';
        std::ofstream csv(output / "samples.csv");
        if (!csv) throw std::runtime_error("Cannot write sample CSV");
        csv << "asset,clip,phase,angle,distance,variant,triangles,vertices,width,height,crop_x,crop_y,image,mask,mask_scale,animation_frame\n";
        std::size_t count = 0;
        for (const auto& [sourceName, records] : sources) {
            const auto original = std::find_if(records.begin(), records.end(), [](const auto& r) { return r.at("variant") == "original"; });
            if (original == records.end()) throw std::runtime_error("Missing original model");
            const auto base = viewer::loadZombieModel(original->at("path").template get<std::string>());
            const auto normalize = normalization(base);
            for (const auto& record : records) {
                const auto path = record.at("path").get<std::string>();
                if (viewer::fileSha256(path) != record.at("sha256").get<std::string>())
                    throw std::runtime_error("Model fingerprint mismatch: " + path);
                const auto asset = viewer::loadZombieModel(path);
                const auto variant = record.at("variant").get<std::string>();
                Capture capture(asset, shaders);
                const std::array<viewer::BakedAnimationClip, 3> clips{{
                    {"idle", asset.duration, asset.frameCount, 0, {}},
                    *asset.walkClip, *asset.runClip}};
                for (std::size_t c = 0; c < clips.size(); ++c) {
                    const auto& clip = clips[c];
                    const std::string clipName = c == 0 ? "idle" : c == 1 ? "walk" : "run";
                    for (int pose = 0; pose < poses; ++pose) for (int angle = 0; angle < angles; ++angle)
                        for (const float distance : distances) {
                            const float phase = float(pose) / poses;
                            const float samples = std::max(1.f, std::floor(clip.duration * (distance < 35 ? 15.f : 10.f)));
                            const float frame = std::floor(std::floor(phase * samples) / samples * static_cast<float>(clip.frameCount));
                            const auto transform = glm::rotate(glm::mat4(1), glm::radians(float(angle) * 45), glm::vec3(0, 1, 0)) * normalize;
                            const std::string name = std::filesystem::path(sourceName).stem().string() + "-" + clipName +
                                "-p" + std::to_string(pose) + "-a" + std::to_string(angle * 45) + "-d" +
                                std::to_string(static_cast<int>(distance)) + "-" + variant;
                            const std::string image = "images/" + name + ".png", silhouette = "images/" + name + "-mask.png";
                            capture.draw(color, transform, distance, clip, frame, false); saveColor(output / image, color.size);
                            capture.draw(mask, transform, distance, clip, frame, true); saveMask(output / silhouette, mask.size);
                            csv << sourceName << ',' << clipName << ',' << phase << ',' << angle * 45 << ',' << distance << ',' << variant << ','
                                << record.at("triangles") << ',' << record.at("vertices") << ',' << cropSize << ',' << cropSize << ','
                                << cropX << ',' << cropY << ',' << image << ',' << silhouette << ','
                                << maskScale << ',' << frame << '\n';
                            ++count;
                        }
                }
                csv.flush();
                std::cout << sourceName << ' ' << variant << " complete; samples=" << count << '\n' << std::flush;
            }
        }
        checkGL("final comparison readback");
        metadata["samples"] = count;
        std::ofstream(output / "metadata.json") << metadata.dump(2) << '\n';
        std::cout << "Captured " << count << " samples on " << gpu << '\n';
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
