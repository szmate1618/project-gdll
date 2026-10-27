#include "bird_flock.hpp"
#include "bird_renderer.hpp"
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
        if (color[i * 4] < 20) {
            ++silhouette;
            require(depth[i] < 1.f, "Bird silhouettes must write depth");
        } else {
            ++empty;
            if (expectBird) require(depth[i] == 1.f, "Discarded card pixels must leave depth untouched");
        }
    }
    if (expectBird) {
        require(silhouette > 50 && silhouette < 1000, "Bird cards must show a small masked silhouette");
        require(empty > 64000, "Bird cards must leave the surrounding sky clear");
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
    viewer::BirdRenderer renderer(VIEWER_SHADER_DIR);
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
    const auto front = checkPixels();
    saveCapture(capturePrefix, "front");
    camera.setPosition({5, 0, 0});
    camera.setOrientation(180, 0);
    target.clear();
    renderer.draw(camera, 1, 0, birds);
    const auto side = checkPixels();
    saveCapture(capturePrefix, "side");
    camera.setPosition({0, 5, .0873f});
    camera.setOrientation(-90, -89);
    target.clear();
    renderer.draw(camera, 1, 0, birds);
    const auto top = checkPixels();
    saveCapture(capturePrefix, "top");
    require(top > side * 3 && top > front * 2, "Top triangle must be broader than the flat side/front silhouettes");
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
    viewer::BirdRenderer renderer(VIEWER_SHADER_DIR);
    target.clear();
    renderer.draw(camera, 1, 120 * step, flocks.birds());
    std::vector<unsigned char> color(size * size * 4);
    glReadPixels(0, 0, size, size, GL_RGBA, GL_UNSIGNED_BYTE, color.data());
    std::size_t silhouette = 0;
    for (std::size_t i = 0; i < color.size(); i += 4) silhouette += color[i] < 20;
    require(silhouette > 100, "A framed simulated flock must produce visible bird silhouettes");
    require(glGetError() == GL_NO_ERROR, "A full simulated flock draw must not produce GL errors");
    saveCapture(prefix, "flock");
}

void checkState() {
    Target target;
    viewer::BirdRenderer renderer(VIEWER_SHADER_DIR);
    viewer::Camera camera;
    camera.setPosition({0, 0, 5});
    camera.setOrientation(-90, 0);
    viewer::FlockBird bird;
    bird.position = {0, 0, 0};
    bird.velocity = {0, 0, -10};
    GLuint vao = 0, buffer = 0;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &buffer);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
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
        std::cout << "Bird rendering tests passed (" << glGetString(GL_RENDERER) << ")\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
