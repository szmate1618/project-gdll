#include "renderer.hpp"
#include "inventory/inventory_config.hpp"
#include "inventory/item_config.hpp"
#include "inventory/crafted_item.hpp"
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
std::vector<unsigned char> pixels() {
    std::vector<unsigned char> data(800*600*3);
    glPixelStorei(GL_PACK_ALIGNMENT,1);
    glReadPixels(0,0,800,600,GL_RGB,GL_UNSIGNED_BYTE,data.data());
    return data;
}
}
int main(int argc, char** argv) {
    if (!glfwInit()) return 77;
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    auto* window=glfwCreateWindow(800,600,"Ground items",nullptr,nullptr);
    if (!window) { glfwTerminate(); return 77; }
    glfwMakeContextCurrent(window);
    try {
        viewer::initializeOpenGL();
        const std::filesystem::path root(VIEWER_ITEM_ROOT);
        auto inventory=viewer::loadPlayerInventory(root);
        auto floor=viewer::BoxLayer::unitCube();
        floor.materials[0].baseColor={.55f,.55f,.55f,1};
        floor.draws.push_back({0,glm::scale(glm::mat4(1),glm::vec3(8,.1f,8)),"floor"});
        floor.boundsMin={-4,-.05f,-4}; floor.boundsMax={4,.05f,4};
        std::array<viewer::Model,viewer::weaponIds.size()> models;
        for (std::size_t i=0; i<models.size(); ++i)
            models[i]=viewer::loadModel(std::filesystem::path(VIEWER_WEAPON_ASSETS)/(std::string(viewer::weaponIds[i])+".glb"));
        viewer::GroundItems ground(std::make_shared<viewer::PhysicsWorld>());
        ground.setModels(models);
        viewer::Renderer renderer(floor,root/"shaders");
        renderer.resize(800,600); renderer.setShadows(false); renderer.setHaze(false); renderer.setGroundFog(false);
        renderer.setFlashlight(false); renderer.setGroundItems(ground,models);
        viewer::Camera camera; camera.setPosition({0,.9f,1.2f}); camera.setOrientation(-90,-33);
        renderer.render(camera); const auto empty=pixels();
        ground.add(*inventory.find("pistol_rounds_1"),23,{}, {-.25f,.1f,0});
        renderer.render(camera); const auto ammo=pixels();
        ground.add(*inventory.find("pistol"),0,viewer::WeaponItem{0,7,0},{.25f,.15f,0});
        renderer.render(camera); const auto weapon=pixels();
        const auto changed=[](const auto& a,const auto& b) {
            std::size_t count=0;
            for (std::size_t i=0;i<a.size();i+=3)
                if (std::abs(int(a[i])-b[i])+std::abs(int(a[i+1])-b[i+1])+std::abs(int(a[i+2])-b[i+2])>12) ++count;
            return count;
        };
        require(changed(empty,ammo)>100,"Dropped ammo box did not render");
        require(changed(ammo,weapon)>100,"Dropped weapon model did not render");
        ground.add(*inventory.find("stake"),0,viewer::WeaponItem{viewer::stakeSlot,0,0},
                   {0,.65f,-.25f}, {}, glm::angleAxis(glm::radians(65.f),glm::vec3(1,0,0)));
        renderer.render(camera);
        require(changed(weapon,pixels())>100,"Stake model did not render");
        viewer::GroundItems materials(std::make_shared<viewer::PhysicsWorld>());
        renderer.setGroundItems(materials,models);
        camera.setPosition({0,.9f,1.6f}); camera.setOrientation(-90,-25);
        renderer.render(camera);
        auto beforeItem=pixels();
        const std::array<const char*,4> ids{"duct_tape","wire_spool","nail","broom"};
        const std::array<glm::vec3,4> positions{{{.1f,.06f,.3f},{.4f,.06f,.3f},{.7f,.055f,.3f},{-.45f,.65f,-.25f}}};
        for (std::size_t index=0; index<ids.size(); ++index) {
            const auto item=viewer::loadItem(root/"storage/items",ids[index]);
            materials.add(item,item.inventory().stackable ? 1 : 0,{},positions[index]);
            renderer.render(camera); const auto current=pixels();
            require(changed(beforeItem,current)>(index==2 ? 5u : 25u),"A configured crafting item model did not render");
            beforeItem=current;
        }

        // Binding intersections use the ordinary world pass, including when no
        // equippable items exist. Their invisible planes have no rendered mesh.
        viewer::GroundItems bindings(std::make_shared<viewer::PhysicsWorld>());
        viewer::GroundItems replacement(std::make_shared<viewer::PhysicsWorld>());
        const auto verifyBindings=[&](viewer::SceneDepthMode depthMode) {
            renderer.setSceneDepthMode(depthMode);
            renderer.setGroundItems(bindings,models);
            bindings.setBindingLines({});
            renderer.render(camera); const auto unmarked=pixels();
            const std::vector<glm::vec3> first{{-.65f,.05f,.1f},{.65f,.05f,.1f}};
            bindings.setBindingLines(first);
            renderer.render(camera); const auto marked=pixels();
            require(changed(unmarked,marked)>150,"Surface binding intersection lines did not render in gameplay");
            bindings.setBindingLines({{-.65f,.05f,-.3f},{.65f,.05f,-.3f}});
            renderer.render(camera);
            require(changed(marked,pixels())>150,"Changed binding intersections retained old line endpoints");
            bindings.setBindingLines({{-.65f,-.1f,.1f},{.65f,-.1f,.1f}});
            renderer.render(camera);
            require(changed(unmarked,pixels())==0,"Binding intersection lines drew through opaque scene geometry");
            bindings.setBindingLines(first);
            renderer.render(camera);
            bindings.setBindingLines({});
            renderer.render(camera);
            require(changed(unmarked,pixels())==0,"Cleared binding intersections left stale lines");
            bindings.setBindingLines(first);
            renderer.render(camera);
            renderer.setGroundItems(replacement,models);
            renderer.render(camera);
            require(changed(unmarked,pixels())==0,"Replacing the ground source retained its binding intersections");
        };
        verifyBindings(viewer::SceneDepthMode::Conventional24);
        if (renderer.reversedDepthSupported()) verifyBindings(viewer::SceneDepthMode::Reversed32);

        // A lifted prop can retain its source visuals without a melee grip.
        // Use two colored triangles with a large empty gap inside one physical
        // box: drawing the collider instead would fill that entire gap.
        viewer::CraftedItemData meshData;
        meshData.id="lifted_mesh"; meshData.prototype="synthetic_prop";
        meshData.name="Lifted colored prop"; meshData.acoustics="crate";
        meshData.width=meshData.height=2; meshData.tags={"wood"};
        viewer::ItemModelPart collider;
        collider.name="physical_box"; collider.material="wood"; collider.size={.8f,.8f,.08f}; collider.color={1,1,1};
        meshData.model.parts.push_back(collider);
        const auto triangle=[&](float x,glm::vec3 color) {
            for (const auto point : {glm::vec3(x-.16f,-.3f,.02f),glm::vec3(x+.16f,-.3f,.02f),glm::vec3(x,.3f,.02f)})
                meshData.model.mesh.push_back({point,{0,0,1},color});
        };
        triangle(-.2f,{1,.02f,.02f}); triangle(.2f,{.02f,.02f,1});
        const auto custom=viewer::restoreCraftedItem(viewer::captureCraftedItem(viewer::restoreCraftedItem(meshData)));
        require(custom.grips().empty() && custom.inventory().model.mesh.size()==6 && custom.hasTag("wood"),
                "An ungripped custom prop lost its visual/material metadata during restoration");
        auto whiteData=viewer::captureCraftedItem(custom);
        for (auto& vertex:whiteData.model.mesh) vertex.color={1,1,1};
        const auto white=viewer::restoreCraftedItem(whiteData);
        auto boxData=whiteData; boxData.model.mesh.clear();
        const auto physical=viewer::restoreCraftedItem(boxData);
        auto recoloredData=meshData;
        for (auto& vertex:recoloredData.model.mesh) vertex.color={.02f,1,.02f};
        const auto recolored=viewer::restoreCraftedItem(recoloredData);
        viewer::GroundItems customSource(std::make_shared<viewer::PhysicsWorld>());
        viewer::GroundItems whiteSource(std::make_shared<viewer::PhysicsWorld>());
        viewer::GroundItems physicalSource(std::make_shared<viewer::PhysicsWorld>());
        viewer::GroundItems recoloredSource(std::make_shared<viewer::PhysicsWorld>());
        viewer::GroundItems hiddenSource(std::make_shared<viewer::PhysicsWorld>());
        customSource.add(custom,0,{}, {0,.6f,0}); whiteSource.add(white,0,{}, {0,.6f,0});
        physicalSource.add(physical,0,{}, {0,.6f,0}); recoloredSource.add(recolored,0,{}, {0,.6f,0});
        hiddenSource.add(custom,0,{}, {0,-.5f,0});
        camera.setPosition({0,.6f,2}); camera.setOrientation(-90,0);
        const auto verifyCustomMesh=[&](viewer::SceneDepthMode depthMode) {
            renderer.setSceneDepthMode(depthMode);
            renderer.setGroundItems(replacement,models); renderer.render(camera); const auto backdrop=pixels();
            renderer.setGroundItems(customSource,models); renderer.render(camera); const auto colored=pixels();
            std::size_t red=0,blue=0;
            for (std::size_t i=0;i<colored.size();i+=3) {
                if (std::abs(int(colored[i])-backdrop[i])+std::abs(int(colored[i+1])-backdrop[i+1])+
                    std::abs(int(colored[i+2])-backdrop[i+2])<=12) continue;
                if (colored[i]>colored[i+1]+20 && colored[i]>colored[i+2]+20) ++red;
                if (colored[i+2]>colored[i]+20 && colored[i+2]>colored[i+1]+20) ++blue;
            }
            require(red>300 && blue>300,"Custom prop triangles lost their independent source colors");
            renderer.setGroundItems(whiteSource,models); renderer.render(camera); const auto whitePixels=pixels();
            renderer.setGroundItems(physicalSource,models); renderer.render(camera); const auto boxPixels=pixels();
            require(changed(backdrop,colored)>500 && changed(backdrop,boxPixels)>changed(backdrop,colored)*2,
                    "Custom prop rendering used the physical box instead of its sparse visual mesh");
            std::size_t sampleCount=0,matchingMaterial=0;
            for (std::size_t i=0;i<colored.size();i+=3) {
                if (std::abs(int(colored[i])-backdrop[i])+std::abs(int(colored[i+1])-backdrop[i+1])+
                    std::abs(int(colored[i+2])-backdrop[i+2])<=12) continue;
                ++sampleCount;
                if (std::abs(int(whitePixels[i])-boxPixels[i])+std::abs(int(whitePixels[i+1])-boxPixels[i+1])+
                    std::abs(int(whitePixels[i+2])-boxPixels[i+2])<12) ++matchingMaterial;
            }
            require(matchingMaterial*10>sampleCount*9,
                    "Custom mesh vertex colors received an extra fallback material tint");
            renderer.setGroundItems(recoloredSource,models); renderer.render(camera);
            require(changed(colored,pixels())>500,"Recolored custom geometry reused stale cached vertex colors");
            renderer.setGroundItems(hiddenSource,models); renderer.render(camera);
            require(changed(backdrop,pixels())==0,"Custom prop visuals rendered through the opaque floor");
        };
        verifyCustomMesh(viewer::SceneDepthMode::Conventional24);
        if (renderer.reversedDepthSupported()) verifyCustomMesh(viewer::SceneDepthMode::Reversed32);
        renderer.setGroundItems(materials,models);
        require(glGetError()==GL_NO_ERROR,"Ground item rendering generated a GL error");
        if (argc>1) {
            camera.setPosition({1.7f,1.1f,2.8f}); camera.setOrientation(-115,-12);
            renderer.render(camera); renderer.writeScreenshot(argv[1]);
        }
        std::cout<<"Ground item rendering checks passed\n";
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; glfwDestroyWindow(window); glfwTerminate(); return 1; }
    glfwDestroyWindow(window); glfwTerminate(); return 0;
}
