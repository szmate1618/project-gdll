#include "gui/hud.hpp"
#include "inventory/inventory_config.hpp"
#include "minimap.hpp"
#include "minimap_fixture.hpp"
#include "opengl.hpp"
#include <GLFW/glfw3.h>
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
GLint integer(GLenum name) { GLint result; glGetIntegerv(name, &result); return result; }
std::array<int, 3> colorAt(const std::vector<unsigned char>& image, int width, int height, int x, int top) {
    const auto i = static_cast<std::size_t>((height - 1 - top) * width + x) * 3;
    return {image[i], image[i+1], image[i+2]};
}
bool isColor(std::array<int, 3> actual, std::array<int, 3> expected) {
    return std::abs(actual[0] - expected[0]) <= 2 && std::abs(actual[1] - expected[1]) <= 2 &&
           std::abs(actual[2] - expected[2]) <= 2;
}
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
struct StorageFixture {
    std::filesystem::path directory = std::filesystem::current_path() /
        ("hud-storage-fixture-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    StorageFixture() {
        const std::filesystem::path ui(VIEWER_HUD_DIR);
        std::filesystem::create_directory(directory);
        std::filesystem::copy(ui, directory / "ui", std::filesystem::copy_options::recursive);
        std::filesystem::copy(ui.parent_path() / "storage", directory / "storage", std::filesystem::copy_options::recursive);
        std::filesystem::copy_file(ui.parent_path() / "inventory.toml", directory / "inventory.toml");
        std::filesystem::copy_file(ui.parent_path() / "skills.toml", directory / "skills.toml");
        std::filesystem::copy(ui.parent_path() / "skills", directory / "skills", std::filesystem::copy_options::recursive);
    }
    ~StorageFixture() { std::filesystem::remove_all(directory); }
    void select(const char* fields) const {
        std::ofstream(directory / "storage/loadout.toml") << "schema_version=1\n" << fields;
        // Storage-only variants have no items to place in unequipped gear.
        std::ofstream(directory / "inventory.toml") << "[weapons]\n";
    }
};
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
        const auto mapModel = minimapFixture();
        const auto mapSource = viewer::findMinimapSource(mapModel);
        require(mapSource.has_value(), "Missing test map source");
        GLuint mapTexture;
        glGenTextures(1, &mapTexture); glBindTexture(GL_TEXTURE_2D, mapTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, mapModel.images[0].rgba.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        GLuint framebuffer, readFramebuffer, renderbuffer, color, vao, buffer, unpack, sampler, texture;
        glGenFramebuffers(1, &framebuffer); glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glGenTextures(1, &color); glBindTexture(GL_TEXTURE_2D, color);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1280, 720, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
        require(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "Incomplete HUD test framebuffer");
        glGenFramebuffers(1, &readFramebuffer); glBindFramebuffer(GL_READ_FRAMEBUFFER, readFramebuffer);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
        glGenRenderbuffers(1, &renderbuffer); glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
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
        glDisable(GL_MULTISAMPLE);
        glEnable(GL_SAMPLE_COVERAGE); glSampleCoverage(0, GL_FALSE);
        glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
        glEnable(GL_SAMPLE_MASK); glSampleMaski(0, 0);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE);
        glBlendEquationSeparate(GL_FUNC_REVERSE_SUBTRACT, GL_FUNC_SUBTRACT);
        const auto clear = [] {
            glDisable(GL_SCISSOR_TEST); glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
            glEnable(GL_SCISSOR_TEST);
        };
        const auto checkState = [&] {
            require(integer(GL_DRAW_FRAMEBUFFER_BINDING) == GLint(framebuffer) &&
                    integer(GL_READ_FRAMEBUFFER_BINDING) == GLint(readFramebuffer) &&
                    integer(GL_RENDERBUFFER_BINDING) == GLint(renderbuffer), "HUD changed framebuffer/renderbuffer bindings");
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
            require(!glIsEnabled(GL_MULTISAMPLE) && glIsEnabled(GL_SAMPLE_COVERAGE) &&
                    glIsEnabled(GL_SAMPLE_ALPHA_TO_COVERAGE) && glIsEnabled(GL_SAMPLE_MASK),
                    "HUD changed multisample coverage state");
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
            auto hudInventory = viewer::loadPlayerInventory(std::filesystem::path(VIEWER_HUD_DIR).parent_path());
            viewer::Hud hud(VIEWER_HUD_DIR, hudInventory);
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
            state.selectionSlot = 1;
            state.weaponSwitchDuration = 4;
            state.weaponSwitchRemaining = 3;
            clear(); hud.draw(state, 1280, 720); checkState();
            const auto selecting = pixels(1280, 720);
            require(selecting != first, "Selection overlay and progress must render");
            if (argc > 1) capture((std::string(argv[1]) + "-selection.ppm").c_str(), selecting, 1280, 720);
            state.selectionSlot = 5; state.weaponSwitchRemaining = 1;
            clear(); hud.draw(state, 1280, 720); checkState();
            require(selecting != pixels(1280, 720), "Selected skill and progress changes must render");
            if (argc > 1) capture((std::string(argv[1]) + "-selection-skill.ppm").c_str(), pixels(1280, 720), 1280, 720);
            clear(); hud.draw(state, 640, 360); checkState();
            if (argc > 1) capture((std::string(argv[1]) + "-selection-small.ppm").c_str(), pixels(640, 360), 640, 360);
            state.selectionSlot = -1; state.weaponSwitchDuration = state.weaponSwitchRemaining = 0;
            clear(); hud.draw(state, 1280, 720); checkState();
            require(first == pixels(1280, 720), "Closing selection must restore the gameplay HUD");
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
            hud.setMinimap(*mapSource, mapTexture); checkState();
            clear(); hud.draw(state, 1280, 720); checkState();
            const auto map = pixels(1280, 720);
            require(isColor(colorAt(map, 1280, 720, 1150, 50), {234,15,4}) &&
                    isColor(colorAt(map, 1280, 720, 1248, 50), {4,222,15}) &&
                    isColor(colorAt(map, 1280, 720, 1150, 120), {4,15,234}) &&
                    isColor(colorAt(map, 1280, 720, 1248, 120), {234,222,4}),
                    "Minimap orientation or display contrast is wrong");
            require(isColor(colorAt(map, 1280, 720, 1198, 50), {234,15,4}) &&
                    isColor(colorAt(map, 1280, 720, 1201, 50), {4,222,15}),
                    "Nearest-filtered minimap must retain a sharp texel boundary without blending");
            require(isColor(colorAt(map, 1280, 720, 1150, 35), {234,15,4}),
                    "300 m window must stay within the terrain's north edge");
            state.position.x = 180;
            clear(); hud.draw(state, 1280, 720); checkState();
            const auto moved = pixels(1280, 720);
            // The 400 m terrain ends at X=200. A 300 m window centered at
            // X=180 must reach its edge 56.7% across the control, leaving blank
            // space afterward rather than repeating or stretching the texture.
            require(isColor(colorAt(moved, 1280, 720, 1170, 50), {4,222,15}) &&
                    isColor(colorAt(moved, 1280, 720, 1202, 50), {4,222,15}) &&
                    isColor(colorAt(moved, 1280, 720, 1215, 50), {18,26,20}),
                    "Minimap did not follow the player with a fixed 300 m window at the map edge");
            require(isColor(colorAt(moved, 1280, 720, 1200, 84), {245,246,242}), "Player marker moved away from the center");
            state.position.x = 0;
            clear(); hud.draw(state, 640, 360); checkState();
            const auto resizedMap = pixels(640, 360);
            require(isColor(colorAt(resizedMap, 640, 360, 575, 25), {234,15,4}) &&
                    isColor(colorAt(resizedMap, 640, 360, 624, 60), {234,222,4}),
                    "Resizing changed the minimap's world coverage or orientation");
            hud.setMinimapRoads(minimapRoadFixture()); checkState();
            clear(); hud.draw(state, 1280, 720); checkState();
            const auto roads = pixels(1280, 720);
            require(isColor(colorAt(roads, 1280, 720, 1232, 50), {230,219,184}),
                    "Road surface must be flat-colored over satellite imagery, ignoring its source texture");
            require(isColor(colorAt(roads, 1280, 720, 1232, 10), {0,0,0}) &&
                    isColor(colorAt(roads, 1280, 720, 1232, 160), {0,0,0}), "Roads escaped the minimap rectangle");
            require(isColor(colorAt(roads, 1280, 720, 1150, 50), {234,15,4}), "Road overlay changed unrelated imagery");
            state.position.x = 90;
            clear(); hud.draw(state, 1280, 720); checkState();
            const auto centeredRoad = pixels(1280, 720);
            require(isColor(colorAt(centeredRoad, 1280, 720, 1200, 60), {230,219,184}) &&
                    isColor(colorAt(centeredRoad, 1280, 720, 1200, 84), {245,246,242}),
                    "Roads must follow the minimap center and remain below the player marker");
            state.position.x = 1000;
            clear(); hud.draw(state, 1280, 720); checkState();
            require(isColor(colorAt(pixels(1280, 720), 1280, 720, 1224, 50), {18,26,20}),
                    "A panned minimap retained stale nearby road geometry");
            state.position.x = 0;
            // A road just under one output pixel wide, limited to the north
            // half of the map, must retain partial coverage while scrolling.
            auto thinRoad = minimapRoadFixture();
            const std::array<glm::vec3, 4> thinCorners{{{40, 15, -110}, {42.5f, 15, -110},
                                                       {42.5f, 15, -30}, {40, 15, -30}}};
            for (std::size_t i = 0; i < thinCorners.size(); ++i)
                thinRoad.primitives.back().vertices[i].position = thinCorners[i];
            hud.setMinimapRoads(thinRoad); checkState();
            bool partialCoverage = false;
            for (int step = 0; step < 8; ++step) {
                state.position.x = step * .35f;
                clear(); hud.draw(state, 1280, 720); checkState();
                const auto movingRoad = pixels(1280, 720);
                int coverage = 0;
                for (int x = 1210; x < 1220; ++x) {
                    const auto red = colorAt(movingRoad, 1280, 720, x, 60)[0];
                    coverage += red - 4;
                    partialCoverage |= red > 10 && red < 224;
                }
                require(coverage > 100 && coverage < 280, "Subpixel road disappeared or changed width while scrolling");
                require(isColor(colorAt(movingRoad, 1280, 720, 1215, 120), {234,222,4}),
                        "Resolved road overlay is upside down or contaminated untouched satellite imagery");
            }
            require(partialCoverage, "Road edges lack multisampled partial coverage");
            state.position.x = 0;
            hud.setMinimapRoads(minimapRoadFixture());
            clear(); hud.draw(state, 640, 360); checkState();
            require(isColor(colorAt(pixels(640, 360), 640, 360, 616, 25), {230,219,184}),
                    "Road MSAA target did not follow HUD resize");
        }
        require(glIsTexture(mapTexture), "HUD deleted the borrowed terrain texture");
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, mapTexture);
        GLint filter; glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &filter);
        require(filter == GL_LINEAR, "HUD changed the scene terrain's texture filtering");
        glBindTexture(GL_TEXTURE_2D, texture); glActiveTexture(GL_TEXTURE3);
        // Multiplayer returns to its shared menu window and starts another session.
        {
            auto hudInventory = viewer::loadPlayerInventory(std::filesystem::path(VIEWER_HUD_DIR).parent_path());
            viewer::Hud hud(VIEWER_HUD_DIR, hudInventory);
            state.loaded = 7;
            clear(); hud.draw(state, 1280, 720); checkState();
            require(first == pixels(1280, 720), "HUD failed to recreate after a session ended");
            state.screen = viewer::HudScreen::Inventory;
            clear(); hud.draw(state, 1280, 720); checkState();
            const auto inventory = pixels(1280, 720);
            std::size_t bodyItemPixels = 0, backpackItemPixels = 0;
            for (int y = 160; y < 500; ++y) for (int x = 34; x < 872; ++x) {
                if (isColor(colorAt(inventory, 1280, 720, x, y), {89,196,156})) {
                    if (x < 424) ++bodyItemPixels;
                    if (x > 442) ++backpackItemPixels;
                }
            }
            require(bodyItemPixels > 60 && backpackItemPixels > 150,
                    "Inventory did not render occupied item rectangles in body storage and backpack");
            if (argc > 1) capture((std::string(argv[1]) + "-inventory.ppm").c_str(), inventory, 1280, 720);
            state.screen = viewer::HudScreen::Skills;
            clear(); hud.draw(state, 1280, 720); checkState();
            const auto skills = pixels(1280, 720);
            if (argc > 1) capture((std::string(argv[1]) + "-skills.ppm").c_str(), skills, 1280, 720);
            std::size_t pageChanges = 0;
            for (int y = 0; y < 720; ++y) for (int x = 0; x < 1280; ++x) {
                const auto i = static_cast<std::size_t>(y * 1280 + x) * 3;
                const bool same = inventory[i] == skills[i] && inventory[i+1] == skills[i+1] && inventory[i+2] == skills[i+2];
                if (y < 160) require(same, "Inventory and skills show different shared hotbars");
                if (!same) ++pageChanges;
            }
            require(pageChanges > 5000, "Inventory and skills did not render different page content");
            clear(); hud.draw(state, 640, 360); checkState();
            const auto smallSkills = pixels(640, 360);
            if (argc > 1) capture((std::string(argv[1]) + "-skills-small.ppm").c_str(), smallSkills, 640, 360);
            const auto marginColor = [&] {
                const auto i = static_cast<std::size_t>((360 - 1 - 355) * 640) * 3;
                return std::array<unsigned char, 3>{smallSkills[i], smallSkills[i+1], smallSkills[i+2]};
            }();
            for (int y = 0; y < 10; ++y) for (int x = 17; x < 623; ++x) {
                const auto i = static_cast<std::size_t>(y * 640 + x) * 3;
                require(smallSkills[i] == marginColor[0] && smallSkills[i+1] == marginColor[1] && smallSkills[i+2] == marginColor[2],
                        "Scaled hotbar wrapped or overflowed into the bottom margin");
            }
            state.screen = viewer::HudScreen::Inventory;
            clear(); hud.draw(state, 640, 360); checkState();
            if (argc > 1) capture((std::string(argv[1]) + "-inventory-small.ppm").c_str(), pixels(640, 360), 640, 360);
            state.screen = viewer::HudScreen::Health;
            clear(); hud.draw(state, 1280, 720); checkState();
            const auto health = pixels(1280, 720);
            if (argc > 1) capture((std::string(argv[1]) + "-health.ppm").c_str(), health, 1280, 720);
            require(colorAt(health, 1280, 720, 100, 200) == colorAt(health, 1280, 720, 640, 360),
                    "Empty health panel contains unexpected content");
            state.screen = viewer::HudScreen::Map;
            clear(); hud.draw(state, 1280, 720); checkState();
            const auto noMap = pixels(1280, 720);
            hud.setMinimap(*mapSource, mapTexture);
            clear(); hud.draw(state, 1280, 720); checkState();
            const auto fullMap = pixels(1280, 720);
            require(isColor(colorAt(fullMap, 1280, 720, 400, 180), {220,30,20}) &&
                    isColor(colorAt(fullMap, 1280, 720, 870, 180), {20,210,30}) &&
                    isColor(colorAt(fullMap, 1280, 720, 400, 640), {20,30,220}) &&
                    isColor(colorAt(fullMap, 1280, 720, 870, 640), {220,210,20}),
                    "Full map is missing imagery, has the wrong orientation or changes original RGB colors");
            hud.setMinimapRoads(minimapRoadFixture());
            state.position = {1000, 0, 1000};
            clear(); hud.draw(state, 1280, 720); checkState();
            require(fullMap == pixels(1280, 720), "Full map drew roads or followed player movement");
            if (argc > 1) capture((std::string(argv[1]) + "-map.ppm").c_str(), fullMap, 1280, 720);
            clear(); hud.draw(state, 640, 360); checkState();
            require(isColor(colorAt(pixels(640, 360), 640, 360, 200, 90), {220,30,20}),
                    "Full map did not scale with the screen");
            hud.setMinimap(*mapSource, 0);
            clear(); hud.draw(state, 1280, 720); checkState();
            require(noMap == pixels(1280, 720), "Full map retained stale imagery or fallback text after its source was removed");
            state.position = {0, 0, 0};
            state.screen = viewer::HudScreen::Gameplay;
            clear(); hud.draw(state, 1280, 720); checkState();
            require(first == pixels(1280, 720), "Closing a prototype screen failed to restore the gameplay HUD");
        }
        {
            auto hudInventory = viewer::loadPlayerInventory(std::filesystem::path(VIEWER_HUD_DIR).parent_path());
            viewer::Hud hud(VIEWER_HUD_DIR, hudInventory);
            state.screen = viewer::HudScreen::Inventory;
            const auto draw = [&](double elapsed = 0) { state.seconds += elapsed; clear(); hud.draw(state, 1280, 720); checkState(); return pixels(1280, 720); };
            const auto initial = draw();
            const auto move = [&](float x, float y) { hud.pointerMove(x / 1280, y / 720); };
            const auto click = [&](const char* id, float x, float y) {
                const auto before = *hudInventory.locationOf(id)->placement;
                move(x, y); hud.pointerButton(0, true); draw();
                require(!hudInventory.held() && hudInventory.locationOf(id)->placement,
                    "Pressing an item must select it without starting a pickup");
                move(x + 1, y + 1); hud.pointerButton(0, false); draw();
                const auto after = hudInventory.locationOf(id);
                require(!hudInventory.held() && after->placement && after->placement->storage == before.storage &&
                    after->placement->x == before.x && after->placement->y == before.y && after->placement->rotation == before.rotation,
                    "Clicking or small pointer jitter must not start a timed replacement");
            };
            click("pistol", 65, 402);
            click("he_grenade", 304, 333);
            click("sniper", 600, 293);
            // Preserve the path even when moving away and back happens within
            // a single render frame. Only the actual backpack drag is timed.
            move(65, 402); hud.pointerButton(0, true);
            move(200, 420); move(65, 402); hud.pointerButton(0, false); draw();
            require(!hudInventory.held() && hudInventory.locationOf("pistol")->placement->storage == "body/hip_holster/holster",
                "Dragging a pistol away and back into its holster must return it without a timer");
            move(600, 293); hud.pointerButton(0, true);
            move(800, 450); move(600, 293); hud.pointerButton(0, false); draw();
            require(hudInventory.held() && hudInventory.held()->item == "sniper",
                "A true backpack drag returned to its origin must retain the backpack access cost");
            hud.pointerButton(1, true); draw();
            // Exchange SMG for shotgun, then shotgun for sniper. Cancel must
            // restore all three footprints, rather than only the last item.
            move(600, 261); hud.pointerButton(0, true);
            move(650, 261); hud.pointerButton(0, false); draw(); draw(6);
            move(600, 293); hud.pointerButton(0, true); hud.pointerButton(0, false); draw(); draw(4);
            hud.pointerButton(1, true);
            const auto cancelled = draw();
            for (int y = 150; y < 540; ++y) for (int x = 34; x < 872; ++x)
                require(colorAt(initial, 1280, 720, x, y) == colorAt(cancelled, 1280, 720, x, y), "Right-click failed to restore all swapped item footprints");
            // Sniper starts at backpack row 2. Pick up its first cell, rotate,
            // and align the rotated grab cell with the sling's second column.
            move(600, 293); hud.pointerButton(0, true); hud.mouseWheel(1);
            move(74, 195);
            const auto held = draw();
            require(held != initial, "Inventory input did not show a held item");
            if (argc > 1) capture((std::string(argv[1]) + "-drag-preview.ppm").c_str(), held, 1280, 720);
            hud.pointerButton(0, false);
            const auto pending = draw();
            require(hudInventory.held() && !hudInventory.placements().count("sniper"),
                "Item must remain held until placement access time completes");
            const auto halfway = draw(2.01);
            require(pending != halfway, "Pending placement did not animate its progress bar");
            require(isColor(colorAt(halfway, 1280, 720, 65, 261), {231,205,101}),
                "Placement progress bar must appear near the destination item");
            clear(); hud.draw(state, 640, 360); checkState();
            require(isColor(colorAt(pixels(640, 360), 640, 360, 33, 131), {231,205,101}),
                "Pending placement progress must follow viewport scaling");
            move(900, 500);
            require(draw() == halfway, "Pending item or progress bar followed the mouse away from its destination");
            const auto placed = draw(2.02);
            require(hudInventory.locationOf("sniper")->placement->storage == "body/left_shoulder_sling/carry" &&
                hudInventory.locationOf("sniper")->placement->rotation == 90,
                "GUI placement did not update the application's player inventory model");
            require(colorAt(placed, 1280, 720, 55, 230) != colorAt(initial, 1280, 720, 55, 230), "Rotated sniper was not placed in the shoulder sling");
            for (int y = 568; y < 690; ++y) for (int x = 34; x < 1246; ++x)
                require(colorAt(initial, 1280, 720, x, y) == colorAt(placed, 1280, 720, x, y), "Storage drag changed the hotbar");
            if (argc > 1) capture((std::string(argv[1]) + "-drag-placed.ppm").c_str(), placed, 1280, 720);
            // Same window fractions work after a 2x render-resolution change.
            clear(); hud.draw(state, 640, 360); checkState();
            move(74, 195); hud.pointerButton(0, true); hud.mouseWheel(-1);
            clear(); hud.draw(state, 640, 360); checkState();
            hud.pointerButton(1, true);
            clear(); hud.draw(state, 640, 360); checkState();
            require(draw() == placed, "Right-click failed to restore the previous completed placement");
            move(74, 195); hud.pointerButton(0, true); move(80, 205); draw();
            require(hudInventory.held().has_value(), "Dragging beyond the movement threshold must pick up the item");
            state.screen = viewer::HudScreen::Skills; draw();
            state.screen = viewer::HudScreen::Inventory;
            require(draw() == placed, "Leaving Inventory failed to cancel a pending drag");
        }
        // A registered empty definition must add a visible action without
        // changing the combo placeholders or the shared hotbar.
        {
            StorageFixture fixture;
            auto hudInventory = viewer::loadPlayerInventory(fixture.directory);
            state.screen = viewer::HudScreen::Skills;
            std::vector<unsigned char> original;
            {
                viewer::Hud hud(fixture.directory / "ui", hudInventory);
                clear(); hud.draw(state, 1280, 720); checkState();
                original = pixels(1280, 720);
                std::ofstream(fixture.directory / "skills.toml") << "[melee]\npunch='Punch'\nkick='Kick'\n";
                std::ofstream(fixture.directory / "skills/melee/kick.skill.toml");
                state.screen = viewer::HudScreen::Gameplay;
                clear(); hud.draw(state, 1280, 720); checkState();
                state.screen = viewer::HudScreen::Skills;
                clear(); hud.draw(state, 1280, 720); checkState();
                require(original == pixels(1280, 720), "Reopening Skills unexpectedly reloaded the catalog");
            }
            viewer::Hud hud(fixture.directory / "ui", hudInventory);
            clear(); hud.draw(state, 1280, 720); checkState();
            const auto configured = pixels(1280, 720);
            std::size_t actionChanges = 0;
            for (int y = 0; y < 720; ++y) for (int x = 0; x < 1280; ++x) {
                const bool same = colorAt(original, 1280, 720, x, y) == colorAt(configured, 1280, 720, x, y);
                if (x >= 54 && x < 380 && y >= 185 && y < 400) actionChanges += !same;
                else require(same, "Melee registration changed content outside the available skill list");
            }
            require(actionChanges > 300, "Newly registered melee skill did not appear in Skills");
        }
        // Use isolated configuration files to check that actual screen geometry
        // changes with selection, and that reopening a cached page performs no IO.
        {
            StorageFixture fixture;
            fixture.select("backpack='hiking_backpack'\nequipment=['jacket']\n");
            state.screen = viewer::HudScreen::Inventory;
            std::vector<unsigned char> configured;
            {
                auto hudInventory = viewer::loadPlayerInventory(std::filesystem::path(fixture.directory / "ui").parent_path());
                viewer::Hud hud(fixture.directory / "ui", hudInventory);
                clear(); hud.draw(state, 1280, 720); checkState();
                configured = pixels(1280, 720);
                if (argc > 1) capture((std::string(argv[1]) + "-hiking.ppm").c_str(), configured, 1280, 720);
                state.screen = viewer::HudScreen::Gameplay;
                clear(); hud.draw(state, 1280, 720); checkState();
                fixture.select("equipment=[]\n");
                state.screen = viewer::HudScreen::Inventory;
                clear(); hud.draw(state, 1280, 720); checkState();
                require(configured == pixels(1280, 720), "Reopening Inventory unexpectedly reloaded storage files");
            }
            auto emptyHudInventory = viewer::loadPlayerInventory(std::filesystem::path(fixture.directory / "ui").parent_path());
            viewer::Hud emptyHud(fixture.directory / "ui", emptyHudInventory);
            clear(); emptyHud.draw(state, 1280, 720); checkState();
            const auto empty = pixels(1280, 720);
            if (argc > 1) capture((std::string(argv[1]) + "-unequipped.ppm").c_str(), empty, 1280, 720);
            std::size_t bodyChanges = 0, backpackChanges = 0;
            for (int y = 0; y < 720; ++y) for (int x = 0; x < 1280; ++x) {
                const bool same = colorAt(configured, 1280, 720, x, y) == colorAt(empty, 1280, 720, x, y);
                if (y >= 568) require(same, "Storage configuration changed shared hotbar examples");
                if (y > 140 && y < 540 && !same) {
                    if (x > 34 && x < 424) ++bodyChanges;
                    if (x > 442 && x < 872) ++backpackChanges;
                }
            }
            require(bodyChanges > 100 && backpackChanges > 1000,
                    "Storage selections did not change rendered body compartments and backpack grid");
        }
        glDeleteSamplers(1, &sampler); glDeleteTextures(1, &texture); glDeleteTextures(1, &color);
        glDeleteTextures(1, &mapTexture);
        glDeleteBuffers(1, &buffer); glDeleteBuffers(1, &unpack); glDeleteVertexArrays(1, &vao);
        glDeleteFramebuffers(1, &framebuffer); glDeleteFramebuffers(1, &readFramebuffer);
        glDeleteRenderbuffers(1, &renderbuffer);
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    glfwDestroyWindow(window); glfwTerminate();
    return result;
}
