#include "pole_layer.hpp"
#include "renderer.hpp"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
constexpr int width = 800, height = 600;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
std::vector<unsigned char> pixels() {
    std::vector<unsigned char> result(width * height * 3);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, result.data());
    return result;
}

void compare(const std::filesystem::path& captures) {
    std::vector<viewer::PoleColumn> columns;
    for (int i = 0; i < 10; ++i) columns.push_back({std::to_string(i), {3.f + i, 0, 10}, 7, .2f,
        i % 2 ? viewer::PoleKind::hevCatenary : viewer::PoleKind::mapped});
    const auto batches = viewer::buildPoleBatches(columns);
    viewer::Model individual;
    for (const auto& column : columns)
        viewer::appendPoleLayer(individual, {{column}, viewer::buildPoleBatches({column})});
    viewer::Renderer batched(batches, VIEWER_SHADER_DIR), baseline(individual, VIEWER_SHADER_DIR);
    batched.resize(width, height); baseline.resize(width, height);
    batched.setHaze(false); baseline.setHaze(false);
    batched.setGroundFog(false); baseline.setGroundFog(false);
    viewer::Camera camera;
    camera.frame(batches.boundsMin, batches.boundsMax, float(width) / height);
    for (const bool shadows : {false, true}) {
        batched.setShadows(shadows); baseline.setShadows(shadows);
        for (const bool culling : {false, true}) {
            batched.setSceneCulling(culling); baseline.setSceneCulling(culling);
            baseline.render(camera);
            const auto before = pixels();
            batched.render(camera);
            const auto after = pixels();
            std::size_t differences = 0;
            for (std::size_t i = 0; i < before.size(); ++i)
                if (std::abs(int(before[i]) - int(after[i])) > 2) ++differences;
            require(differences < before.size() / 1000, "Pole batching changed rendered pixels");
            require(batched.sceneStats().drawCalls == 2 && baseline.sceneStats().drawCalls == 10,
                    "Ten columns with two colors must render in two batched draws");
            viewer::Renderer::checkErrors("pole batching comparison");
        }
    }
    if (!captures.empty()) batched.writeScreenshot(captures / "pole-columns.ppm");
    std::cout << "Pole batching preserves both colors with culling/shadows and reduces ten draws to two\n";
}

void actualMap(const std::filesystem::path& captures, const std::filesystem::path& map,
               const std::filesystem::path& manifest) {
    auto town = viewer::loadModel(map);
    auto poles = viewer::loadPoleLayer(manifest, map);
    require(!poles.columns.empty(), "Actual map requires mapped poles");
    const auto selected = std::min_element(poles.columns.begin(), poles.columns.end(), [](const auto& a, const auto& b) {
        if (a.kind != b.kind) return a.kind == viewer::PoleKind::hevCatenary;
        return glm::length(glm::vec2(a.position.x, a.position.z)) < glm::length(glm::vec2(b.position.x, b.position.z));
    });
    const auto p = selected->position;
    std::cout << "Actual map: " << poles.columns.size() << " columns in " << poles.batches.draws.size()
              << " batches; capture near " << selected->id << '\n';
    viewer::Camera camera;
    camera.frame(p - glm::vec3(3, 0, 3), p + glm::vec3(3, selected->height, 3), float(width) / height);
    {
        viewer::Renderer before(town, VIEWER_SHADER_DIR);
        before.resize(width, height);
        before.setHaze(false); before.setGroundFog(false);
        before.render(camera);
        before.writeScreenshot(captures / "town-without-poles.ppm");
    }
    viewer::appendPoleLayer(town, std::move(poles));
    viewer::Renderer after(town, VIEWER_SHADER_DIR);
    after.resize(width, height);
    after.setHaze(false); after.setGroundFog(false);
    after.render(camera);
    after.writeScreenshot(captures / "town-with-poles.ppm");
    viewer::Renderer::checkErrors("actual map poles");
}
} // namespace

int main(int argc, char** argv) {
    if (!glfwInit()) return 77;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    auto* window = glfwCreateWindow(width, height, "Pole batching", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 77; }
    glfwMakeContextCurrent(window);
    int result = 0;
    try {
        std::cout << "Renderer: " << glGetString(GL_RENDERER) << '\n';
        const auto captures = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path{};
        compare(captures);
        if (argc == 4) actualMap(captures, argv[2], argv[3]);
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    glfwDestroyWindow(window); glfwTerminate();
    return result;
}
