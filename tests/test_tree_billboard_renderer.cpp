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
void splitCards(bool reflected) {
    auto model=billboard_test::asset();
    viewer::ImageData image;image.width=image.height=128;image.rgba.resize(128*128*4);
    for(int y=4;y<124;++y)for(int x=0;x<128;++x) {
        const bool canopy=y<80 && x>=8 && x<120;
        const bool trunk=y>=80 && x>=60 && x<68;
        if(!canopy && !trunk)continue;
        const auto i=(y*128+x)*4;image.rgba[i]=canopy?40:90;
        image.rgba[i+1]=canopy?120:75;image.rgba[i+2]=40;image.rgba[i+3]=255;
    }
    model.images.push_back(image);viewer::Texture texture;texture.image=0;texture.minFilter=texture.magFilter=9728;
    model.textures.push_back(texture);
    for(std::size_t i=0;i<2;++i) {
        model.materials[i].baseColor=glm::vec4(1);model.materials[i].baseColorTexture=0;
        const std::array<glm::vec2,4> uv{{{0,0},{0,1},{1,1},{1,0}}};
        for(std::size_t j=0;j<4;++j)model.primitives[i].vertices[j].uv=uv[j];
    }
    viewer::TreeLayer trees;trees.assets.push_back({"tree",{},model});
    const auto node=glm::scale(glm::mat4(1),{reflected?-1.f:1.f,1,1});
    for(auto& draw:trees.assets[0].model.draws)draw.transform=node;
    trees.instances.push_back(billboard_test::instance(trees.assets[0].model,glm::scale(glm::mat4(1),glm::vec3(5))));
    viewer::Renderer renderer({},VIEWER_SHADER_DIR,&trees);renderer.resize(320,240);
    renderer.setShadows(false);renderer.setHaze(false);renderer.setGroundFog(false);renderer.setTreeCanopyShading(false);
    for(const auto eye:{glm::vec3(.7f,8,30),glm::vec3(0,10,180),glm::vec3(0,180,0)}) {
        const auto view=camera(eye,{0,10,0});
        std::vector<unsigned char> before(320*240*4),after(before.size());
        renderer.setTreeCardSplitting(false);renderer.render(view);
        const auto original=renderer.treeStats();glReadPixels(0,0,320,240,GL_RGBA,GL_UNSIGNED_BYTE,before.data());
        renderer.setTreeCardSplitting(true);renderer.render(view);
        glReadPixels(0,0,320,240,GL_RGBA,GL_UNSIGNED_BYTE,after.data());
        require(before==after,"Trunk split changed nearest-filtered fixture coverage or color");
        require(renderer.treeStats().drawCalls==original.drawCalls,"Trunk split added a draw call");
        require(renderer.treeStats().triangles==original.triangles+(original.topBillboards?0:original.sideBillboards?2:4),
                "Split index range or single-card LoD triangle count incorrect");
    }
    viewer::Renderer::checkErrors("split tree cards");
}
void filtering() {
    auto model=billboard_test::asset();
    viewer::ImageData image;image.width=image.height=64;image.rgba.resize(64*64*4);
    for(int y=0;y<64;++y)for(int x=0;x<64;++x) {
        const auto i=(y*64+x)*4;
        image.rgba[i]=(x+y)%2?255:0;image.rgba[i+1]=0;image.rgba[i+2]=(x+y)%2?0:255;image.rgba[i+3]=255;
    }
    model.images.push_back(image);viewer::Texture texture;texture.image=0;
    texture.wrapS=33071;texture.wrapT=33648;model.textures.push_back(texture);
    for(std::size_t p=0;p<model.primitives.size();++p) {
        model.materials[p].baseColor=glm::vec4(1);model.materials[p].baseColorTexture=0;
        const std::array<glm::vec2,4> uv{{{0,0},{0,1},{1,1},{1,0}}};
        for(std::size_t j=0;j<4;++j)model.primitives[p].vertices[j].uv=uv[j];
    }
    viewer::TreeLayer trees;trees.assets.push_back({"tree",{},model});
    trees.instances.push_back(billboard_test::instance(model,glm::scale(glm::mat4(1),glm::vec3(5))));
    viewer::Renderer renderer({},VIEWER_SHADER_DIR,&trees);renderer.resize(320,240);
    renderer.setShadows(false);renderer.setHaze(false);renderer.setGroundFog(false);renderer.setTreeCanopyShading(false);
    const auto view=camera({.7f,8,30},{0,10,0});renderer.render(view);
    GLint treeTexture=0;glGetIntegerv(GL_TEXTURE_BINDING_2D,&treeTexture);
    require(treeTexture!=0,"Textured tree fixture did not bind its texture");
    const auto parameter=[](GLenum name){GLint value=0;glGetTexParameteriv(GL_TEXTURE_2D,name,&value);return value;};
    require(parameter(GL_TEXTURE_MIN_FILTER)==GL_NEAREST_MIPMAP_NEAREST && parameter(GL_TEXTURE_MAG_FILTER)==GL_NEAREST,
            "Vegetation must default to nearest texels and mip levels");
    std::vector<unsigned char> before(320*240*4),authored(before.size()),restored(before.size());
    glReadPixels(0,0,320,240,GL_RGBA,GL_UNSIGNED_BYTE,before.data());
    GLuint sentinel=0;glGenTextures(1,&sentinel);glActiveTexture(GL_TEXTURE5);glBindTexture(GL_TEXTURE_2D,sentinel);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    renderer.setTreeNearestFiltering(false);renderer.setTreeNearestFiltering(false);
    GLint active=0,binding=0;glGetIntegerv(GL_ACTIVE_TEXTURE,&active);glGetIntegerv(GL_TEXTURE_BINDING_2D,&binding);
    require(active==GL_TEXTURE5 && binding==static_cast<GLint>(sentinel),"Filtering switch leaked active texture/binding");
    require(parameter(GL_TEXTURE_MIN_FILTER)==GL_LINEAR,"Filtering switch modified an unrelated texture");
    glBindTexture(GL_TEXTURE_2D,static_cast<GLuint>(treeTexture));
    require(parameter(GL_TEXTURE_MIN_FILTER)==GL_LINEAR_MIPMAP_LINEAR && parameter(GL_TEXTURE_MAG_FILTER)==GL_LINEAR,
            "Disabling nearest did not restore authored filtering");
    require(parameter(GL_TEXTURE_WRAP_S)==33071 && parameter(GL_TEXTURE_WRAP_T)==33648,"Filtering switch changed wrapping");
    GLint mipWidth=0;glGetTexLevelParameteriv(GL_TEXTURE_2D,6,GL_TEXTURE_WIDTH,&mipWidth);
    require(mipWidth==1,"Filtering switch lost the mipmap chain");
    glActiveTexture(GL_TEXTURE0);renderer.render(view);
    glReadPixels(0,0,320,240,GL_RGBA,GL_UNSIGNED_BYTE,authored.data());
    require(authored!=before,"Nearest fixture produced no observable filtering change");
    renderer.setTreeNearestFiltering(true);renderer.render(view);
    require(parameter(GL_TEXTURE_MIN_FILTER)==GL_NEAREST_MIPMAP_NEAREST && parameter(GL_TEXTURE_MAG_FILTER)==GL_NEAREST,
            "Re-enabling nearest did not restore nearest sampling");
    glReadPixels(0,0,320,240,GL_RGBA,GL_UNSIGNED_BYTE,restored.data());
    require(restored==before,"Returning to nearest filtering changed the original capture");
    glDeleteTextures(1,&sentinel);viewer::Renderer::checkErrors("tree filtering regression");
}
}
int main(){if(!glfwInit())return 77;glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);
glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
auto* window=glfwCreateWindow(320,240,"tree billboard regression",nullptr,nullptr);if(!window){glfwTerminate();return 77;}
glfwMakeContextCurrent(window);int code=0;try{viewer::initializeOpenGL();check(false);check(true);nearBounds();splitCards(false);splitCards(true);filtering();std::cout<<"Tree billboard, split and filtering GPU regressions passed\n";}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';code=1;}glfwDestroyWindow(window);glfwTerminate();return code;}
