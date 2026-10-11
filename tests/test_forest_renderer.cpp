#include "renderer.hpp"
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition,const char* message) {if (!condition) throw std::runtime_error(message);}
viewer::Model treeAsset() {
    viewer::Model model;
    viewer::Material material; material.baseColor={0,1,0,1};
    material.doubleSided=true; material.unlit=true; material.alphaMode="MASK";
    model.materials.push_back(material);
    viewer::Primitive mesh; mesh.material=0;
    for (const auto p:{glm::vec3(-5,0,0),glm::vec3(5,0,0),glm::vec3(5,10,0),glm::vec3(-5,10,0)}) {
        viewer::Vertex v; v.position=p; v.normal={0,0,1}; mesh.vertices.push_back(v);
    }
    mesh.indices={0,1,2,0,2,3}; model.primitives.push_back(mesh); model.draws.push_back({0,glm::mat4(1),"tree"});
    return model;
}
std::array<unsigned char,3> center() {
    std::array<unsigned char,3> pixels{}; glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(320,240,1,1,GL_RGB,GL_UNSIGNED_BYTE,pixels.data()); return pixels;
}
void checkSwitching() {
    viewer::TreeLayer trees; trees.assets.push_back({"tree",{},treeAsset()});
    viewer::TreeInstance instance; instance.transform=glm::translate(glm::mat4(1),{0,0,-110});
    instance.boundsMin={-5,0,-110.01f}; instance.boundsMax={5,10,-109.99f}; trees.instances.push_back(instance);
    auto detected = instance;
    detected.transform = glm::translate(glm::mat4(1), {3,0,-110});
    detected.boundsMin.x += 3;
    detected.boundsMax.x += 3;
    trees.instances.push_back(detected);
    viewer::ForestLayer forest; forest.detailDistance=30;
    const viewer::ForestPolygon polygon{{{-10,100},{10,100},{10,120},{-10,120},{-10,100}}, {}};
    const auto meshes=viewer::forestVolume(polygon,[](glm::dvec2){return std::optional<float>(0);},10,8);
    forest.volumes.primitives={meshes.first,meshes.second};
    viewer::Material red; red.unlit=true; red.baseColor={1,0,0,1};
    forest.volumes.materials={red,red};
    forest.volumes.draws={{0,glm::mat4(1),"walls"},{1,glm::mat4(1),"top"}};
    forest.patches.push_back({"A",{{-10,0,-120},{10,10,-100}},0,1,0,2,{1}});
    viewer::Renderer renderer({},VIEWER_SHADER_DIR,&trees,nullptr,&forest);
    renderer.setShadows(false); renderer.setHaze(false); renderer.setGroundFog(false); renderer.resize(640,480);
    viewer::Camera camera; camera.setPosition({0,5,0}); camera.look(0,200);
    const auto render=[&](bool detailed) {
        renderer.render(camera);
        const auto pixel=center();
        require(detailed ? pixel[1]>250 && pixel[0]<2 : pixel[0]>250 && pixel[1]<2,
                "LoD rendered the wrong geometry at the camera centre");
        require(renderer.treeStats().visible==(detailed?2u:0u), "Volume and generated/detected trees drawn together or trees missing");
        require(renderer.forestStats().volumes==(detailed?0u:1u), "Wrong forest volume count");
        GLboolean depth=GL_FALSE; glGetBooleanv(GL_DEPTH_WRITEMASK,&depth);
        GLint front=0; glGetIntegerv(GL_FRONT_FACE,&front);
        require(depth==GL_TRUE && front==GL_CCW, "Forest draw leaked depth/winding state");
        viewer::Renderer::checkErrors("forest LoD switching");
    };
    render(false);
    camera.cycleViewMode(); render(true); // 10-degree scope.
    camera.cycleViewMode(); render(true); // 1-degree scope.
    camera.cycleViewMode(); render(false);
    renderer.setTreeCulling(false); render(false); // Culling bypass preserves LoD.
    camera.setPosition({0,5,-90}); render(true);
    renderer.setShadows(true); render(true); // Shadow pass does not alter color selection.
}
}
int main() {
    if (!glfwInit()) return 77;
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE); glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3); glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    auto* window=glfwCreateWindow(640,480,"forest regression",nullptr,nullptr);
    if (!window) {glfwTerminate(); return 77;}
    glfwMakeContextCurrent(window);
    int code=0;
    try {checkSwitching(); std::cout<<"Forest GPU LoD regression passed\n";}
    catch (const std::exception& e) {std::cerr<<e.what()<<'\n'; code=1;}
    glfwDestroyWindow(window); glfwTerminate(); return code;
}
