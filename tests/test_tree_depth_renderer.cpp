#include "renderer.hpp"
#include "tree_billboard_fixture.hpp"
#include <GLFW/glfw3.h>
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
constexpr int width = 320, height = 240;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
std::vector<unsigned char> pixels() {
    std::vector<unsigned char> result(width*height*3);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0,0,width,height,GL_RGB,GL_UNSIGNED_BYTE,result.data());
    return result;
}
viewer::Model maskCard() {
    viewer::Model model;
    viewer::Material material;
    material.unlit = true; material.doubleSided = true; material.alphaMode = "MASK";
    material.baseColorTexture = 0;
    model.materials.push_back(material);
    viewer::ImageData image; image.width = image.height = 2;
    image.rgba = {255,255,255,0, 255,255,255,255, 255,255,255,255, 255,255,255,255};
    model.images.push_back(image);
    viewer::Texture texture; texture.image = 0; texture.minFilter = texture.magFilter = GL_NEAREST;
    model.textures.push_back(texture);
    viewer::Primitive mesh; mesh.material = 0;
    const std::array<glm::vec2,4> corners{{{-1,-1},{1,-1},{1,1},{-1,1}}};
    for (const auto corner : corners) {
        viewer::Vertex vertex; vertex.position = {corner.x*3,3+corner.y*3,0};
        vertex.normal = {0,0,1}; vertex.uv = corner*.5f+.5f;
        mesh.vertices.push_back(vertex);
    }
    mesh.indices = {0,1,2,0,2,3};
    model.primitives.push_back(mesh); model.draws.push_back({0,glm::mat4(1),"card"});
    return model;
}
void check(viewer::Model model, glm::mat4 far, glm::mat4 near, viewer::Camera camera,
           viewer::TreeBillboardMode mode) {
    viewer::TreeLayer trees; trees.assets.push_back({"test",{},std::move(model)});
    trees.instances.push_back(billboard_test::instance(trees.assets[0].model,far));
    trees.instances.push_back(billboard_test::instance(trees.assets[0].model,near));
    viewer::Renderer renderer({},VIEWER_SHADER_DIR,&trees); renderer.resize(width,height);
    renderer.setHaze(false); renderer.setGroundFog(false); renderer.setTreeCanopyShading(false);
    renderer.setShadows(false);
    GLuint query; glGenQueries(1,&query);
    const auto renderSamples = [&](bool ordering) {
        renderer.setTreeDepthOrdering(ordering);
        glBeginQuery(GL_SAMPLES_PASSED,query);
        renderer.render(camera);
        glEndQuery(GL_SAMPLES_PASSED);
        GLuint samples = 0; glGetQueryObjectuiv(query,GL_QUERY_RESULT,&samples);
        return samples;
    };
    const auto oldSamples = renderSamples(false);
    const auto before = pixels(); const auto stats = renderer.treeStats();
    const auto newSamples = renderSamples(true);
    require(pixels()==before, "Depth ordering changed alpha holes or opaque foliage pixels");
    require(newSamples < oldSamples, "Near-first instances did not reduce depth-passing overdraw");
    const auto& after = renderer.treeStats();
    require(after.visible==stats.visible && after.drawCalls==stats.drawCalls && after.triangles==stats.triangles,
            "Depth ordering changed visibility, geometry or draw count");
    require((mode==viewer::TreeBillboardMode::Full && after.fullDetail==2) ||
            (mode==viewer::TreeBillboardMode::Side && after.sideBillboards==2) ||
            (mode==viewer::TreeBillboardMode::Top && after.topBillboards==2), "Fixture selected wrong tree LoD group");
    for (int scope=0;scope<3;++scope) {
        renderer.setShadows(true);
        renderer.setTreeDepthOrdering(false); renderer.render(camera); const auto original=pixels();
        const auto originalStats=renderer.treeStats();
        renderer.setTreeDepthOrdering(true); renderer.render(camera);
        require(pixels()==original, "Depth ordering changed scope or shadow output");
        require(renderer.treeStats().drawCalls==originalStats.drawCalls, "Ordering added scope draws");
        camera.cycleViewMode();
    }
    require(trees.instances[0].transform==far && trees.instances[1].transform==near,
            "Depth ordering changed authoritative transforms");
    viewer::Renderer::checkErrors("near-first tree depth ordering");
    glDeleteQueries(1,&query);
}
void checks() {
    viewer::Camera camera; camera.setPosition({0,3,0}); camera.setOrientation(-90,0);
    check(maskCard(),glm::translate(glm::mat4(1),{0,0,-40}),
          glm::scale(glm::translate(glm::mat4(1),{0,0,-8}),{-1,1,1}), camera,viewer::TreeBillboardMode::Full);
    camera.setPosition({0,10,0});
    check(billboard_test::asset(),glm::scale(glm::translate(glm::mat4(1),{0,0,-200}),glm::vec3(5)),
          glm::scale(glm::translate(glm::mat4(1),{0,0,-120}),glm::vec3(5)),camera,viewer::TreeBillboardMode::Side);
    camera.setPosition({0,300,0}); camera.setOrientation(-90,-89);
    check(billboard_test::asset(),glm::scale(glm::mat4(1),glm::vec3(5)),
          glm::scale(glm::translate(glm::mat4(1),{0,140,0}),glm::vec3(5)),camera,viewer::TreeBillboardMode::Top);
}
} // namespace
int main() {
    if (!glfwInit()) return 77;
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE); glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3); glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    auto* window=glfwCreateWindow(width,height,"Tree depth order",nullptr,nullptr);
    if (!window) { glfwTerminate(); return 77; }
    glfwMakeContextCurrent(window); int result=0;
    try { viewer::initializeOpenGL(); checks(); std::cout << "Tree depth ordering preserves pixels and reduces overdraw without extra draws\n"; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; result=1; }
    glfwDestroyWindow(window); glfwTerminate(); return result;
}
