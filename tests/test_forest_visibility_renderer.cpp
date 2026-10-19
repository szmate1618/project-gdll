#include "renderer.hpp"
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
viewer::Model card(glm::vec4 color,float width=2,float height=10) {
    viewer::Model model;viewer::Material material;material.baseColor=color;
    material.unlit=true;material.doubleSided=true;material.alphaMode="MASK";model.materials.push_back(material);
    viewer::Primitive mesh;mesh.material=0;
    for(const auto p:{glm::vec3(-width/2,0,0),glm::vec3(width/2,0,0),glm::vec3(width/2,height,0),glm::vec3(-width/2,height,0)}){
        viewer::Vertex vertex;vertex.position=p;vertex.normal={0,0,1};mesh.vertices.push_back(vertex);
    }
    mesh.indices={0,1,2,0,2,3};model.primitives.push_back(mesh);model.draws.push_back({0,glm::mat4(1),"card"});
    return model;
}
viewer::TreeLayer trees() {
    viewer::TreeLayer layer;layer.assets.push_back({"test",{},card({0,1,0,1},4)});
    for(const auto root:{glm::vec3(95,0,-120),glm::vec3(100,0,-240),glm::vec3(105,0,-175)}){
        viewer::TreeInstance tree;tree.transform=glm::translate(glm::mat4(1),root);
        tree.boundsMin=root+glm::vec3(-2,0,-.01f);tree.boundsMax=root+glm::vec3(2,10,.01f);
        tree.dimensions={4,10,1};layer.instances.push_back(tree);
    }
    return layer;
}
viewer::Model town() {
    auto model=card({1,0,0,1});
    model.draws={{0,glm::translate(glm::mat4(1),{110,0,-180}),"near"},
                 {0,glm::translate(glm::mat4(1),{100,0,-240}),"far"}};
    viewer::Material gray;gray.baseColor={.25f,.25f,.25f,1};gray.unlit=true;gray.doubleSided=true;model.materials.push_back(gray);
    viewer::Primitive ground;ground.material=1;
    for(const auto p:{glm::vec3(0,0,0),glm::vec3(0,0,-300),glm::vec3(200,0,-300),glm::vec3(200,0,0)}){
        viewer::Vertex v;v.position=p;v.normal={0,1,0};ground.vertices.push_back(v);
    }
    ground.indices={0,2,1,0,3,2};model.primitives.push_back(ground);model.draws.push_back({1,glm::mat4(1),"crossing_ground"});
    return model;
}
viewer::ForestLayer forest(std::size_t count=3) {
    viewer::ForestLayer layer;viewer::ForestPatch patch;
    patch.bounds={{0,0,-200},{200,30,0}};patch.firstTree=0;patch.treeCount=count;
    patch.oldForest={{{50,50},{150,50},{150,150},{50,150},{50,50}}, {}};
    layer.patches.push_back(patch);return layer;
}
void setup(viewer::Renderer& renderer) {
    renderer.resize(640,480);renderer.setHaze(false);renderer.setGroundFog(false);renderer.setTreeCanopyShading(false);
}
std::array<unsigned char,3> pixel(const viewer::Camera& camera,glm::vec3 point) {
    const auto clip=camera.projection(640.f/480)*camera.view()*glm::vec4(point,1);
    const int x=static_cast<int>((clip.x/clip.w*.5f+.5f)*640),y=static_cast<int>((clip.y/clip.w*.5f+.5f)*480);
    require(x>=0&&x<640&&y>=0&&y<480,"Test point is outside capture");
    std::array<unsigned char,3> result{};glReadBuffer(GL_COLOR_ATTACHMENT0);glReadPixels(x,y,1,1,GL_RGB,GL_UNSIGNED_BYTE,result.data());return result;
}
std::vector<unsigned char> pixels() {
    std::vector<unsigned char> result(640*480*3);glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(0,0,640,480,GL_RGB,GL_UNSIGNED_BYTE,result.data());return result;
}
void cutoffAndTransitions() {
    const auto vegetation=trees();const auto scene=town();const auto areas=forest();
    viewer::Camera camera;camera.setPosition({100,5,-100});camera.setOrientation(-90,-3);
    viewer::Renderer empty({},VIEWER_SHADER_DIR);setup(empty);empty.render(camera);
    const auto background=pixel(camera,{140,0,-230});
    viewer::Renderer renderer(scene,VIEWER_SHADER_DIR,&vegetation,nullptr,&areas);setup(renderer);renderer.setShadows(false);
    renderer.render(camera);
    require(renderer.treeStats().visible==2&&renderer.sceneStats().visible==2,
            "Inside old forest must cull distant trees and town draws");
    require(pixel(camera,{140,0,-230})==background,"Large terrain mesh leaked pixels beyond 100 m");
    require(pixel(camera,{115,0,-125})!=background,"Cutoff discarded nearby geometry");
    const auto inside=pixels();
    renderer.setOldForestDistance(200);renderer.render(camera);
    require(renderer.treeStats().visible==3&&renderer.sceneStats().visible==3&&
            pixel(camera,{140,0,-230})!=background,"Larger forest distance did not restore geometry and pixels");
    renderer.setOldForestDistance(50);renderer.render(camera);
    require(renderer.treeStats().visible==1&&renderer.sceneStats().visible==1,
            "Smaller forest distance did not restrict both tree and town selection");
    renderer.setOldForestDistance(0);renderer.render(camera);
    require(renderer.treeStats().visible==3&&renderer.sceneStats().visible==3&&
            pixel(camera,{140,0,-230})!=background,"Zero forest distance did not disable selection and fragment clipping");
    renderer.setOldForestDistance(100);renderer.render(camera);
    require(pixels()==inside,"Re-enabling forest distance left stale clipping state");
    renderer.setTreeCulling(false);renderer.setSceneCulling(false);renderer.render(camera);
    require(renderer.treeStats().visible==2&&renderer.sceneStats().visible==2&&pixels()==inside,
            "Debug frustum bypass disabled the forest cutoff");
    camera.cycleViewMode();renderer.render(camera);
    require(renderer.treeStats().visible==3&&renderer.sceneStats().visible==3,
            "Scope did not restore distant tree and town visibility");
    camera.cycleViewMode();renderer.render(camera);
    require(renderer.treeStats().visible==3&&renderer.sceneStats().visible==3,
            "Strong scope did not restore distant geometry");
    camera.cycleViewMode();renderer.render(camera);
    require(pixels()==inside,"Leaving scope did not restore the old-forest range");
    camera.setPosition({100,5,-40});renderer.render(camera);
    require(renderer.treeStats().visible==3&&renderer.sceneStats().visible==3,
            "Leaving old forest did not restore full scene selection");
    require(pixel(camera,{140,0,-230})!=background,"Leaving old forest left the fragment cutoff active");
    camera.setPosition({100,5,-100});renderer.render(camera);
    require(pixels()==inside,"Re-entering old forest produced stale range state");
    auto holed=areas;holed.patches[0].oldForest.holes={{{90,90},{90,110},{110,110},{110,90},{90,90}}};
    viewer::Renderer hole(scene,VIEWER_SHADER_DIR,&vegetation,nullptr,&holed);setup(hole);hole.setShadows(false);hole.render(camera);
    require(hole.treeStats().visible==3&&hole.sceneStats().visible==3,"An old-forest hole activated the cutoff");
    viewer::Renderer normal(scene,VIEWER_SHADER_DIR,&vegetation);setup(normal);normal.setShadows(false);normal.render(camera);
    require(normal.treeStats().visible==3&&normal.sceneStats().visible==3,"A scene with no old forest lost distant geometry");
    viewer::Renderer::checkErrors("old forest distance and transitions");
}
void shadowRange() {
    auto vegetation=trees();vegetation.assets.push_back({"tall",{},card({0,1,0,1},8,160)});
    auto& far=vegetation.instances[1];far.asset=1;far.transform=glm::translate(glm::mat4(1),{-20,0,-100});
    far.boundsMin={-24,0,-100.01f};far.boundsMax={-16,160,-99.99f};far.dimensions={8,160,1};
    auto nearOnly=vegetation;nearOnly.instances.erase(nearOnly.instances.begin()+1);
    auto scene=town();scene.materials[1].unlit=false;scene.draws.erase(scene.draws.begin(),scene.draws.begin()+2);
    auto areas=forest();auto one=forest(2);
    viewer::Camera camera;camera.setPosition({100,5,-100});camera.setOrientation(-115,-15);
    viewer::Renderer actual(scene,VIEWER_SHADER_DIR,&vegetation,nullptr,&areas);setup(actual);
    viewer::Renderer expected(scene,VIEWER_SHADER_DIR,&nearOnly,nullptr,&one);setup(expected);
    actual.render(camera);const auto sunlight=pixels();expected.render(camera);
    require(pixels()==sunlight,"Outside-radius caster shaded nearby ground");
    // Prove the tall offscreen caster can affect this fixture when unrestricted.
    auto unrestricted=areas;unrestricted.patches[0].oldForest={};
    viewer::Renderer baseline(scene,VIEWER_SHADER_DIR,&vegetation,nullptr,&unrestricted);setup(baseline);baseline.render(camera);
    const auto withFar=pixel(camera,{50,0,-152});
    const auto unrestrictedPixels=pixels();
    actual.setOldForestDistance(0);actual.render(camera);
    require(pixels()==unrestrictedPixels,"Disabling forest distance did not restore unrestricted sun shadows");
    actual.setOldForestDistance(150);actual.render(camera);
    require(pixel(camera,{50,0,-152})==withFar,"Custom radius did not restore a caster within that radius");
    actual.setOldForestDistance(100);
    auto unrestrictedOne=unrestricted;unrestrictedOne.patches[0].treeCount=2;
    viewer::Renderer noFar(scene,VIEWER_SHADER_DIR,&nearOnly,nullptr,&unrestrictedOne);setup(noFar);noFar.render(camera);
    require(pixel(camera,{50,0,-152})!=withFar,"Shadow fixture did not expose an outside-radius caster");
    actual.setNight(true);expected.setNight(true);actual.setFlashlight(true);expected.setFlashlight(true);
    actual.setFlashlightShadows(true);expected.setFlashlightShadows(true);
    actual.render(camera);const auto flashlight=pixels();expected.render(camera);
    require(pixels()==flashlight,"Flashlight caster selection ignored old-forest range");
    viewer::Renderer::checkErrors("old forest shadow range");
}
}
int main() {
    if(!glfwInit())return 77;
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    auto* window=glfwCreateWindow(640,480,"Old forest cutoff regression",nullptr,nullptr);
    if(!window){glfwTerminate();return 77;}glfwMakeContextCurrent(window);
    int code=0;try{cutoffAndTransitions();shadowRange();std::cout<<"Old forest GPU cutoff and shadows passed\n";}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';code=1;}
    glfwDestroyWindow(window);glfwTerminate();return code;
}
