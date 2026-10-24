#include "renderer.hpp"
#include "vehicle_layer.hpp"
#include "physics_world.hpp"
#include "interior_fixture.hpp"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
void lookAt(viewer::Camera& camera,glm::vec3 eye,glm::vec3 target) {
    camera.setPosition(eye); const auto d=target-eye;
    camera.setOrientation(glm::degrees(std::atan2(d.z,d.x)),glm::degrees(std::atan2(d.y,glm::length(glm::vec2(d.x,d.z)))));
}
std::vector<unsigned char> pixels() {
    std::vector<unsigned char> result(960*640*4);
    glReadBuffer(GL_COLOR_ATTACHMENT0);glPixelStorei(GL_PACK_ALIGNMENT,1);
    glReadPixels(0,0,960,640,GL_RGBA,GL_UNSIGNED_BYTE,result.data());
    return result;
}
void equivalent(const std::vector<unsigned char>& a,const std::vector<unsigned char>& b) {
    std::size_t different=0;
    for(std::size_t i=0;i<a.size();++i) if(std::abs(int(a[i])-int(b[i]))>3) ++different;
    require(different<a.size()/1000,"Instanced image differs from individual draws beyond rasterization tolerance");
}
void move(viewer::VehicleLayer& cars,std::size_t id,glm::vec3 delta) {
    auto pose=cars.pose(id);pose.chassis.center+=delta;
    for(auto& wheel:pose.wheels) wheel[3]+=glm::vec4(delta,0);
    cars.setPose(id,pose,{});
}
void run(const std::filesystem::path& models,const std::filesystem::path& configs,const std::filesystem::path& output) {
    std::filesystem::create_directories(output);
    viewer::Model scene; scene.materials.push_back({});
    interior_fixture::quad(scene,{-400,0,-400},{-400,0,400},{400,0,400},{400,0,-400},"terrain");
    interior_fixture::bounds(scene);
    auto physics=std::make_shared<viewer::PhysicsWorld>();
    viewer::Renderer renderer(scene,VIEWER_SHADER_DIR);
    renderer.resize(960,640); renderer.setHaze(false); renderer.setGroundFog(false); renderer.setShadows(false);
    renderer.setVehicleAssets(models);
    const auto types=viewer::loadVehicleConfigs(configs);
    for(const auto& type:types) {
        require(viewer::loadVehicleLods(models,type.id,&type).size()==4,"Prepared asset type failed validation");
        viewer::VehicleLayer car(physics,{type}); car.add({0,.8f,0});
        renderer.setVehicleInstancing(false);
        renderer.updateVehicles(car);
        viewer::Camera camera;
        lookAt(camera,{5,3,-6},{0,1,0}); renderer.render(camera);
        const auto reference=pixels();
        renderer.setVehicleInstancing(true);renderer.render(camera);
        equivalent(reference,pixels());
        require(renderer.vehicleStats().boundsUpdated==0,"Unchanged car unnecessarily refitted its bounds");
        const auto near=renderer.vehicleStats();
        require(near.levels[0]==1 && near.drawCalls>0,"Near car did not draw LOD0");
        renderer.writeScreenshot(output/(type.id+".ppm"));
        const float distances[]{30,70,140};
        for(std::size_t level=1;level<4;++level) {
            lookAt(camera,{0,2,-distances[level-1]},{0,1,0}); renderer.render(camera);
            const auto stats=renderer.vehicleStats();
            require(stats.levels[level]==1 && stats.triangles<=near.triangles,"Distance failed to select reduced geometry");
        }
        camera.cycleViewMode(); camera.cycleViewMode();
        renderer.render(camera);
        require(renderer.vehicleStats().levels[0]==1,"Scope failed to restore full mesh");
        lookAt(camera,{0,2,-350},{0,1,0});
        // New camera at normal FOV, to test distance removal.
        viewer::Camera far; lookAt(far,{0,2,-350},{0,1,0}); renderer.render(far);
        require(renderer.vehicleStats().distanceCulled==1 && renderer.vehicleStats().drawCalls==0,"Distant car was not culled");
        renderer.setVehicleDistanceDetail(false); renderer.render(far);
        require(renderer.vehicleStats().levels[0]==1,"Disabled detail did not retain full car");
        renderer.setVehicleDistanceDetail(true);
        renderer.updateVehicles(car,0); renderer.render(far);
        require(renderer.vehicleStats().levels[0]==1,"Occupied car lost near detail");
        renderer.updateVehicles(car);
        viewer::Camera shadowCamera; lookAt(shadowCamera,{5,3,-6},{0,1,0});
        renderer.setShadows(true); renderer.setNight(true); renderer.setFlashlightShadows(true); renderer.render(shadowCamera);
        const auto shadowed=pixels();
        renderer.setVehicleInstancing(false);renderer.render(shadowCamera);equivalent(shadowed,pixels());
        renderer.setVehicleInstancing(true);
        renderer.setNight(false); renderer.setShadows(false);
        GLboolean depth=GL_FALSE; glGetBooleanv(GL_DEPTH_WRITEMASK,&depth);
        require(depth==GL_TRUE && glIsEnabled(GL_BLEND)==GL_FALSE,"Vehicle material draws leaked GL state");
        viewer::Renderer::checkErrors("vehicle LoD material and shadow passes");
    }
    // Repeated types reuse uploaded geometry/textures. Isolated diagnostics time
    // complete render calls with a blocking query, never production render timing.
    viewer::VehicleLayer fleet(physics,types);
    for(int z=0;z<12;++z) for(int x=0;x<16;++x) fleet.add({(x-7.5f)*4.5f,.8f,-float(z)*6},0,(x+z)%types.size());
    renderer.updateVehicles(fleet);
    viewer::Camera camera; lookAt(camera,{0,20,35},{0,0,-30});
    std::ofstream report(output/"timing.csv");
    report<<"gpu,mode,round,gpu_ms,wall_ms,triangles,draw_calls,instanced_draws,transparent_draws,visibility_tests\n";
    std::cout<<"OpenGL renderer: "<<glGetString(GL_RENDERER)<<'\n';
    for(int round=0;round<4;++round) for(int mode=0;mode<3;++mode) {
        renderer.setVehicleDistanceDetail(mode!=0);renderer.setVehicleInstancing(mode==2);
        for(int warm=0;warm<6;++warm) renderer.render(camera);
        glFinish(); GLuint query; glGenQueries(1,&query);
        const auto start=std::chrono::steady_clock::now();
        glBeginQuery(GL_TIME_ELAPSED,query);
        for(int frame=0;frame<20;++frame) renderer.render(camera);
        glEndQuery(GL_TIME_ELAPSED); glFinish();
        const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/20;
        GLuint64 ns=0; glGetQueryObjectui64v(query,GL_QUERY_RESULT,&ns); glDeleteQueries(1,&query);
        report<<'"'<<glGetString(GL_RENDERER)<<"\","<<(mode==0?"near_individual":mode==1?"lod_individual":"lod_instanced")<<','<<round<<','<<double(ns)/20000000<<','<<elapsed<<','<<renderer.vehicleStats().triangles<<','<<renderer.vehicleStats().drawCalls<<','<<renderer.vehicleStats().instancedDrawCalls<<','<<renderer.vehicleStats().transparentDrawCalls<<','<<renderer.vehicleStats().testedInstances<<'\n';
    }
    renderer.setVehicleDistanceDetail(true);renderer.setVehicleInstancing(false);renderer.render(camera);
    const auto individual=renderer.vehicleStats();const auto reference=pixels();
    renderer.setVehicleInstancing(true);renderer.render(camera);equivalent(reference,pixels());
    require(renderer.vehicleStats().triangles==individual.triangles && renderer.vehicleStats().drawCalls<individual.drawCalls/2,
            "Instancing did not reduce fleet submissions while preserving geometry");
    renderer.writeScreenshot(output/"fleet.ppm");
    viewer::VehicleLayer scattered(physics,{types[0]});
    for(int i=0;i<1000;++i) scattered.add({1000.f+float(i%40)*20,.8f,-float(i/40)*20});
    renderer.updateVehicles(scattered);
    viewer::Camera close;lookAt(close,{5,3,6},{0,1,0});renderer.render(close);
    require(renderer.vehicleStats().drawCalls==0 && renderer.vehicleStats().testedInstances<100,
            "Spatial index scanned/drew the distant population");
    move(scattered,800,glm::vec3(0,.8f,0)-scattered.pose(800).chassis.center);
    renderer.render(close); // Explicit poses must invalidate the cache without updateVehicles.
    require(renderer.vehicleStats().levels[0]==1 && renderer.vehicleStats().boundsUpdated==1,
            "Moving a previously culled car failed to refit its visibility bounds");
    std::cout<<"1000-car indexed query: "<<renderer.vehicleStats().rangeCandidates<<" range candidates, "
             <<renderer.vehicleStats().testedNodes<<" nodes, "<<renderer.vehicleStats().testedInstances<<" individual bounds tested\n";
    renderer.render(close);require(renderer.vehicleStats().boundsUpdated==0,"Stationary population refitted every frame");
    scattered.setDriven(800,true);
    const auto revision=scattered.poseRevision(800);
    physics->step(1.f/60);
    require(scattered.poseRevision(800)!=revision,"Physics motion did not invalidate vehicle pose cache");
    renderer.render(close);require(renderer.vehicleStats().boundsUpdated>0,"Simulated movement left stale render bounds");
    move(scattered,800,{1000,0,0});renderer.render(close);
    require(renderer.vehicleStats().drawCalls==0,"A car moved away but remained visible in the spatial index");
    viewer::Renderer::checkErrors("vehicle fleet");
}
}
int main(int argc,char** argv) {
    if(argc!=4 && argc!=5) return 1;
    if(!std::filesystem::exists(std::filesystem::path(argv[1])/"lods.json")) return 77;
    if (argc==5) {
        if (std::string(argv[4])!="--cpu-only") return 1;
        try {
            for (const auto& type : viewer::loadVehicleConfigs(argv[2]))
                require(viewer::loadVehicleLods(argv[1],type.id,&type).size()==4,"Prepared asset type failed validation");
            std::cout<<"All 16 vehicle types and 64 LoDs match their configured fitting frames\n";
            return 0;
        } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
    }
    if(!glfwInit()) return 77;
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE); glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    auto* window=glfwCreateWindow(960,640,"Vehicle LoDs",nullptr,nullptr);
    if(!window) { glfwTerminate(); return 77; }
    glfwMakeContextCurrent(window); viewer::initializeOpenGL();
    int result=0;
    try {run(argv[1],argv[2],argv[3]);} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';result=1;}
    glfwDestroyWindow(window); glfwTerminate(); return result;
}
