#include "boid/bird_flock.hpp"
#include "boid/bird_renderer.hpp"
#include "collision_world.hpp"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr int size = 256;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct Target {
    GLuint framebuffer = 0, color = 0, depth = 0;
    Target() {
        glGenFramebuffers(1, &framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
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
        require(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "Bird test framebuffer must be complete");
        glViewport(0, 0, size, size);
    }
    ~Target() {
        glDeleteRenderbuffers(1, &depth);
        glDeleteTextures(1, &color);
        glDeleteFramebuffers(1, &framebuffer);
    }
    void clear(double depthValue = 1) const {
        glDepthMask(GL_TRUE);
        glClearColor(1, 1, 1, 1);
        glClearDepth(depthValue);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }
};

std::size_t checkPixels(bool expectBird = true) {
    std::vector<unsigned char> color(size * size * 4);
    std::vector<float> depth(size * size);
    glReadPixels(0, 0, size, size, GL_RGBA, GL_UNSIGNED_BYTE, color.data());
    glReadPixels(0, 0, size, size, GL_DEPTH_COMPONENT, GL_FLOAT, depth.data());
    std::size_t silhouette = 0, empty = 0;
    for (std::size_t i = 0; i < depth.size(); ++i) {
        if (color[i * 4] < 245 || color[i * 4 + 1] < 245 || color[i * 4 + 2] < 245) {
            ++silhouette;
            require(depth[i] < 1.f, "Pigeon geometry must write depth");
        } else {
            ++empty;
            if (expectBird) require(depth[i] == 1.f, "Discarded card pixels must leave depth untouched");
        }
    }
    if (expectBird) {
        require(silhouette > 5 && silhouette < 10000, "Pigeon mesh must produce visible geometry");
        require(empty > 55000, "Pigeon mesh must leave the surrounding sky clear");
    } else require(silhouette == 0, "Scene depth must occlude birds");
    return silhouette;
}

void saveCapture(const std::string& prefix, const char* view) {
    if (prefix.empty()) return;
    std::vector<unsigned char> rgb(size * size * 3);
    glReadPixels(0, 0, size, size, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());
    std::ofstream image(prefix + "-" + view + ".ppm", std::ios::binary);
    require(static_cast<bool>(image), "Cannot save bird rendering capture");
    image << "P6\n" << size << ' ' << size << "\n255\n";
    for (int y = size - 1; y >= 0; --y)
        image.write(reinterpret_cast<const char*>(rgb.data() + y * size * 3), size * 3);
}

void checkRendering(const std::string& capturePrefix) {
    Target target;
    viewer::BirdRenderer renderer(VIEWER_SHADER_DIR, VIEWER_BIRD_ASSET);
    viewer::FlockBird bird;
    bird.position = {0, 0, 0};
    bird.velocity = {0, 0, -10};
    bird.phase = 0;
    const std::vector<viewer::FlockBird> birds{bird};
    viewer::Camera camera;
    camera.setPosition({0, 0, 5});
    camera.setOrientation(-90, 0);
    target.clear();
    renderer.draw(camera, 1, 0, birds);
    checkPixels();
    saveCapture(capturePrefix, "front");
    camera.setPosition({5, 0, 0});
    camera.setOrientation(180, 0);
    target.clear();
    renderer.draw(camera, 1, 0, birds);
    checkPixels();
    saveCapture(capturePrefix, "side");
    camera.setPosition({0, 5, .0873f});
    camera.setOrientation(-90, -89);
    target.clear();
    renderer.draw(camera, 1, 0, birds);
    checkPixels();
    saveCapture(capturePrefix, "top");
    target.clear(.5);
    renderer.draw(camera, 1, 0, birds);
    checkPixels(false);
    target.clear();
    renderer.draw(camera, 1, 0, {});
    checkPixels(false);
}

void captureFlock(const std::string& prefix) {
    if (prefix.empty()) return;
    const viewer::CollisionWorld world(viewer::Model{});
    viewer::BirdFlocks flocks(world, nullptr, {-120, 0, -120}, {120, 80, 120}, {0, 15, 0});
    viewer::BirdFlockPlayer spectator;
    spectator.targetable = false;
    constexpr float step = 1.f / 60;
    for (int frame = 0; frame < 120; ++frame) flocks.update(step, spectator);
    require(flocks.birds().size() == 1152 && flocks.flocks().size() == 6,
            "Flock capture must exercise the default six simulated flocks");

    const auto& first = flocks.flocks().front();
    auto minimum = flocks.birds()[first.firstBird].position;
    auto maximum = minimum;
    for (std::size_t i = first.firstBird; i < first.firstBird + first.birdCount; ++i) {
        minimum = glm::min(minimum, flocks.birds()[i].position);
        maximum = glm::max(maximum, flocks.birds()[i].position);
    }
    viewer::Camera camera;
    camera.frame(minimum - glm::vec3(2), maximum + glm::vec3(2), 1);
    Target target;
    viewer::BirdRenderer renderer(VIEWER_SHADER_DIR, VIEWER_BIRD_ASSET);
    target.clear();
    renderer.draw(camera, 1, 120 * step, flocks.birds());
    std::vector<unsigned char> color(size * size * 4);
    glReadPixels(0, 0, size, size, GL_RGBA, GL_UNSIGNED_BYTE, color.data());
    std::size_t silhouette = 0;
    for (std::size_t i = 0; i < color.size(); i += 4) silhouette += color[i] < 245;
    require(silhouette > 100, "A framed simulated flock must produce visible bird silhouettes");
    require(glGetError() == GL_NO_ERROR, "A full simulated flock draw must not produce GL errors");
    saveCapture(prefix, "flock");
}

