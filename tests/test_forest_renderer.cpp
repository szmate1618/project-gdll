#include "renderer.hpp"
#include "forest_shrub.hpp"
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
void checkSwitching(bool textured = false, bool matchCanopy = true) {
    viewer::TreeLayer trees; trees.assets.push_back({"tree",{},treeAsset()});
    trees.assets[0].matchCanopyColor=matchCanopy;
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
    const std::array<unsigned char,3> matchedColor{40,60,28};
    if (textured) {
        auto& asset = trees.assets[0].model;
        asset.images = {{1,1,{180,220,110,255}}};
        asset.textures = {{0}};
        asset.materials[0].baseColor = {1,1,1,1};
        asset.materials[0].baseColorTexture = 0;
        forest.volumes.images = {{1,1,{40,60,28,255}}};
        forest.volumes.textures = {{0}};
        for (auto& material : forest.volumes.materials) {
            material.baseColor = {1,1,1,1};
            material.baseColorTexture = 0;
        }
    }
    viewer::Renderer renderer({},VIEWER_SHADER_DIR,&trees,nullptr,&forest);
    require(!textured || trees.assets[0].model.materials[0].baseColor == glm::vec4(1),
            "Renderer tint mutated the source tree asset");
    renderer.setShadows(false); renderer.setHaze(false); renderer.setGroundFog(false); renderer.resize(640,480);
    viewer::Camera camera; camera.setPosition({0,5,0}); camera.look(0,200);
    const auto render=[&](bool detailed) {
        renderer.render(camera);
        const auto pixel=center();
        if (textured) {
            for (std::size_t channel=0;channel<3;++channel)
                require(std::abs(static_cast<int>(pixel[channel])-
                            (detailed&&!matchCanopy?std::array<unsigned char,3>{180,220,110}[channel]:matchedColor[channel]))<=1,
                        "Textured billboards lost canopy matching or bare-wood color across LoD/scope switches");
        } else {
            require(detailed ? pixel[1]>250 && pixel[0]<2 : pixel[0]>250 && pixel[1]<2,
                    "LoD rendered the wrong geometry at the camera centre");
        }
        require(renderer.treeStats().visible==(detailed?2u:0u), "Volume and generated/detected trees drawn together or trees missing");
        require(renderer.forestStats().volumes==(detailed?0u:1u), "Wrong forest volume count");
        require(renderer.forestStats().drawCalls==(detailed?0u:2u), "Wrong material batch count");
        require(renderer.treeStats().queriedForestPatches==(detailed?1u:0u),
                "Volume patch queried individual trees");
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

void checkMultipleVolumes() {
    viewer::TreeLayer trees; trees.assets.push_back({"tree",{},treeAsset()});
    viewer::ForestLayer forest; forest.detailDistance=20;
    const viewer::ForestPolygon polygon{{{-5,100},{5,100},{5,120},{-5,120},{-5,100}}, {}};
    const auto meshes=viewer::forestVolume(polygon,[](glm::dvec2){return std::optional<float>(0);},10,8);
    forest.volumes.primitives={meshes.first,meshes.second};
    forest.volumes.images={{1,1,{255,0,0,255}}, {1,1,{0,0,255,255}}};
    forest.volumes.textures={{0},{1}};
    viewer::Material walls; walls.unlit=true; walls.baseColorTexture=0;
    viewer::Material top=walls; top.baseColorTexture=1;
    forest.volumes.materials={walls,top};
    for (std::size_t i=0;i<4;++i) {
        const glm::vec3 offset=i==3 ? glm::vec3(0,0,220) : glm::vec3((static_cast<float>(i)-1)*24,0,0);
        auto transform=glm::translate(glm::mat4(1),offset);
        if (i==1) transform=glm::scale(transform,{-1,1,1});
        forest.volumes.draws.push_back({0,transform,"walls"});
        forest.volumes.draws.push_back({1,transform,"top"});
        viewer::TreeInstance instance;
        instance.transform=glm::translate(glm::mat4(1),offset+glm::vec3(0,0,-110));
        instance.boundsMin=offset+glm::vec3(-5,0,-110.01f);
        instance.boundsMax=offset+glm::vec3(5,10,-109.99f);
        trees.instances.push_back(instance);
        forest.patches.push_back({std::to_string(i),
            {offset+glm::vec3(-5,0,-120),offset+glm::vec3(5,10,-100)},i,1,i*2,2,{}});
    }
    viewer::Renderer renderer({},VIEWER_SHADER_DIR,&trees,nullptr,&forest);
    renderer.setShadows(false); renderer.setHaze(false); renderer.setGroundFog(false); renderer.resize(640,480);
    viewer::Camera camera; camera.setPosition({0,5,0}); camera.look(0,200);
    renderer.render(camera);
    require(renderer.forestStats().volumes==3 && renderer.forestStats().drawCalls==2,
            "Visible volumes were not submitted as two shared material batches");
    for (float x : {-24.0f,0.0f,24.0f}) {
        const auto clip=camera.projection(640.0f/480)*camera.view()*glm::vec4(x,5,-100,1);
        const int pixelX=static_cast<int>((clip.x/clip.w*.5f+.5f)*640);
        std::array<unsigned char,3> pixel{};
        glReadPixels(pixelX,240,1,1,GL_RGB,GL_UNSIGNED_BYTE,pixel.data());
        require(pixel[0]>250 && pixel[1]<2 && pixel[2]<2,
                "Multi-draw omitted a patch, changed its texture, or lost reflected winding");
    }
    renderer.setTreeCulling(false); renderer.render(camera);
    require(renderer.forestStats().volumes==4 && renderer.forestStats().drawCalls==2,
            "Culling bypass did not retain batching");
    renderer.setTreeCulling(true);
    camera.setPosition({0,5,-90}); renderer.render(camera);
    require(renderer.forestStats().volumes==2 && renderer.forestStats().drawCalls==2 &&
            renderer.treeStats().visible==1 && center()[1]>250,
            "Mixed detail rendered stale multi-draw ranges");
    camera.cycleViewMode(); renderer.render(camera);
    require(renderer.forestStats().volumes==0 && renderer.forestStats().drawCalls==0 && center()[1]>250,
            "Scope left distant volume batches active");
    camera.cycleViewMode(); camera.cycleViewMode();
    camera.setPosition({0,40,-110}); camera.look(0,-890); renderer.render(camera);
    const auto canopy=center();
    require(canopy[2]>250 && canopy[0]<2 && canopy[1]<2,
            "Batched top texture or reflected top winding changed");
    renderer.setShadows(true); renderer.render(camera);
    require(center()==canopy && renderer.forestStats().drawCalls==2,
            "Shadow multi-draw corrupted color batches or their counters");
    viewer::Renderer::checkErrors("batched forest volumes");
}

void checkBlastMotion() {
    viewer::TreeLayer trees;
    trees.assets.push_back({"shrub", {}, viewer::forestShrubModel({2, 1, {40,180,30,255,40,180,30,255}})});
    viewer::TreeInstance shrub;
    shrub.transform = glm::scale(glm::mat4(1), glm::vec3(3));
    shrub.boundsMin = {-6,0,-6}; shrub.boundsMax = {6,3,6}; shrub.dimensions = {12,3,12};
    shrub.collidable = false; shrub.blastResponsive = true;
    trees.instances.push_back(shrub);
    viewer::Model ground;
    ground.materials.push_back({});
    viewer::Primitive mesh; mesh.material = 0;
    for (auto p : {glm::vec3(-30,-.01f,-30), glm::vec3(-30,-.01f,30),
                   glm::vec3(30,-.01f,30), glm::vec3(30,-.01f,-30)}) {
        viewer::Vertex vertex; vertex.position = p; vertex.normal = {0,1,0};
        mesh.vertices.push_back(vertex);
    }
    mesh.indices = {0,1,2,0,2,3}; ground.primitives.push_back(mesh);
    ground.draws.push_back({0,glm::mat4(1),"ground"});
    viewer::Camera camera; camera.setPosition({9,7,12}); camera.setOrientation(-126.87f,-21.8f);
    const auto setup = [](viewer::Renderer& renderer) {
        renderer.setHaze(false); renderer.setGroundFog(false);
        renderer.setTreeCanopyShading(false); renderer.resize(640,480);
    };
    const auto pixels = [] {
        std::vector<unsigned char> result(640*480*3);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glReadPixels(0,0,640,480,GL_RGB,GL_UNSIGNED_BYTE,result.data());
        return result;
    };
    viewer::Renderer animated(ground, VIEWER_SHADER_DIR, &trees); setup(animated);
    animated.render(camera); const auto rest = pixels();
    const std::vector<viewer::GrenadeExplosion> events{{1,{-2,1,0},.1f}};
    animated.updateVegetationBlasts(events); animated.render(camera); const auto moved = pixels();
    require(rest != moved, "Blast did not move rendered vegetation");
    auto reference = trees;
    // Render the same pose as a static instance to check color and shadow passes.
    const auto bend = viewer::VegetationBlastMotion(trees.instances).sample(events).at(0);
    reference.instances[0].transform = bend.transform;
    viewer::Renderer expected(ground, VIEWER_SHADER_DIR, &reference); setup(expected);
    expected.render(camera);
    require(pixels() == moved, "Animated cards or sun shadows differ from the shared transformed geometry");
    animated.updateVegetationBlasts(events); animated.render(camera);
    require(pixels() == moved, "Repeated event accumulated deformation on the GPU");
    animated.setTreeShadows(false); animated.render(camera);
    require(pixels() != moved, "Shadow test fixture has no visible vegetation shadow");
    animated.setTreeShadows(true);
    animated.updateVegetationBlasts({{1,{-2,1,0},.9f}}); animated.render(camera);
    require(pixels() == rest, "Expired events left a shrub deformed");
    animated.updateVegetationBlasts(events); animated.updateVegetationBlasts({}); animated.render(camera);
    require(pixels() == rest, "Missing events left a shrub deformed");
    // Count the submitted triangles with no shadow pass: animation must not add
    // geometry or change batching, including when it restores the rest pose.
    animated.setShadows(false);
    GLuint query = 0; glGenQueries(1, &query);
    const auto triangles = [&] {
        glBeginQuery(GL_PRIMITIVES_GENERATED, query); animated.render(camera); glEndQuery(GL_PRIMITIVES_GENERATED);
        GLuint count = 0; glGetQueryObjectuiv(query, GL_QUERY_RESULT, &count); return count;
    };
    const auto before = triangles(); const auto calls = animated.treeStats().drawCalls;
    animated.updateVegetationBlasts(events);
    require(triangles() == before && animated.treeStats().drawCalls == calls && calls == 1,
            "Blast animation added triangles or tree draw calls");
    glDeleteQueries(1, &query);
    require(trees.instances[0].transform == shrub.transform &&
            trees.assets[0].model.primitives[0].vertices.size() == 12 &&
            trees.assets[0].model.primitives[0].indices.size() == 18,
            "Animation mutated source placements or mesh topology");
    animated.setFlashlight(true); animated.setFlashlightShadows(true);
    expected.setFlashlight(true); expected.setFlashlightShadows(true); expected.setShadows(false);
    animated.render(camera); const auto flashlight = pixels(); expected.render(camera);
    require(pixels() == flashlight, "Flashlight shadows did not share the animated shrub pose");
    viewer::Renderer::checkErrors("vegetation blast cards and shadows");
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
    try {checkSwitching(); checkSwitching(true); checkSwitching(true,false); checkMultipleVolumes(); checkBlastMotion(); std::cout<<"Forest GPU LoD, color matching, batching and vegetation blast regressions passed\n";}
    catch (const std::exception& e) {std::cerr<<e.what()<<'\n'; code=1;}
    glfwDestroyWindow(window); glfwTerminate(); return code;
}
