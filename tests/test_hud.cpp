#include "hud.hpp"
#include "opengl.hpp"
#include <GLFW/glfw3.h>
#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
GLint integer(GLenum name) { GLint result; glGetIntegerv(name, &result); return result; }
std::vector<unsigned char> pixels(int width, int height) {
    std::vector<unsigned char> result(static_cast<std::size_t>(width) * height * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, result.data());
    return result;
}
std::size_t mapPixels(const std::vector<unsigned char>& image) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < image.size(); i += 3)
        if (image[i] == 0x3a && image[i+1] == 0x51 && image[i+2] == 0x44) ++count;
    return count;
}
void capture(const char* path, const std::vector<unsigned char>& image, int width, int height) {
    std::ofstream file(path, std::ios::binary);
    file << "P6\n" << width << ' ' << height << "\n255\n";
    for (int row = height - 1; row >= 0; --row)
        file.write(reinterpret_cast<const char*>(image.data() + row * width * 3), width * 3);
}
} // namespace

int main(int argc, char** argv) {
    if (!glfwInit()) return 77;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    auto* window = glfwCreateWindow(1280, 720, "HUD regression", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 77; }
    glfwMakeContextCurrent(window);
    int result = 0;
    try {
        viewer::initializeOpenGL();
        std::cout << "OpenGL renderer: " << glGetString(GL_RENDERER) << '\n';
        GLuint framebuffer, color, vao, buffer, unpack, sampler, texture;
        glGenFramebuffers(1, &framebuffer); glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glGenTextures(1, &color); glBindTexture(GL_TEXTURE_2D, color);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1280, 720, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
        require(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "Incomplete HUD test framebuffer");
        glGenVertexArrays(1, &vao); glBindVertexArray(vao);
        glGenBuffers(1, &buffer); glBindBuffer(GL_ARRAY_BUFFER, buffer);
        glGenBuffers(1, &unpack); glBindBuffer(GL_PIXEL_UNPACK_BUFFER, unpack);
        glBufferData(GL_PIXEL_UNPACK_BUFFER, 256, nullptr, GL_STATIC_DRAW);
        glGenSamplers(1, &sampler); glBindSampler(0, sampler);
        glGenTextures(1, &texture); glBindTexture(GL_TEXTURE_2D, texture);
        glActiveTexture(GL_TEXTURE3);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 8); glPixelStorei(GL_UNPACK_ROW_LENGTH, 32);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, 3); glPixelStorei(GL_UNPACK_SKIP_ROWS, 2);
        glViewport(13, 17, 20, 30); glScissor(11, 19, 31, 41);
        glEnable(GL_DEPTH_TEST); glEnable(GL_CULL_FACE); glEnable(GL_STENCIL_TEST);
        glDepthMask(GL_TRUE); glDisable(GL_BLEND);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE);
        glBlendEquationSeparate(GL_FUNC_REVERSE_SUBTRACT, GL_FUNC_SUBTRACT);
        const auto clear = [] {
            glDisable(GL_SCISSOR_TEST); glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
            glEnable(GL_SCISSOR_TEST);
        };
        const auto checkState = [&] {
            require(integer(GL_DRAW_FRAMEBUFFER_BINDING) == GLint(framebuffer) &&
                    integer(GL_READ_FRAMEBUFFER_BINDING) == GLint(framebuffer), "HUD changed framebuffer bindings");
            require(integer(GL_VERTEX_ARRAY_BINDING) == GLint(vao) && integer(GL_ARRAY_BUFFER_BINDING) == GLint(buffer),
                    "HUD changed geometry bindings");
            require(integer(GL_ACTIVE_TEXTURE) == GL_TEXTURE3 && integer(GL_PIXEL_UNPACK_BUFFER_BINDING) == GLint(unpack),
                    "HUD changed texture/upload bindings");
            glActiveTexture(GL_TEXTURE0);
            require(integer(GL_TEXTURE_BINDING_2D) == GLint(texture) && integer(GL_SAMPLER_BINDING) == GLint(sampler),
                    "HUD changed texture unit zero");
            glActiveTexture(GL_TEXTURE3);
            require(integer(GL_UNPACK_ALIGNMENT) == 8 && integer(GL_UNPACK_ROW_LENGTH) == 32 &&
                    integer(GL_UNPACK_SKIP_PIXELS) == 3 && integer(GL_UNPACK_SKIP_ROWS) == 2, "HUD changed pixel unpack state");
            require(glIsEnabled(GL_DEPTH_TEST) && glIsEnabled(GL_CULL_FACE) && glIsEnabled(GL_STENCIL_TEST) &&
                    glIsEnabled(GL_SCISSOR_TEST) && !glIsEnabled(GL_BLEND), "HUD changed GL enables");
            require(integer(GL_BLEND_EQUATION_RGB) == GL_FUNC_REVERSE_SUBTRACT && integer(GL_BLEND_EQUATION_ALPHA) == GL_FUNC_SUBTRACT &&
                    integer(GL_BLEND_SRC_RGB) == GL_SRC_ALPHA && integer(GL_BLEND_DST_RGB) == GL_ONE &&
                    integer(GL_BLEND_SRC_ALPHA) == GL_ZERO && integer(GL_BLEND_DST_ALPHA) == GL_ONE,
                    "HUD changed blend state");
            std::array<GLint, 4> viewport{}, scissor{};
            glGetIntegerv(GL_VIEWPORT, viewport.data()); glGetIntegerv(GL_SCISSOR_BOX, scissor.data());
            require(viewport == std::array<GLint, 4>{13,17,20,30} && scissor == std::array<GLint, 4>{11,19,31,41},
                    "HUD changed viewport/scissor");
            require(glGetError() == GL_NO_ERROR, "HUD generated an OpenGL error");
        };
        viewer::HudState state;
        state.equipment = "Pistol <test>"; state.slot = 0; state.loaded = 7; state.reserve = 42;
        state.movement = "CROUCH"; state.yawDegrees = 225; state.speedMetersPerSecond = 1.5f / 3.6f;
        std::vector<unsigned char> first;
        {
            viewer::Hud hud(VIEWER_HUD_DIR);
            clear(); hud.draw(state, 1280, 720); checkState();
            first = pixels(1280, 720);
            require(mapPixels(first) > 11400 && mapPixels(first) < 11800,
                    "HUD did not draw its map placeholder into the scene framebuffer");
            for (int y = 0; y < 720; ++y) for (int x = 0; x < 1280; ++x) {
                const auto i = static_cast<std::size_t>(y * 1280 + x) * 3;
                if (first[i] == 0x3a && first[i+1] == 0x51 && first[i+2] == 0x44)
                    require(x > 1100 && y > 550, "Minimap is not anchored to the upper right");
            }
            require(first[(680 * 1280 + 40) * 3] == 0, "Removed upper-left panel still obscures the scene");
            if (argc > 1) capture(argv[1], first, 1280, 720);
            clear(); hud.draw(state, 1280, 720); checkState();
            require(first == pixels(1280, 720), "Unchanged HUD state changed rendered output");
            state.loaded = 3;
            clear(); hud.draw(state, 1280, 720); checkState();
            const auto changed = pixels(1280, 720);
            std::size_t differences = 0;
            for (int y = 0; y < 720; ++y) for (int x = 0; x < 1280; ++x) {
                const auto i = static_cast<std::size_t>(y * 1280 + x) * 3;
                if (first[i] == changed[i] && first[i+1] == changed[i+1] && first[i+2] == changed[i+2]) continue;
                ++differences;
                require(x > 1100 && y < 100, "Ammo update changed unrelated HUD content");
            }
            require(differences > 20, "Live ammunition did not update");
            state.yawDegrees = 269;
            clear(); hud.draw(state, 1280, 720); checkState();
            const auto westOfNorth = pixels(1280, 720);
            state.yawDegrees = 270;
            clear(); hud.draw(state, 1280, 720); checkState();
            const auto north = pixels(1280, 720);
            std::size_t compassChanges = 0;
            for (int y = 0; y < 720; ++y) for (int x = 0; x < 1280; ++x) {
                const auto i = static_cast<std::size_t>(y * 1280 + x) * 3;
                if (westOfNorth[i] == north[i] && westOfNorth[i+1] == north[i+1] && westOfNorth[i+2] == north[i+2]) continue;
                ++compassChanges;
                require(x > 410 && x < 870 && y > 630 && y < 700, "Compass wrap changed content outside its clipped ribbon");
            }
            require(compassChanges > 20 && compassChanges < 5000, "Compass did not scroll continuously across north");
            state.yawDegrees = 225;
            clear(); hud.draw(state, 640, 360); checkState();
            const auto small = pixels(640, 360);
            require(mapPixels(small) > 2850 && mapPixels(small) < 3000, "HUD did not scale its map on resize");
            hud.draw(state, 0, 0); checkState();
            hud.draw(state, 1280, 720); checkState();
        }
        // Multiplayer returns to its shared menu window and starts another session.
        {
            viewer::Hud hud(VIEWER_HUD_DIR);
            state.loaded = 7;
            clear(); hud.draw(state, 1280, 720); checkState();
            require(first == pixels(1280, 720), "HUD failed to recreate after a session ended");
        }
        glDeleteSamplers(1, &sampler); glDeleteTextures(1, &texture); glDeleteTextures(1, &color);
        glDeleteBuffers(1, &buffer); glDeleteBuffers(1, &unpack); glDeleteVertexArrays(1, &vao);
        glDeleteFramebuffers(1, &framebuffer);
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    glfwDestroyWindow(window); glfwTerminate();
    return result;
}
