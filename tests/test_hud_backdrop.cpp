#include "gui/hud_backdrop.hpp"
#include "gui/hud_render_interface.hpp"
#include "opengl.hpp"

#include <GLFW/glfw3.h>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct Target {
    GLuint framebuffer = 0, color = 0;
    int width, height;

    Target(int widthValue, int heightValue) : width(widthValue), height(heightValue) {
        glGenFramebuffers(1, &framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glGenTextures(1, &color);
        glBindTexture(GL_TEXTURE_2D, color);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
        require(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
            "Incomplete backdrop test framebuffer");
    }
    ~Target() {
        glDeleteTextures(1, &color);
        glDeleteFramebuffers(1, &framebuffer);
    }
    Target(const Target&) = delete;
    Target& operator=(const Target&) = delete;

    void clear(float grey) const {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
        glDisable(GL_SCISSOR_TEST);
        const GLfloat value[]{grey, grey, grey, 1};
        glClearBufferfv(GL_COLOR, 0, value);
    }
    void mixed() const {
        clear(16.f / 255);
        glEnable(GL_SCISSOR_TEST);
        // Only the upper-right quadrant is bright: also detect a Y-axis flip.
        glScissor(width / 2, height / 2, width - width / 2, height - height / 2);
        constexpr GLfloat white[]{1, 1, 1, 1};
        glClearBufferfv(GL_COLOR, 0, white);
        glDisable(GL_SCISSOR_TEST);
    }
    std::array<unsigned char, 3> colorAt(int x, int top) const {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        std::array<unsigned char, 3> result{};
        glReadPixels(x, height - 1 - top, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, result.data());
        return result;
    }
    int pixel(int x, int top) const { return colorAt(x, top)[0]; }
    void capture(const std::filesystem::path& path) const {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        std::vector<unsigned char> image(static_cast<std::size_t>(width) * height * 3);
        glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, image.data());
        std::ofstream file(path, std::ios::binary);
        file << "P6\n" << width << ' ' << height << "\n255\n";
        for (int row = height - 1; row >= 0; --row)
            file.write(reinterpret_cast<const char*>(image.data() + static_cast<std::size_t>(row) * width * 3), width * 3);
        require(bool(file), "Cannot write backdrop capture");
    }
};

