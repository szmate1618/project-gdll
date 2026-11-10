#include "house_fixture.hpp"
#include "apartment_fixture.hpp"
#include "section_fixture.hpp"
#include "floorplan_layer.hpp"
#include "door_layer.hpp"
#include "collision_world.hpp"
#include "physics_world.hpp"
#include "renderer.hpp"

#include <GLFW/glfw3.h>
#include <array>
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
constexpr int width = 960, height = 720;
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
std::array<unsigned char, 3> pixel(const viewer::Camera& camera, glm::vec3 point) {
    const auto p = camera.projection(float(width) / height) * camera.view() * glm::vec4(point, 1);
    const auto ndc = glm::vec3(p) / p.w;
    require(std::abs(ndc.x) < .95f && std::abs(ndc.y) < .95f, "Floorplan sample must be on screen");
    std::array<unsigned char, 3> rgb{};
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(int((ndc.x + 1) * width * .5f), int((ndc.y + 1) * height * .5f), 1, 1, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());
    return rgb;
}
float depth(const viewer::Camera& camera, glm::vec3 point) {
    const auto p = camera.projection(float(width) / height) * camera.view() * glm::vec4(point, 1);
    const auto ndc = glm::vec3(p) / p.w;
    float result = 0;
    glReadPixels(int((ndc.x + 1) * width * .5f), int((ndc.y + 1) * height * .5f), 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &result);
    return viewer::sceneClipControl() ? 1.f-result : result;
}
void render(const std::filesystem::path& capture) {
    auto model = house_fixture::house();
    // This stable identity creates upper living rooms as well as both entries.
    for (auto& draw : model.draws) if (draw.name.rfind("building_house", 0) == 0)
        draw.name.replace(0, 14, "building_render_0");
    auto physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::DoorLayer doors(viewer::prepareDoorPanels(model), physics);
    viewer::CollisionWorld collision(model, {}, &doors);
    viewer::FloorplanLayer layer(model, doors, collision);
    viewer::Renderer renderer(model, VIEWER_SHADER_DIR);
    renderer.resize(width, height); renderer.setHaze(false); renderer.setGroundFog(false); renderer.setShadows(false);
    viewer::Camera camera;
    camera.setPosition({3, 4.4f, 3}); camera.setOrientation(50, 0);
    renderer.setShadows(true); renderer.render(camera);
    const auto bareCorner = pixel(camera, {5, 4.5f, 5.97f});
    renderer.setShadows(false);
    camera.setPosition({2, 1.65f, 5.5f}); camera.setOrientation(-90, 0);
    renderer.render(camera);
    const auto bare = pixel(camera, {2, 1.65f, 4});
    doors.setAngle(1, .9f); layer.update({camera.position()});
    renderer.updateDoors(doors); renderer.updateFloorplans(layer); renderer.render(camera);
    const auto partition = pixel(camera, {2, 1.65f, 4});
    require(partition != bare, "A generated partition must replace the distant bare shell in the camera view");
    camera.setPosition({0, 1.65f, 5.6f});
    renderer.render(camera);
    const auto closed = depth(camera, {.35f, 1.4f, 4});
    const auto& plan = layer.resident().begin()->second;
    const auto entry = std::find_if(plan.doors.begin(), plan.doors.end(), [](const viewer::HouseInteriorDoor& d) { return d.entrance == 1; });
    require(entry != plan.doors.end(), "Rendering fixture needs its front anteroom door");
    doors.setAngle(entry->id, -1.5f);
    renderer.updateFloorplans(layer); renderer.render(camera);
    require(depth(camera, {.35f, 1.4f, 4}) > closed + .00001f, "The interactive interior panel must visibly uncover its doorway");
    // Camera sees the entry partition, the swung panel and the living space.
    camera.setPosition({1.1f, 1.65f, 5.8f}); camera.setOrientation(-122, -5);
    renderer.setShadows(true); renderer.updateDoors(doors); renderer.render(camera);
    if (!capture.empty()) renderer.writeScreenshot(capture);
    renderer.setNight(true); renderer.setFlashlight(true); renderer.setFlashlightShadows(true); renderer.render(camera);
    if (!capture.empty()) renderer.writeScreenshot(capture.string() + ".night.ppm");
    if (!plan.stairs.empty()) {
        for (const auto& door : plan.doors) doors.setAngle(door.id, door.panel.inward ? -1.5f : 1.5f);
        renderer.updateFloorplans(layer);
        renderer.setNight(false); renderer.setFlashlight(false);
        const auto& stair = plan.stairs.front();
        const auto& room = plan.rooms.at(stair.room);
        glm::vec2 center(0); for (const auto p : room.polygon) center += p / float(room.polygon.size());
        const auto aim = [&](glm::vec3 eye, glm::vec3 target) {
            const auto d = target - eye;
            camera.setPosition(eye);
            camera.setOrientation(glm::degrees(std::atan2(d.z, d.x)),
                glm::degrees(std::atan2(d.y, glm::length(glm::vec2(d.x, d.z)))));
        };
        auto direction = stair.top - stair.bottom; direction.y = 0; direction = glm::normalize(direction);
        auto towardRoom = glm::vec3(center.x, plan.floor, center.y) - stair.bottom; towardRoom.y = 0;
        towardRoom -= direction * glm::dot(towardRoom, direction); towardRoom = glm::normalize(towardRoom);
        aim(stair.bottom + direction * .3f + towardRoom * 1.2f + glm::vec3(0, 1.65f, 0),
            (stair.bottom + stair.top) * .5f + glm::vec3(0, .4f, 0));
        renderer.setShadows(false); renderer.render(camera);
        if (!capture.empty()) renderer.writeScreenshot(capture.string() + ".stairs.ppm");
        aim(stair.top + direction * .5f + glm::vec3(0, 1.65f, 0), (stair.bottom + stair.top) * .5f);
        renderer.render(camera);
        if (!capture.empty()) renderer.writeScreenshot(capture.string() + ".upper.ppm");
        const auto upperDoor = std::find_if(plan.doors.begin(), plan.doors.end(), [](const viewer::HouseInteriorDoor& d) {
            return d.storey > 0 && !d.panel.inward;
        });
        require(upperDoor != plan.doors.end(), "Rendering fixture must exercise an upper living-room doorway");
        const auto& panel = upperDoor->panel;
        aim(panel.center - panel.outward * 1.25f + glm::vec3(0, .35f, 0), panel.center);
        doors.setAngle(upperDoor->id, 0); renderer.updateFloorplans(layer); renderer.render(camera);
        const auto closedUpper = depth(camera, panel.center);
        doors.setAngle(upperDoor->id, 1.5f); renderer.updateFloorplans(layer); renderer.render(camera);
        require(depth(camera, panel.center) > closedUpper + .00001f,
                "An upper-storey interactive panel must uncover the doorway into its stair area");
        if (!capture.empty()) renderer.writeScreenshot(capture.string() + ".upper-rooms.ppm");
    }
    renderer.setNight(false); renderer.setFlashlight(false); renderer.setShadows(true);
    camera.setPosition({3, 6.1f, 0}); camera.setOrientation(0, 0);
    renderer.render(camera);
    if (!capture.empty()) renderer.writeScreenshot(capture.string() + ".roof-seam.ppm");
    const auto seam = pixel(camera, {5, 6.45f, 0});
    const auto wall = pixel(camera, {5, 6.0f, 0});
    float seamDifference = 0;
    for (int channel = 0; channel < 3; ++channel) seamDifference += std::abs(int(seam[channel]) - int(wall[channel]));
    require(seamDifference < 12, "Closed ceiling must block the sunlight stripe at the existing wall/roof seam");
    renderer.setShadows(false); renderer.render(camera);
    const auto unshadowed = pixel(camera, {5, 6.45f, 0});
    int sunlightDifference = 0;
    for (int channel = 0; channel < 3; ++channel) sunlightDifference += int(unshadowed[channel]) - int(seam[channel]);
    require(sunlightDifference > 20, "Disabling shadows must also bypass the interior shell's sun occlusion");
    renderer.setShadows(true);
    camera.setPosition({0, 4.4f, 4.5f}); camera.setOrientation(180, -17);
    renderer.render(camera);
    if (!capture.empty()) renderer.writeScreenshot(capture.string() + ".floor-seam.ppm");
    const auto floorSeam = pixel(camera, {-4.97f, 3, 4.5f});
    const auto floorInside = pixel(camera, {-4.3f, 3, 4.5f});
    int floorDifference = 0;
    for (int channel = 0; channel < 3; ++channel) floorDifference += std::abs(int(floorSeam[channel]) - int(floorInside[channel]));
    require(floorDifference < 12, "Closed exterior wall must block sunlight at its upper-floor contact edge");
    camera.setPosition({3, 4.4f, 3}); camera.setOrientation(50, 0);
    renderer.render(camera);
    if (!capture.empty()) renderer.writeScreenshot(capture.string() + ".corner-seam.ppm");
    const auto cornerSeam = pixel(camera, {5, 4.5f, 5.97f});
    const auto wallInside = pixel(camera, {5, 4.5f, 5.4f});
    int cornerDifference = 0;
    for (int channel = 0; channel < 3; ++channel) cornerDifference += std::abs(int(cornerSeam[channel]) - int(wallInside[channel]));
    require(cornerDifference < 12, "Closed wall corner must block sunlight along the vertical contact edge");
    layer.update({glm::vec3(100)}); renderer.updateFloorplans(layer); renderer.render(camera);
    require(pixel(camera, {5, 4.5f, 5.97f}) == bareCorner,
            "Eviction must clear the exact interior shadow and restore the bare shell's lighting");
    GLboolean depthWrite = GL_FALSE; glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWrite);
    require(depthWrite == GL_TRUE, "Streamed interiors must retain opaque depth writes");
    viewer::Renderer::checkErrors("floorplan camera, shadow, door movement and eviction");
}
void renderIrregular(const std::filesystem::path& capture) {
    auto model = house_fixture::outlined({{-5,-6},{-5,6},{2,6},{2,3},{5,3},{5,-6}});
    auto physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::DoorLayer doors(viewer::prepareDoorPanels(model), physics);
    viewer::CollisionWorld collision(model, {}, &doors);
    viewer::FloorplanLayer layer(model, doors, collision);
    viewer::Renderer renderer(model, VIEWER_SHADER_DIR);
    renderer.resize(width, height); renderer.setHaze(false); renderer.setGroundFog(false);
    viewer::Camera camera; camera.setPosition({4, 10, 6}); camera.setOrientation(-90, -83);
    renderer.render(camera);
    const auto bare = depth(camera, {4, 0, 4.8f});
    doors.setAngle(0, .7f); layer.update({doors.panel(0).center});
    require(layer.residentCount() == 1, "Rendering fixture must accept its concave house");
    renderer.updateDoors(doors); renderer.updateFloorplans(layer); renderer.render(camera);
    require(std::abs(depth(camera, {4, 0, 4.8f}) - bare) < .000001f,
            "Missing building corners must retain exterior depth rather than receive a bounding-box floor/ceiling");
    for (const auto& door : layer.resident().begin()->second.doors) doors.setAngle(door.id, door.panel.inward ? -1.5f : 1.5f);
    renderer.updateFloorplans(layer);
    camera.setPosition({-1, 4.5f, 4.5f}); camera.setOrientation(12, -7);
    renderer.render(camera);
    if (!capture.empty()) renderer.writeScreenshot(capture.string() + ".irregular.ppm");
    camera.setPosition({16, 16, 20}); camera.setOrientation(-128.66f, -27);
    renderer.render(camera);
    if (!capture.empty()) renderer.writeScreenshot(capture.string() + ".irregular-overview.ppm");

    // Isolate the analytic sun mask: an exterior receiver in the missing corner
    // remains visible from inside after omitting the original opaque shell.
    viewer::Model probe; probe.materials.resize(1); probe.materials[0].doubleSided = true;
    probe.materials[0].baseColor = {.7f, .7f, .7f, 1};
    interior_fixture::quad(probe, {4, 3.5f, 4.5f}, {4, 5.5f, 4.5f}, {4, 5.5f, 5.5f}, {4, 3.5f, 5.5f}, "exterior_probe");
    interior_fixture::bounds(probe);
    viewer::Renderer mask(probe, VIEWER_SHADER_DIR); mask.resize(width, height);
    mask.setHaze(false); mask.setGroundFog(false);
    camera.setPosition({1.98f, 4.4f, 5}); camera.setOrientation(0, 0);
    mask.render(camera); const auto exterior = pixel(camera, {4, 4.4f, 5});
    mask.updateFloorplans(layer); mask.render(camera);
    require(pixel(camera, {4, 4.4f, 5}) == exterior, "An irregular shell's exact sun mask must not shade the exterior recess");
    viewer::Renderer::checkErrors("irregular floorplan depth and exact sun containment");
}
void renderApartment(const std::filesystem::path& capture) {
    auto model = apartment_fixture::block(48,14,11);
    auto physics = std::make_shared<viewer::PhysicsWorld>();
    viewer::DoorLayer doors(viewer::prepareDoorPanels(model),physics);
    viewer::CollisionWorld collision(model,{},&doors);
    viewer::FloorplanLayer layer(model,doors,collision);
    doors.setAngle(0,.7f);layer.update({doors.panel(0).center});
    require(layer.residentCount()==1,"Apartment rendering fixture must generate an interior");
    const auto& plan=layer.resident().begin()->second;
    require(plan.kind==viewer::HouseFloorplan::Kind::PrefabApartmentBlock && plan.upperFloors.size()==10,
            "Apartment renderer must receive all eleven floors");
    viewer::Renderer renderer(model,VIEWER_SHADER_DIR);
    renderer.resize(width,height);renderer.setHaze(false);renderer.setGroundFog(false);renderer.setShadows(false);
    viewer::Camera camera;
    const auto aim=[&](glm::vec3 eye,glm::vec3 target){
        const auto d=target-eye;camera.setPosition(eye);
        camera.setOrientation(glm::degrees(std::atan2(d.z,d.x)),glm::degrees(std::atan2(d.y,glm::length(glm::vec2(d.x,d.z)))));
    };
    renderer.updateDoors(doors);renderer.updateFloorplans(layer);
    for(const auto level:{std::size_t(0),std::size_t(10)}){
        const auto door=std::find_if(plan.doors.begin(),plan.doors.end(),[&](const auto& d){return d.storey==level;});
        require(door!=plan.doors.end(),"Every apartment floor must have an interactive entrance");
        aim(door->panel.center+door->panel.outward*.7f+glm::vec3(0,.6f,0),door->panel.center);
        renderer.render(camera);const auto closed=depth(camera,door->panel.center);
        doors.setAngle(door->id,-1.5f);renderer.updateFloorplans(layer);renderer.render(camera);
        require(depth(camera,door->panel.center)>closed+.00001f,"An apartment entrance must visibly open on ground and top floors");
    }
    const auto corridor=std::find_if(plan.rooms.begin(),plan.rooms.end(),[](const auto& r){return r.storey==0 && r.kind==viewer::HouseRoom::Kind::Corridor;});
    require(corridor!=plan.rooms.end(),"Slab rendering fixture must expose a common corridor");
    glm::vec2 center(0);for(const auto p:corridor->polygon)center+=p/float(corridor->polygon.size());
    const auto& stair=plan.stairs.front();
    for(const auto& door:plan.doors)doors.setAngle(door.id,-1.2f);
    renderer.updateFloorplans(layer);renderer.setShadows(true);
    aim({center.x,plan.floor+1.7f,center.y},stair.bottom+glm::vec3(0,1.3f,0));
    renderer.render(camera);if(!capture.empty())renderer.writeScreenshot(capture.string()+".apartments.ppm");
    auto direction=stair.top-stair.bottom;direction.y=0;direction=glm::normalize(direction);
    const auto side=glm::normalize(glm::cross(glm::vec3(0,1,0),direction));
    aim(stair.bottom-direction*.45f+side*.55f+glm::vec3(0,1.7f,0),stair.top+glm::vec3(0,.6f,0));
    renderer.render(camera);if(!capture.empty())renderer.writeScreenshot(capture.string()+".apartment-stairs.ppm");
    viewer::Renderer::checkErrors("eleven-storey apartment floors, doors and stair core");
}
void renderComplex(const std::filesystem::path& capture) {
    for (int variant=0;variant<2;++variant) {
        auto model=variant==0 ? section_fixture::courtyard(2) : section_fixture::angled(2);
        auto physics=std::make_shared<viewer::PhysicsWorld>();
        viewer::DoorLayer doors(viewer::prepareDoorPanels(model),physics);
        viewer::CollisionWorld collision(model,{},&doors);
        viewer::FloorplanLayer layer(model,doors,collision);
        doors.setAngle(0,1.5f); layer.update({doors.panel(0).center});
        require(layer.residentCount()==1 && layer.resident().begin()->second.kind==viewer::HouseFloorplan::Kind::ComplexBuilding,
            "Complex rendering fixture must produce a connected section interior");
        const auto& plan=layer.resident().begin()->second;
        for (const auto& door:plan.doors) doors.setAngle(door.id,door.panel.inward ? -1.5f : 1.5f);
        auto display=model;
        // Diagnostic roof-off view keeps original draw IDs for live doors.
        for (auto& draw:display.draws) if (draw.name=="building_sections_roof")
            draw.transform=glm::translate(glm::mat4(1),{0,-1000,0})*draw.transform;
        viewer::Renderer renderer(display,VIEWER_SHADER_DIR); renderer.resize(width,height);
        renderer.setHaze(false); renderer.setGroundFog(false); renderer.setShadows(false);
        renderer.updateDoors(doors); renderer.updateFloorplans(layer);
        viewer::Camera camera;
        const auto aim=[&](glm::vec3 eye,glm::vec3 target) {
            const auto d=target-eye; camera.setPosition(eye);
            camera.setOrientation(glm::degrees(std::atan2(d.z,d.x)),glm::degrees(std::atan2(d.y,glm::length(glm::vec2(d.x,d.z)))));
        };
        aim({38,37,44},{12,1,12}); renderer.render(camera);
        if (!capture.empty()) renderer.writeScreenshot(capture.string()+(variant==0 ? ".courtyard.ppm" : ".angled.ppm"));
        if (!plan.stairs.empty()) {
            const auto& stair=plan.stairs.front();
            const auto& room=plan.rooms.at(stair.room);
            glm::vec2 center(0); for (const auto p:room.polygon) center+=p/float(room.polygon.size());
            aim({center.x,plan.floorHeight(room.storey)+1.65f,center.y},(stair.bottom+stair.top)*.5f+glm::vec3(0,.6f,0));
            renderer.setShadows(true); renderer.render(camera);
            if (!capture.empty()) renderer.writeScreenshot(capture.string()+(variant==0 ? ".courtyard-interior.ppm" : ".angled-interior.ppm"));
        }
        viewer::Renderer::checkErrors("complex courtyard/angled interior rendering");
    }
}
} // namespace
int main(int argc, char** argv) {
    if (!glfwInit()) return 77;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    auto* window = glfwCreateWindow(width, height, "floorplan regression", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 77; }
    glfwMakeContextCurrent(window);
    int result = 0;
    try {
        std::cout << "OpenGL renderer: " << glGetString(GL_RENDERER) << '\n';
        render(argc > 1 ? argv[1] : std::filesystem::path{});
        renderIrregular(argc > 1 ? argv[1] : std::filesystem::path{});
        renderApartment(argc > 1 ? argv[1] : std::filesystem::path{});
        renderComplex(argc > 1 ? argv[1] : std::filesystem::path{});
        std::cout << "Floorplan rendering tests passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; result = 1; }
    glfwDestroyWindow(window); glfwTerminate();
    return result;
}
