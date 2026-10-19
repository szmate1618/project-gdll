#include "renderer.hpp"
#include "tree_billboard_fixture.hpp"
#include <GLFW/glfw3.h>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
viewer::Camera camera(glm::vec3 eye,glm::vec3 target) {
    viewer::Camera result;result.setPosition(eye);const auto direction=glm::normalize(target-eye);
    result.setOrientation(glm::degrees(std::atan2(direction.z,direction.x)),glm::degrees(std::asin(direction.y)));return result;
}
std::array<unsigned char,3> center(){std::array<unsigned char,3> result{};glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(160,120,1,1,GL_RGB,GL_UNSIGNED_BYTE,result.data());return result;}
void check(bool reflected) {
    viewer::TreeLayer trees;trees.assets.push_back({"tree",{},billboard_test::asset()});
    auto transform=glm::scale(glm::mat4(1),{reflected?-5.f:5.f,5.f,5.f});
    trees.instances.push_back(billboard_test::instance(trees.assets[0].model,transform));
    const auto original=trees.instances[0].transform;
    viewer::Renderer renderer({},VIEWER_SHADER_DIR,&trees);renderer.resize(320,240);
    renderer.setShadows(false);renderer.setHaze(false);renderer.setGroundFog(false);renderer.setTreeCanopyShading(false);
    GLuint query;glGenQueries(1,&query);
    const auto render=[&](const viewer::Camera& view){
        glBeginQuery(GL_PRIMITIVES_GENERATED,query);renderer.render(view);glEndQuery(GL_PRIMITIVES_GENERATED);
        GLuint triangles=0;glGetQueryObjectuiv(query,GL_QUERY_RESULT,&triangles);return triangles;
    };
    for(const auto eye:{glm::vec3(180,10,0),glm::vec3(-180,10,0),glm::vec3(0,10,180),glm::vec3(0,10,-180)}){
        require(render(camera(eye,{0,10,0}))==2&&renderer.treeStats().sideBillboards==1,
                "Distant tree must submit exactly one vertical quad");
        const auto pixel=center();require(pixel[0]>250&&pixel[1]<2&&pixel[2]<2,
                "One-sided vertical billboard did not face the camera (including reflection)");
    }
    for(const auto eye:{glm::vec3(0,180,0),glm::vec3(0,-180,0),glm::vec3(40,180,20)}){
        require(render(camera(eye,{0,10,0}))==2&&renderer.treeStats().topBillboards==1,
                "High-angle view must submit exactly one top quad");
        const auto pixel=center();require(pixel[2]>250&&pixel[0]<2&&pixel[1]<2,
                "One-sided top billboard, tilt or opposite-normal rotation is incorrect");
    }
    auto view=camera({0,10,180},{0,10,0});
    renderer.setTreeBillboardDistance(200);require(render(view)==8&&renderer.treeStats().fullDetail==1,"Near LoD did not retain all four cards");
    renderer.setTreeBillboardDistance(100);view.cycleViewMode();
    require(render(view)==8&&renderer.treeStats().sideBillboards==0,"Scope did not retain authored geometry");
    view.cycleViewMode();require(render(view)==8,"Strong scope did not retain authored geometry");
    view.cycleViewMode();require(render(view)==2,"Leaving scope did not restore single-card LoD");
    renderer.setTreeBillboardDistance(0);require(render(view)==8,"Disabling tree billboards did not restore four cards");
    renderer.setTreeBillboardDistance(50);renderer.setShadows(true);
    require(render(camera({0,10,80},{0,10,0}))==10,"Shadow pass must retain eight authored triangles plus two camera triangles");
    require(trees.instances[0].transform==original,"Billboard rendering mutated authoritative placement");
    GLboolean depth=GL_FALSE;glGetBooleanv(GL_DEPTH_WRITEMASK,&depth);GLint winding=0;glGetIntegerv(GL_FRONT_FACE,&winding);
    require(depth&&winding==GL_CCW&&!glIsEnabled(GL_CULL_FACE),"Billboard draw leaked depth, winding or culling state");
    viewer::Renderer::checkErrors("tree billboard GPU regression");glDeleteQueries(1,&query);
}
void nearBounds() {
    viewer::TreeLayer trees;trees.assets.push_back({"tree",{},billboard_test::asset()});
    const auto transform=glm::scale(glm::translate(glm::mat4(1),{149,0,0}),glm::vec3(5));
    trees.instances.push_back(billboard_test::instance(trees.assets[0].model,transform));
    viewer::Renderer renderer({},VIEWER_SHADER_DIR,&trees);renderer.resize(320,240);renderer.setShadows(false);
    const auto view=camera({0,10,180},{0,10,0});
    renderer.setTreeBillboardDistance(0);renderer.render(view);
    require(renderer.treeStats().visible==0,"Bounds fixture must be outside the authored camera query");
    renderer.setTreeBillboardDistance(500);renderer.render(view);
    require(renderer.treeStats().visible==0,"Expanded billboard bounds submitted an invisible nearby full tree");
    renderer.setTreeBillboardDistance(100);renderer.render(view);
    require(renderer.treeStats().sideBillboards==1,"Far billboard lost conservative rotated bounds");
    viewer::Renderer::checkErrors("tree billboard nearby bounds");
}
}
int main(){if(!glfwInit())return 77;glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);
glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
auto* window=glfwCreateWindow(320,240,"tree billboard regression",nullptr,nullptr);if(!window){glfwTerminate();return 77;}
glfwMakeContextCurrent(window);int code=0;try{viewer::initializeOpenGL();check(false);check(true);nearBounds();std::cout<<"Single-sided tree billboard GPU regressions passed\n";}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';code=1;}glfwDestroyWindow(window);glfwTerminate();return code;}
