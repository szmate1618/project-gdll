#include "renderer.hpp"

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
constexpr int width = 480, height = 360;
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

void quad(viewer::Model& model, const std::array<glm::vec3, 4>& positions,
          glm::vec3 normal, int material, const char* name, glm::mat4 transform = glm::mat4(1)) {
    viewer::Primitive mesh;
    mesh.material = material;
    const std::array<glm::vec2, 4> uvs{{{0, 0}, {0, 1}, {1, 1}, {1, 0}}};
    for (std::size_t i = 0; i < positions.size(); ++i) {
        viewer::Vertex vertex;
        vertex.position = positions[i];
        vertex.normal = normal;
        vertex.uv = uvs[i];
        vertex.color = {.6f + .1f * i, .7f, .8f, 1};
        mesh.vertices.push_back(vertex);
    }
    mesh.indices = {0, 1, 2, 0, 2, 3};
    model.draws.push_back({model.primitives.size(), transform, name});
    model.primitives.push_back(mesh);
}

viewer::Model fixture() {
    viewer::Model model;
    model.materials.resize(2);
    model.materials[1].baseColorTexture = 0;
    // Single-sided roofs exercise reflection/winding rather than hiding errors.
    viewer::ImageData image;
    image.width = image.height = 2;
    image.rgba = {220, 40, 30, 255, 30, 180, 40, 255, 40, 50, 210, 255, 210, 180, 40, 255};
    model.images.push_back(image);
    viewer::Texture texture;
    texture.image = 0;
    texture.minFilter = texture.magFilter = GL_NEAREST;
    model.textures.push_back(texture);
    quad(model, {{{-10, 0, -12}, {-10, 0, 12}, {16, 0, 12}, {16, 0, -12}}}, {0, 1, 0}, 0, "ground");
    const std::array<glm::vec3, 4> roof{{{1, 2, -3}, {1, 2, -1}, {3, 2, -1}, {3, 2, -3}}};
    quad(model, roof, {0, 1, 0}, 1, "building_a_roof");
    glm::mat4 mirrored(1);
    mirrored[0][0] = -1.25f;
    mirrored[1][1] = 1.5f;
    mirrored[2][0] = .25f;
    mirrored[3][0] = 8;
    quad(model, roof, {0, 1, 0}, 1, "building_b_roof", mirrored);
    quad(model, roof, {0, 1, 0}, 0, "building_c_roof", glm::translate(glm::mat4(1), {8, 0, 0}));
    // Behind the camera, but its shadow falls onto the visible ground.
    quad(model, {{{1, 3, 7}, {5, 3, 7}, {5, 5, 7}, {1, 5, 7}}}, {0, 0, 1}, 0, "building_offscreen_roof");
    return model;
}

std::vector<unsigned char> pixels() {
    std::vector<unsigned char> result(width * height * 3);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, result.data());
    return result;
}

void compare(const std::filesystem::path& captures) {
    const auto model = fixture();
    viewer::Renderer renderer(model, VIEWER_SHADER_DIR);
    renderer.resize(width, height);
    renderer.setHaze(false);
    renderer.setGroundFog(false);
    viewer::SunLighting light;
    light.direction = {0, 1, 1};
    renderer.setLighting(light);
    viewer::Camera camera;
    camera.setPosition({4, 8, 6});
    camera.look(0, -250); // Pitch -57.5 degrees.
    std::vector<unsigned char> lit, shadowed;
    for (const bool shadows : {false, true}) {
        renderer.setShadows(shadows);
        for (const bool culling : {true, false}) {
            renderer.setSceneCulling(culling);
            for (int scope = 0; scope < 3; ++scope) {
                renderer.setRoofBatching(false);
                renderer.render(camera);
                const auto before = pixels();
                const auto originalCalls = renderer.sceneStats().drawCalls;
                const auto originalVisible = renderer.sceneStats().visible;
                renderer.setRoofBatching(true);
                renderer.render(camera);
                const auto after = pixels();
                require(renderer.sceneStats().visible == originalVisible, "Batching changed source visibility statistics");
                std::size_t differences = 0;
                for (std::size_t i = 0; i < before.size(); ++i)
                    if (std::abs(int(before[i]) - int(after[i])) > 3) ++differences;
                require(differences < before.size() / 1000,
                        "Roof batching changed textures, colors, normals, culling, or shadows");
                if (scope == 0) {
                    require(renderer.sceneStats().drawCalls < originalCalls, "Compatible roofs did not reduce draw calls");
                    if (culling) {
                        if (shadows) shadowed = after;
                        else lit = after;
                        if (!captures.empty()) renderer.writeScreenshot(captures / (shadows ? "roof-shadows.ppm" : "roof-color.ppm"));
                    }
                }
                viewer::Renderer::checkErrors("roof batching comparison");
                camera.cycleViewMode();
            }
        }
    }
    std::size_t darker = 0;
    for (std::size_t i = 0; i < lit.size(); ++i)
        if (int(lit[i]) - int(shadowed[i]) > 30) ++darker;
    require(darker > 100, "Fixture did not exercise roof shadows");
    const viewer::Frustum frustum(camera.projection(float(width) / height) * camera.view());
    require(!frustum.intersects({1, 3, 7}, {5, 5, 7}), "Fixture shadow caster must be outside the camera view");
    std::cout << "Roof batching preserves pixels and reduces draws across culling, scopes and shadows\n";
}
} // namespace

int main(int argc, char** argv) {
    if (!glfwInit()) return 77;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    auto* window = glfwCreateWindow(width, height, "Roof batching", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 77; }
    glfwMakeContextCurrent(window);
    int result = 0;
    try { compare(argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path{}); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