void checkBackdrop(const char* capturePrefix) {
    viewer::HudRenderInterface renderer(VIEWER_HUD_DIR);
    viewer::HudBackdrop backdrop(VIEWER_HUD_DIR);
    Target target(320, 180), unrelatedRead(320, 180);
    unrelatedRead.clear(0);
    viewer::HudBackdrop::Bounds bounds{};
    bounds[0] = {32, 24, 96, 60};
    bounds[1] = {192, 24, 96, 60};
    GLuint unitOneTexture = 0, unitOneSampler = 0;
    glGenTextures(1, &unitOneTexture); glGenSamplers(1, &unitOneSampler);
    const auto draw = [&](const Target& destination, float seconds) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destination.framebuffer);
        // Sampling must use the scene being drawn into, regardless of READ binding.
        glBindFramebuffer(GL_READ_FRAMEBUFFER, unrelatedRead.framebuffer);
        glViewport(0, 0, destination.width, destination.height);
        glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE); glDisable(GL_STENCIL_TEST); glDisable(GL_SCISSOR_TEST);
        glDisable(GL_SAMPLE_COVERAGE); glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE); glDisable(GL_SAMPLE_MASK);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, unitOneTexture); glBindSampler(1, unitOneSampler);
        glActiveTexture(GL_TEXTURE3);
        renderer.renderExternal([&] { backdrop.draw(bounds, destination.width, destination.height, seconds,
            glm::vec3(43.f / 255.f, 45.f / 255.f, 47.f / 255.f)); });
        GLint drawFramebuffer = 0, readFramebuffer = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFramebuffer);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFramebuffer);
        require(drawFramebuffer == GLint(destination.framebuffer) && readFramebuffer == GLint(unrelatedRead.framebuffer),
            "Backdrop did not preserve framebuffer bindings through the UI guard");
        GLint activeTexture = 0, boundTexture = 0, boundSampler = 0;
        glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
        glActiveTexture(GL_TEXTURE1);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &boundTexture); glGetIntegerv(GL_SAMPLER_BINDING, &boundSampler);
        require(activeTexture == GL_TEXTURE3 && boundTexture == GLint(unitOneTexture) && boundSampler == GLint(unitOneSampler),
            "Backdrop changed the caller's active texture or texture/sampler on unit one");
        glActiveTexture(GL_TEXTURE3);
        require(glGetError() == GL_NO_ERROR, "Backdrop generated an OpenGL error");
    };

    target.clear(16.f / 255);
    draw(target, 0);
    require(target.pixel(72, 48) == 16 && target.pixel(232, 48) == 16,
        "Dark scenery must retain exactly transparent backdrops");
    if (capturePrefix) target.capture(std::string(capturePrefix) + "-dark.ppm");

    backdrop.reset();
    target.mixed();
    draw(target, 0);
    const int bright = target.pixel(232, 48);
    require(bright >= 212 && bright <= 216, "Bright scenery must respect the 19.5% backdrop opacity cap");
    const auto tint = target.colorAt(232, 48);
    require(tint[0] <= tint[1] && tint[1] <= tint[2] && tint[2] - tint[0] <= 2,
        "The default backdrop must use the menu's charcoal tint");
    require(target.pixel(72, 48) == 16, "A bright region must not darken a HUD group over dark scenery");
    require(target.pixel(300, 48) == 255 && target.pixel(232, 130) == 16,
        "Backdrops must leave scenery outside their rectangles unchanged");
    if (capturePrefix) target.capture(std::string(capturePrefix) + "-mixed.ppm");
    for (int frame = 0; frame < 20; ++frame) {
        target.mixed();
        draw(target, .016f);
        require(std::abs(target.pixel(232, 48) - bright) <= 1,
            "Steady scenery must not make a backdrop fade or sample its own backing");
    }

    backdrop.reset();
    target.clear(0);
    draw(target, 0);
    target.clear(1);
    draw(target, .02f);
    int previous = target.pixel(232, 48);
    require(previous < 255 && previous > bright + 10, "Dark-to-bright adaptation must fade rather than jump");
    for (int frame = 0; frame < 20; ++frame) {
        target.clear(1);
        draw(target, .02f);
        const int current = target.pixel(232, 48);
        require(current <= previous && current >= bright - 1, "Darkening must approach its target monotonically");
        previous = current;
    }
    require(previous < bright + 5, "Bright-scene adaptation did not settle");
    if (capturePrefix) target.capture(std::string(capturePrefix) + "-bright.ppm");

    target.clear(64.f / 255);
    draw(target, .02f);
    previous = target.pixel(232, 48);
    require(previous > 40 && previous < 64, "Bright-to-dark adaptation must fade rather than jump");
    for (int frame = 0; frame < 60; ++frame) {
        target.clear(64.f / 255);
        draw(target, .02f);
        const int current = target.pixel(232, 48);
        require(current >= previous && current <= 64, "Clearing must approach transparency monotonically");
        previous = current;
    }
    require(previous >= 62, "Dark-scene adaptation did not clear");

    const auto transition = [&](int frames, float seconds) {
        backdrop.reset();
        target.clear(0);
        draw(target, 0);
        for (int frame = 0; frame < frames; ++frame) { target.clear(1); draw(target, seconds); }
        return target.pixel(232, 48);
    };
    const int fasterFrames = transition(20, .01f);
    const int slowerFrames = transition(10, .02f);
    require(std::abs(fasterFrames - slowerFrames) <= 3,
        "Adaptation speed must depend on elapsed time rather than frame count");

    bounds = {};
    target.clear(1);
    draw(target, .02f);
    require(target.pixel(232, 48) == 255, "Hidden HUD groups must not retain visible backdrops");

    // A new framebuffer size must preserve top-left HUD coordinates and scene mapping.
    Target resized(640, 360);
    bounds[0] = {64, 48, 192, 120};
    bounds[1] = {384, 48, 192, 120};
    backdrop.reset();
    resized.mixed();
    draw(resized, 0);
    require(resized.pixel(144, 96) == 16 && std::abs(resized.pixel(464, 96) - bright) <= 2,
        "Resizing must retain local brightness and backdrop placement");
    require(resized.pixel(600, 96) == 255, "Resizing enlarged backdrops beyond their rectangles");
    require(resized.pixel(381, 96) == resized.pixel(464, 96),
        "Panel edges must use the same flat fill as their interior without a border or feather");

    // Separated sectors share a central hole, but each samples its own wedge.
    bounds = {};
    bounds[15] = {230,87,180,180};
    bounds[16] = {232.59808f,91.5f,180,180};
    bounds[17] = {227.40192f,91.5f,180,180};
    resized.clear(0);
    glEnable(GL_SCISSOR_TEST); glScissor(0,0,320,360);
    constexpr GLfloat white[]{1,1,1,1}; glClearBufferfv(GL_COLOR, 0, white);
    glDisable(GL_SCISSOR_TEST);
    backdrop.reset(); draw(resized, 0);
    require(std::abs(resized.pixel(270, 210) - bright) <= 2 && resized.pixel(370, 210) == 0,
        "The bright Backup sector and dark Move sector must adapt independently");
    require(resized.pixel(320, 120) > 0 && resized.pixel(320, 120) < 10,
        "The Danger sector crossing bright and dark scenery must use its own region average");
    require(resized.pixel(305, 180) == 255 && resized.pixel(319, 245) == 255 && resized.pixel(240, 100) == 255,
        "Sector backdrops must leave the central hole, separation gaps and outer corners clear");
    if (capturePrefix) resized.capture(std::string(capturePrefix) + "-sectors.ppm");

    target.clear(0);
    glEnable(GL_SCISSOR_TEST); glScissor(80,0,160,180);
    glClearBufferfv(GL_COLOR, 0, white);
    glDisable(GL_SCISSOR_TEST);
    bounds = {}; bounds[0] = {16,32,40,96}; bounds[1] = {100,32,96,96};
    backdrop.reset(); draw(target, 0);
    require(target.pixel(40, 80) == 0 && std::abs(target.pixel(140, 80) - bright) <= 2,
        "Letterbox bars must remain clear while HUD groups over the fitted scene adapt");

    target.clear(0);
    glEnable(GL_SCISSOR_TEST); glScissor(224,0,96,180);
    glClearBufferfv(GL_COLOR, 0, white);
    glDisable(GL_SCISSOR_TEST);
    bounds = {}; bounds[0] = {32,24,256,128};
    backdrop.reset(); draw(target, 0);
    require(target.pixel(260, 80) > bright + 10 && target.pixel(260, 80) < 254,
        "A mixed bright/dark group must adapt to its region average rather than one point");
    glDeleteTextures(1, &unitOneTexture); glDeleteSamplers(1, &unitOneSampler);
}
} // namespace

int main(int argc, char** argv) {
    if (!glfwInit()) return 77;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    auto* window = glfwCreateWindow(320, 180, "Adaptive HUD backdrop regression", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 77; }
    glfwMakeContextCurrent(window);
    int result = 0;
    try {
        viewer::initializeOpenGL();
        checkBackdrop(argc > 1 ? argv[1] : nullptr);
        std::cout << "Adaptive HUD backdrop tests passed (" << glGetString(GL_RENDERER) << ")\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