void checkState() {
    Target target;
    viewer::BirdRenderer renderer(VIEWER_SHADER_DIR, VIEWER_BIRD_ASSET);
    viewer::Camera camera;
    camera.setPosition({0, 0, 5});
    camera.setOrientation(-90, 0);
    viewer::FlockBird bird;
    bird.position = {0, 0, 0};
    bird.velocity = {0, 0, -10};
    GLuint vao = 0, buffer = 0, textures[2]{}, samplers[2]{};
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &buffer);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glGenTextures(2, textures); glGenSamplers(2, samplers);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_BUFFER, textures[0]); glBindSampler(0, samplers[0]);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, textures[1]); glBindSampler(1, samplers[1]);
    glActiveTexture(GL_TEXTURE3);
    glUseProgram(0);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    renderer.draw(camera, 1, 0, {bird});
    GLint actual = -1;
    GLboolean depthWrite = GL_TRUE;
    glGetIntegerv(GL_CURRENT_PROGRAM, &actual);
    require(actual == 0, "Bird renderer must restore the shader program");
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &actual);
    require(static_cast<GLuint>(actual) == vao, "Bird renderer must restore the VAO");
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &actual);
    require(static_cast<GLuint>(actual) == buffer, "Bird renderer must restore the array buffer");
    glGetIntegerv(GL_ACTIVE_TEXTURE, &actual);
    require(actual == GL_TEXTURE3, "Pigeon rendering must restore the active texture unit");
    glActiveTexture(GL_TEXTURE0); glGetIntegerv(GL_TEXTURE_BINDING_BUFFER, &actual);
    require(static_cast<GLuint>(actual) == textures[0], "Pigeon rendering must restore the animation binding");
    glGetIntegerv(GL_SAMPLER_BINDING, &actual);
    require(static_cast<GLuint>(actual) == samplers[0], "Pigeon rendering must restore sampler zero");
    glActiveTexture(GL_TEXTURE1); glGetIntegerv(GL_TEXTURE_BINDING_2D, &actual);
    require(static_cast<GLuint>(actual) == textures[1], "Pigeon rendering must restore the atlas binding");
    glGetIntegerv(GL_SAMPLER_BINDING, &actual);
    require(static_cast<GLuint>(actual) == samplers[1], "Pigeon rendering must restore sampler one");
    glBindSampler(0, 0); glBindSampler(1, 0);
    glDeleteSamplers(2, samplers); glDeleteTextures(2, textures); glActiveTexture(GL_TEXTURE0);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWrite);
    require(!depthWrite && !glIsEnabled(GL_DEPTH_TEST) && glIsEnabled(GL_BLEND) && glIsEnabled(GL_CULL_FACE),
            "Bird renderer must restore depth, blend, and culling state");
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDeleteBuffers(1, &buffer);
    glDeleteVertexArrays(1, &vao);
    glDepthMask(GL_TRUE);
    require(glGetError() == GL_NO_ERROR, "Bird rendering must not produce GL errors");
}

void checkLodAndAnimation(const std::string& prefix) {
    Target target;
    viewer::BirdRenderer renderer(VIEWER_SHADER_DIR, VIEWER_BIRD_ASSET);
    viewer::Camera camera;
    camera.setOrientation(-90, 0);
    viewer::FlockBird bird;
    bird.velocity = {0, 0, 10};
    for (std::size_t level = 0; level < 3; ++level) {
        camera.setPosition({0, 0, level == 0 ? 1.f : level == 1 ? 5.f : 10.f});
        target.clear();
        const auto stats = renderer.draw(camera, 1, 0, {bird});
        require(stats.instances[level] == 1, "Pigeon must select the projected-size LoD");
        require(stats.instances[0] + stats.instances[1] + stats.instances[2] == 1,
                "Pigeon must appear exactly once across LoD batches");
        checkPixels();
        saveCapture(prefix, level == 0 ? "lod0" : level == 1 ? "lod1" : "lod2");
    }
    camera.setPosition({0, 0, 1.2f});
    target.clear(); renderer.draw(camera, 1, 0, {bird});
    std::vector<unsigned char> first(size * size * 4), second(first.size());
    glReadPixels(0, 0, size, size, GL_RGBA, GL_UNSIGNED_BYTE, first.data());
    target.clear(); renderer.draw(camera, 1, .1, {bird});
    glReadPixels(0, 0, size, size, GL_RGBA, GL_UNSIGNED_BYTE, second.data());
    require(first != second, "Pigeon wing animation must change the rendered mesh");
    saveCapture(prefix, "animated");
    bool colored = false;
    for (std::size_t i = 0; i < second.size(); i += 4)
        colored |= second[i] > second[i + 2] + 2;
    require(colored, "Pigeon must retain the colored palette texture");
    bird.position = {0, 0, 10};
    target.clear();
    const auto culled = renderer.draw(camera, 1, 0, {bird});
    require(culled.triangles == 0, "Birds behind the camera must be culled");
    checkPixels(false);
}
} // namespace

int main(int argc, char** argv) {
    if (!glfwInit()) { std::cout << "SKIP: no display\n"; return 77; }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    auto* window = glfwCreateWindow(size, size, "Bird rendering regression", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 77; }
    glfwMakeContextCurrent(window);
    int result = 0;
    try {
        viewer::initializeOpenGL();
        const std::string capturePrefix = argc > 1 ? argv[1] : "";
        checkRendering(capturePrefix);
        captureFlock(capturePrefix);
        checkState();
        checkLodAndAnimation(capturePrefix);
        std::cout << "Bird rendering tests passed (" << glGetString(GL_RENDERER) << ")\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
