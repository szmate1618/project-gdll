#include "client/menu_view.hpp"
#include "client/character_creation.hpp"
#include "client/character_profile.hpp"
#include "gui/hud.hpp"
#include "inventory/inventory_config.hpp"
#include "opengl.hpp"
#include <GLFW/glfw3.h>
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
std::array<unsigned char, 3> color(int x, int y) {
    std::array<unsigned char, 3> pixel{};
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(x, 599 - y, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, pixel.data());
    return pixel;
}
bool near(std::array<unsigned char, 3> actual, std::array<int, 3> expected) {
    for (int i = 0; i < 3; ++i) if (std::abs(int(actual[i]) - expected[i]) > 2) return false;
    return true;
}
} // namespace

int main(int argc, char** argv) {
    if (!glfwInit()) return 77;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    auto* window = glfwCreateWindow(960, 600, "Main menu regression", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 77; }
    glfwMakeContextCurrent(window);
    int result = 0;
    try {
        viewer::initializeOpenGL();
        std::cout << "OpenGL renderer: " << glGetString(GL_RENDERER) << '\n';
        const std::filesystem::path ui = VIEWER_HUD_DIR;
        std::unique_ptr<multiplayer::CharacterCreation> creation; // Must outlive the borrowed editor binding.
        multiplayer::MenuView menu(ui);
        const auto draw = [&](multiplayer::MenuScreen screen = multiplayer::MenuScreen::Choose) {
            menu.draw(screen, "Status <&> stays literal", "TEST FACE", 960, 600);
        };
        const auto key = [&](int code, int mods = 0) {
            menu.key(code, GLFW_PRESS, mods); menu.key(code, GLFW_RELEASE, mods);
        };
        const auto type = [&](const char* text) { for (; *text; ++text) menu.character(*text); };
        draw();
        require(near(color(5, 5), {6, 10, 14}), "Classic must be the initial theme");
        // Original G at four pixels per cell. Validate the rendered font against
        // its visible pattern, independently of the font writer and JSON table.
        const std::array<const char*, 7> letter{".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".####"};
        for (int y = 0; y < 7; ++y) for (int x = 0; x < 5; ++x)
            require(near(color(62 + x * 4, 57 + y * 4), letter[y][x] == '#' ?
                         std::array<int, 3>{217, 230, 240} : std::array<int, 3>{6, 10, 14}),
                    "Classic title must retain the original pixel glyph");
        key(GLFW_KEY_UP); key(GLFW_KEY_ENTER); draw();
        require(menu.takeCommand() == multiplayer::MenuCommand::Quit, "Arrow navigation must wrap");
        key(GLFW_KEY_TAB); key(GLFW_KEY_BACKSPACE); draw();
        require(menu.port() == "2702", "Tab must place the caret after the existing field value");
        key(GLFW_KEY_A, GLFW_MOD_CONTROL); type("34567"); draw();
        require(menu.port() == "34567", "Port input must support select-all and typing");
        require(menu.takeCommand() == multiplayer::MenuCommand::None, "Typing digits must not activate a game mode");
        key(GLFW_KEY_F6); draw();
        require(near(color(5, 5), {43, 45, 47}), "F6 must select the modern theme");
        require(menu.port() == "34567", "Theme change must preserve input values");
        key(GLFW_KEY_HOME); key(GLFW_KEY_DELETE); draw();
        require(menu.port() == "4567", "Theme change must preserve input focus and cursor editing");
        key(GLFW_KEY_TAB); key(GLFW_KEY_A, GLFW_MOD_CONTROL); type("127.0.0.1:12345");
        key(GLFW_KEY_ENTER); key(GLFW_KEY_3); draw();
        require(menu.address() == "127.0.0.1:12345", "Join input must preserve address punctuation");
        require(menu.takeCommand() == multiplayer::MenuCommand::Join, "Enter must end field editing before mode shortcuts");
        // The menu context and font must survive repeated HUD creation and
        // loading feedback rendered while both contexts exist.
        auto inventory = viewer::loadPlayerInventory(ui.parent_path());
        for (int session = 0; session < 2; ++session) {
            viewer::Hud hud(ui, inventory);
            hud.draw(viewer::HudState{}, 960, 600);
            draw(multiplayer::MenuScreen::Loading);
            key(GLFW_KEY_ESCAPE); draw(multiplayer::MenuScreen::Loading);
            require(menu.takeCommand() == multiplayer::MenuCommand::Cancel, "Loading must remain cancellable beside the HUD");
        }
        draw();
        require(near(color(5, 5), {43, 45, 47}), "Returning from gameplay must retain the modern theme");
        require(menu.address() == "127.0.0.1:12345", "HUD teardown must preserve menu fields");
        menu.pointer(100.f / 960, 160.f / 600); menu.button(0, true, 0); menu.button(0, false, 0); draw();
        require(menu.takeCommand() == multiplayer::MenuCommand::SinglePlayer, "RmlUi buttons must handle pointer clicks");
        draw(multiplayer::MenuScreen::Character);
        const auto preview = menu.previewViewport();
        require(preview.x > 300 && preview.y >= 0 && preview.z > 250 && preview.w > 300 &&
                preview.x + preview.z <= 960 && preview.y + preview.w <= 600,
                "Character viewport must come from the visible layout");
        key(GLFW_KEY_ESCAPE); draw(multiplayer::MenuScreen::Character);
        require(menu.takeCommand() == multiplayer::MenuCommand::Back, "Character preview must support Escape");
        const auto directory = std::filesystem::path(argc > 1 ? argv[1] : "build") /
            ("editor-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        const auto profile = directory / "character.json";
        creation = std::make_unique<multiplayer::CharacterCreation>(profile);
        menu.editCharacter(*creation);
        draw(multiplayer::MenuScreen::Character);
        require(creation->faces().size() == 20, "Editor must expose all 20 face icons");
        // Edit three native color fields, then select the final icon through its
        // visible grid position (four columns, five rows).
        for (const char* value : {"123456", "ABCDEF", "FEDCBA"}) {
            key(GLFW_KEY_TAB); key(GLFW_KEY_A, GLFW_MOD_CONTROL); type(value); draw(multiplayer::MenuScreen::Character);
        }
        require(creation->color(multiplayer::CharacterColorTarget::Base) == multiplayer::CharacterCanvas::Color{0x12,0x34,0x56,255} &&
                creation->color(multiplayer::CharacterColorTarget::Brush) == multiplayer::CharacterCanvas::Color{0xab,0xcd,0xef,255} &&
                creation->color(multiplayer::CharacterColorTarget::Face) == multiplayer::CharacterCanvas::Color{0xfe,0xdc,0xba,255},
                "Base, brush and face inputs must control independent colors");
        key(GLFW_KEY_A, GLFW_MOD_CONTROL); type("ZZZZZZ"); draw(multiplayer::MenuScreen::Character);
        const auto click = [&](float x, float y) {
            menu.pointer(x / 960, y / 600); menu.button(0, true, 0); menu.button(0, false, 0);
            draw(multiplayer::MenuScreen::Character);
        };
        click(840, 538);
        require(!std::filesystem::exists(profile), "Invalid color input must prevent saving");
        // A preset repairs the field without changing the other two selectors.
        click(60 + 16 + 3 * 26 + 10, 130 + 216 + 52 + 10);
        require(creation->color(multiplayer::CharacterColorTarget::Face) == multiplayer::CharacterCanvas::Color{245,246,242,255},
                "Color presets must update their selected role");
        click(660 + 16 + 3 * 52 + 23, 130 + 48 + 4 * 52 + 23);
        require(creation->faceIndex() == 19, "The last of 20 face icons must be clickable");
        click(840, 538);
        require(std::filesystem::is_regular_file(profile), "Save button must persist the local character");
        {
            multiplayer::CharacterCreation restored(profile);
            require(restored.faceIndex() == 19 && restored.color(multiplayer::CharacterColorTarget::Base) == creation->color(multiplayer::CharacterColorTarget::Base),
                    "A new editor must restore the saved face and colors");
        }
        click(250, 538);
        require(creation->faceIndex() == 0 && creation->color(multiplayer::CharacterColorTarget::Base) == multiplayer::CharacterCanvas::baseColor,
                "Reset button must restore the default appearance");
        multiplayer::CharacterCanvas saved; std::string savedFace;
        multiplayer::loadCharacterProfile(profile, saved, savedFace);
        require(savedFace == creation->faces()[19].id, "Reset must leave the previous save intact until Save is clicked");
        if (argc > 2) {
            // Read the GPU buffer directly for dependable visual QA captures,
            // independent of compositor/front-buffer screenshot timing.
            const auto capture = [&](const char* suffix) {
                glDisable(GL_SCISSOR_TEST); glDepthMask(GL_TRUE);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                draw(multiplayer::MenuScreen::Character);
                creation->draw(menu.previewViewport(), {-1,-1}, false, false);
                std::vector<unsigned char> image(960 * 600 * 3);
                glPixelStorei(GL_PACK_ALIGNMENT, 1);
                glReadPixels(0, 0, 960, 600, GL_RGB, GL_UNSIGNED_BYTE, image.data());
                std::ofstream output(std::string(argv[2]) + suffix + ".ppm", std::ios::binary);
                output << "P6\n960 600\n255\n";
                for (int y = 599; y >= 0; --y) output.write(reinterpret_cast<const char*>(image.data() + y * 960 * 3), 960 * 3);
                require(bool(output), "Cannot save editor QA capture");
            };
            capture("-modern"); key(GLFW_KEY_F6); capture("-classic"); key(GLFW_KEY_F6); draw(multiplayer::MenuScreen::Character);
        }
        std::filesystem::remove_all(directory);
        draw(); key(GLFW_KEY_F6); draw();
        require(near(color(5, 5), {6, 10, 14}), "Classic must remain available after sessions and character preview");
        // A wider framebuffer moves the centered layout; hit testing must use
        // the same native framebuffer coordinates as rendering.
        menu.draw(multiplayer::MenuScreen::Choose, "", "", 1500, 600);
        menu.pointer(370.f / 1500, 160.f / 600); menu.button(0, true, 0); menu.button(0, false, 0);
        menu.draw(multiplayer::MenuScreen::Choose, "", "", 1500, 600);
        require(menu.takeCommand() == multiplayer::MenuCommand::SinglePlayer, "Resized menu hit testing must follow its layout");
        require(glGetError() == GL_NO_ERROR, "Menu/HUD transitions must not introduce GL errors");
        std::cout << "Menu themes, original glyphs, editing, pointers, resize and HUD lifetime passed.\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    glfwDestroyWindow(window); glfwTerminate();
    return result;
}
