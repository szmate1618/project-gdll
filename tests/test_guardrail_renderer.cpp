#include "guardrail_layer.hpp"
#include "model_merge.hpp"
#include "renderer.hpp"
#include "collision_world.hpp"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
constexpr int width = 1280, height = 720;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
viewer::Model fixture() {
    viewer::Model model;
    viewer::ImageData image;
    image.width = 256; image.height = 64;
    image.rgba.resize(256 * 64 * 4);
    for (int y = 0; y < 64; ++y) for (int x = 0; x < 256; ++x) {
        const auto offset = (y * 256 + x) * 4;
        image.rgba[offset] = image.rgba[offset + 1] = image.rgba[offset + 2] = 200;
        // Native runtime UVs sample row zero at V=0; the exporter flips PIL UVs.
        image.rgba[offset + 3] = y >= 38 || x < 5 || x >= 251 ? 255 : 0;
    }
    model.images.push_back(std::move(image)); model.textures.push_back({0});
    viewer::Material material;
    material.baseColorTexture = 0; material.doubleSided = true;
    material.alphaMode = "MASK"; material.unlit = true;
    model.materials.push_back(material);
    viewer::Primitive mesh;
    mesh.material = 0;
    const std::array<glm::vec3, 4> points{{{0, 0, 0}, {20, 0, 0}, {20, .75f, 0}, {0, .75f, 0}}};
    const std::array<glm::vec2, 4> uvs{{{0, 0}, {10, 0}, {10, 1}, {0, 1}}};
    for (int i = 0; i < 4; ++i) {
        viewer::Vertex vertex; vertex.position = points[i]; vertex.normal = {0, 0, 1}; vertex.uv = uvs[i];
        mesh.vertices.push_back(vertex);
    }
    mesh.indices = {0, 1, 2, 0, 2, 3};
    model.primitives.push_back(std::move(mesh));
    model.draws.push_back({0, glm::mat4(1), "guardrails_fixture"});
    model.boundsMin = {0, 0, 0}; model.boundsMax = {20, .75f, 0};
    return model;
}

void check(const std::filesystem::path& captures) {
    const auto model = fixture();
    const viewer::CollisionWorld collision(model);
    require(!collision.lineOfSight({10, .4f, 1}, {10, .4f, -1}), "Guardrail strip must block sight");
    require(collision.lineOfSight({10, 2, 1}, {10, 2, -1}), "Space above guardrail must remain clear");
    require(!collision.contacts({10, 0, .1f}, .3f, 1.8f).empty(), "Guardrail must block walking from either side");
    require(!collision.groundAt(10, 0, -1, 2, true), "Guardrail must not become terrain ground");
    viewer::Renderer renderer(model, VIEWER_SHADER_DIR);
    renderer.resize(width, height); renderer.setShadows(false); renderer.setHaze(false); renderer.setGroundFog(false);
    viewer::Camera camera;
    camera.setPosition({10, .4f, 10}); camera.look(0, 200);
    for (int side = 0; side < 2; ++side) {
        renderer.render(camera);
        require(renderer.sceneStats().drawCalls == 1, "Guardrail must use one batched draw");
        const auto pv = camera.projection(float(width) / height) * camera.view();
        const auto pixel = [&](glm::vec3 point) {
            const auto clip = pv * glm::vec4(point, 1);
            const auto screen = (glm::vec2(clip) / clip.w * .5f + .5f) * glm::vec2(width, height);
            std::array<unsigned char, 3> rgb{};
            glReadBuffer(GL_COLOR_ATTACHMENT0);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(static_cast<int>(screen.x), static_cast<int>(screen.y), 1, 1, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());
            return rgb;
        };
        require(pixel({11, .65f, 0})[0] > 180, "Opaque horizontal rail missing");
        require(pixel({10, .15f, 0})[0] > 180, "Textured post missing below rail");
        require(pixel({11, .15f, 0}) == pixel({11, 1.5f, 0}), "Transparent area below rail must show the clear background");
        GLboolean depth = GL_FALSE; glGetBooleanv(GL_DEPTH_WRITEMASK, &depth);
        require(depth && !glIsEnabled(GL_BLEND), "Guardrail rendering leaked depth/blend state");
        viewer::Renderer::checkErrors("guardrail texture and back face");
        if (!captures.empty()) renderer.writeScreenshot(captures / (side ? "guardrail-back.ppm" : "guardrail-front.ppm"));
        camera.setPosition({10, .4f, -10}); camera.look(1800, 0);
    }
    std::cout << "Minimal guardrail strip renders opaque rail/posts and transparent gaps from both sides\n";
}

void actualMap(const std::filesystem::path& captures, const std::filesystem::path& map,
               const std::filesystem::path& layerPath) {
    auto town = viewer::loadModel(map);
    auto layer = viewer::loadGuardrailLayer(layerPath, map);
    require(!layer.model.draws.empty(), "Actual map has no guardrails to capture");
    viewer::Camera camera;
    camera.frame(layer.model.boundsMin, layer.model.boundsMax, float(width) / height);
    std::cout << "Actual map: " << layer.ways << " ways, " << layer.lengthMeters << " m; "
              << layer.model.draws.size() << " batches\n";
    {
        viewer::Renderer before(town, VIEWER_SHADER_DIR);
        before.resize(width, height); before.setHaze(false); before.setGroundFog(false); before.render(camera);
        before.writeScreenshot(captures / "town-without-guardrails.ppm");
    }
    viewer::appendStaticModel(town, std::move(layer.model));
    viewer::Renderer after(town, VIEWER_SHADER_DIR);
    after.resize(width, height); after.setHaze(false); after.setGroundFog(false); after.render(camera);
    after.writeScreenshot(captures / "town-with-guardrails.ppm");
    viewer::Renderer::checkErrors("actual map guardrails");
}
} // namespace

int main(int argc, char** argv) {
    if (!glfwInit()) return 77;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    auto* window = glfwCreateWindow(width, height, "Guardrails", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 77; }
    glfwMakeContextCurrent(window);
    int result = 0;
    try {
        std::cout << "Renderer: " << glGetString(GL_RENDERER) << '\n';
        const auto captures = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path{};
        check(captures);
        if (argc == 4) actualMap(captures, argv[2], argv[3]);
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    glfwDestroyWindow(window); glfwTerminate();
    return result;
}
