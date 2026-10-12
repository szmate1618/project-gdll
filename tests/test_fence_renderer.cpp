#include "renderer.hpp"

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
constexpr int width = 960, height = 720;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

viewer::Model fence(bool railing, bool tuned, float distance, float angle, bool densePosts) {
    viewer::Model model;
    viewer::ImageData image;
    image.width = 2048;
    image.height = 128;
    image.rgba.resize(static_cast<std::size_t>(image.width) * image.height * 4, 255);
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            // Representative .25 m wires/bars and .08 m posts at 2.5 m.
            const float u = static_cast<float>(x) / image.width;
            const float v = static_cast<float>(y) / image.height;
            const auto onWire = [](float coordinate) {
                return std::abs(coordinate - std::round(coordinate)) < .022f;
            };
            const float halfPost = densePosts ? .08f : .016f;
            const bool post = u < halfPost || u > 1 - halfPost;
            const bool pattern = railing ? (std::abs(u * 10 - std::floor(u * 10) - .5f) < .04f ||
                                            (v > .09f && v < .15f) || (v > .81f && v < .87f))
                                         : onWire(u * 10 + v) || onWire(u * 10 - v);
            image.rgba[(static_cast<std::size_t>(y) * image.width + x) * 4 + 3] = post || pattern ? 255 : 0;
        }
    }
    model.images.push_back(std::move(image));
    model.textures.push_back({0});
    viewer::Material material;
    material.baseColor = {1, 0, 0, 1};
    material.baseColorTexture = 0;
    material.doubleSided = true;
    material.unlit = true;
    material.alphaMode = "MASK";
    material.minifiedAlphaCutoff = tuned ? (railing ? .3f : .2f) : 0;
    model.materials.push_back(material);
    viewer::Primitive mesh;
    mesh.material = 0;
    // A two-metre gate stays a physical hole, even with the relaxed cutoff.
    for (const auto span : {glm::vec2(-5, -1), glm::vec2(1, 5)}) {
        const auto first = static_cast<std::uint32_t>(mesh.vertices.size());
        for (const auto corner : {glm::vec2(span.x, -.9f), glm::vec2(span.y, -.9f),
                                  glm::vec2(span.y, .9f), glm::vec2(span.x, .9f)}) {
            viewer::Vertex vertex;
            vertex.position = {corner.x, corner.y, 0};
            vertex.normal = {0, 0, 1};
            vertex.uv = {(corner.x + 5) / 2.5f, railing ? (corner.y + .9f) / 1.8f : (corner.y + .9f) / .25f};
            mesh.vertices.push_back(vertex);
        }
        for (const auto index : {0u, 1u, 2u, 0u, 2u, 3u}) mesh.indices.push_back(first + index);
    }
    model.primitives.push_back(mesh);
    const auto transform = glm::rotate(glm::translate(glm::mat4(1), {0, 0, -distance}),
                                       glm::radians(angle), glm::vec3(0, 1, 0));
    model.draws.push_back({0, transform, "fence"});
    return model;
}

struct Coverage { int pixels = 0, area = 0; };

Coverage render(bool railing, bool tuned, float distance, float angle,
                const std::filesystem::path& screenshot = {}, bool densePosts = false) {
    const auto model = fence(railing, tuned, distance, angle, densePosts);
    viewer::Renderer renderer(model, VIEWER_SHADER_DIR);
    renderer.setHaze(false);
    renderer.setGroundFog(false);
    renderer.setShadows(false);
    renderer.resize(width, height);
    viewer::Camera camera;
    camera.setPosition({distance < 1 ? 3.f : 0.f, 0, 0});
    camera.look(0, 200); // Face the fence at zero pitch.
    renderer.render(camera);
    const auto projectionView = camera.projection(static_cast<float>(width) / height) * camera.view();
    glm::vec2 lo(width, height), hi(0);
    for (const auto& vertex : model.primitives[0].vertices) {
        const auto clip = projectionView * model.draws[0].transform * glm::vec4(vertex.position, 1);
        const auto screen = (glm::vec2(clip) / clip.w * .5f + .5f) * glm::vec2(width, height);
        lo = glm::min(lo, screen);
        hi = glm::max(hi, screen);
    }
    const int left = std::clamp(static_cast<int>(std::ceil(lo.x)), 0, width - 1);
    const int right = std::clamp(static_cast<int>(std::floor(hi.x)), left + 1, width);
    const int bottom = std::clamp(static_cast<int>(std::ceil(lo.y)), 0, height - 1);
    const int top = std::clamp(static_cast<int>(std::floor(hi.y)), bottom + 1, height);
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 3);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    const auto red = [&](int x, int y) {
        const auto offset = (static_cast<std::size_t>(y) * width + x) * 3;
        return pixels[offset] > 220 && pixels[offset + 1] < 20;
    };
    if (distance > 1)
        require(!red(width / 2, height / 2), "Fence filtering obstructed the physical gate gap");
    Coverage coverage{0, (right - left) * (top - bottom)};
    for (int y = bottom; y < top; ++y)
        for (int x = left; x < right; ++x) coverage.pixels += red(x, y);
    GLboolean depth = GL_FALSE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depth);
    require(depth == GL_TRUE && !glIsEnabled(GL_BLEND), "Fence filtering changed mask depth/blend state");
    viewer::Renderer::checkErrors("thin fence alpha filtering");
    if (!screenshot.empty()) renderer.writeScreenshot(screenshot);
    return coverage;
}

void check(const std::filesystem::path& captures) {
    for (const bool railing : {false, true}) {
        const auto close = render(railing, false, .25f, 0);
        const auto closeTuned = render(railing, true, .25f, 0);
        require(close.pixels > 100, "Close-up check did not render the fence mask");
        require(close.pixels == closeTuned.pixels, "Fence filtering changed the magnified mask");
        for (const float angle : {0.f, 60.f}) {
            for (const float distance : {20.f, 30.f}) {
                const bool capture = !railing && angle == 0 && distance == 20 && !captures.empty();
                const auto baseline = render(railing, false, distance, angle,
                    capture ? captures / "fence-alpha-before.ppm" : std::filesystem::path{});
                const auto tuned = render(railing, true, distance, angle,
                    capture ? captures / "fence-alpha-after.ppm" : std::filesystem::path{});
                std::cout << (railing ? "railing" : "wire") << " at " << distance << " m, " << angle
                          << " degrees: " << baseline.pixels << " -> " << tuned.pixels << " visible pixels\n";
                require(tuned.pixels > baseline.pixels * 1.2, "Distant fence did not gain visible coverage");
                require(tuned.pixels < tuned.area * .4, "Distant fence became a solid sheet");
            }
        }
        const auto far = render(railing, true, 600, 0);
        require(far.pixels < far.area * .4, "Coarsest fence mips became opaque");
        const auto denseFar = render(railing, true, 600, 0, {}, true);
        require(denseFar.pixels < denseFar.area * .4, "Denser fence patterns became opaque at distance");
    }
}
} // namespace

int main(int argc, char** argv) {
    if (!glfwInit()) { std::cout << "No display; skipping fence graphics check\n"; return 77; }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    auto* window = glfwCreateWindow(width, height, "Fence filtering", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 77; }
    glfwMakeContextCurrent(window);
    int result = 0;
    try { check(argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path{}); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
