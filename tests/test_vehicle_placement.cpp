#include "vehicle_placement.hpp"
#include "file_fingerprint.hpp"
#include "physics_world.hpp"
#include <json.hpp>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
using nlohmann::json;
void require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
template<class F> void rejects(F run) {
    bool rejected=false;try {run();} catch(const std::exception&) {rejected=true;}
    require(rejected,"Invalid placement publication accepted");
}
}
int main(int argc,char** argv) {
    try {
        require(argc==2,"Fixture path required");
        const std::filesystem::path root(argv[1]);std::filesystem::create_directories(root);
        const auto map=root/"map.glb", path=root/"abandoned_cars.instances.json";
        // A hash fixture, deliberately not a real scene: this loader never reads
        // geometry from cached terrain or reloads a map just to place vehicles.
        std::ofstream(map,std::ios::binary)<<"map fixture";
        require(!viewer::loadVehiclePlacements(root/"missing"/"map.glb"),"Missing sidecar must use nearby-car fallback");
        const auto q=glm::angleAxis(.2f,glm::vec3(0,0,1))*glm::angleAxis(.7f,glm::vec3(0,1,0));
        json document={{"schema","godollo.abandoned-car-placements"},{"version",1},
            {"map",{{"map_sha256",viewer::fileSha256(map)}}},
            {"transforms",{{"units","meters"},{"asset_axes",{{"forward","+Z"},{"up","+Y"},{"right","+X"}}}}},
            {"counts",{{"instances",1}}},
            {"instances",{{{"id","car/1"},{"position",{5,8,7}},{"yaw_rad",0},
                            {"rotation_xyzw",{q.x,q.y,q.z,q.w}}}}}};
        const auto publish=[&](const json& d) {std::ofstream(path)<<d;};
        publish(document);
        const auto placements=viewer::loadVehiclePlacements(map);
        require(placements && placements->size()==1,"Valid placement not loaded");
        auto physics=std::make_shared<viewer::PhysicsWorld>();
        std::vector<viewer::VehicleConfig> types(16);
        viewer::VehicleLayer cars(physics,types);
        viewer::placeVehicles(cars,*placements,123);
        const auto pose=cars.pose(0);
        require(glm::distance(pose.chassis.center-pose.chassis.rotation*glm::vec3(0,.68f,0),glm::vec3(5,8,7))<1e-5f,
                "Ground-contact pivot was not converted along tilted local up");
        require(glm::distance(pose.chassis.rotation*glm::vec3(0,0,-1),q*glm::vec3(0,0,1))<1e-5f,
                "Placement heading is reversed or yaw_rad overrode authoritative rotation");
        require(glm::distance(pose.chassis.rotation*glm::vec3(0,1,0),q*glm::vec3(0,1,0))<1e-5f,
                "Ground tilt was lost");
        for(std::size_t i=0;i<4;++i)
            require(glm::distance(glm::vec3(pose.wheels[i][3]),pose.chassis.center+pose.chassis.rotation*types[0].wheels[i])<1e-5f,
                    "Wheel anchors did not inherit full initial rotation");
        std::vector<viewer::VehiclePlacement> fleet;
        for(int i=0;i<1000;++i) fleet.push_back({std::to_string(i),{float(i)*6,1,0},glm::quat(1,0,0,0)});
        viewer::VehicleLayer first(physics,types),second(physics,types),third(physics,types);
        viewer::placeVehicles(first,fleet,42);viewer::placeVehicles(second,fleet,42);viewer::placeVehicles(third,fleet,43);
        std::set<std::size_t> selected;bool changed=false;
        for(std::size_t i=0;i<fleet.size();++i) {
            selected.insert(first.type(i));changed=changed || first.type(i)!=third.type(i);
            require(first.type(i)==second.type(i),"Same seed did not reproduce type choices");
        }
        require(first.count()==1000 && selected.size()==types.size() && changed,"Fleet types were not randomized across the full catalog");
        for(int mode=0;mode<11;++mode) {
            auto bad=document;
            if(mode==0) bad["map"]["map_sha256"]="stale";
            if(mode==1) bad["instances"][0]["rotation_xyzw"]={0,0,0,0};
            if(mode==2) {bad["instances"].push_back(bad["instances"][0]);bad["counts"]["instances"]=2;}
            if(mode==3) bad["instances"][0]["position"]={1,2};
            if(mode==4) bad["transforms"]["asset_axes"]["forward"]="-Z";
            if(mode==5) bad["version"]=2;
            if(mode==6) bad["counts"]["instances"]=2;
            if(mode==7) bad["instances"]=json::array({nullptr});
            if(mode==8) bad["version"]=1.0;
            if(mode==9) {
                bad["instances"]=nlohmann::json::array();
                for(std::size_t i=0;i<=viewer::maxVehicleInstances;++i) bad["instances"].push_back(document["instances"][0]);
                bad["counts"]["instances"]=viewer::maxVehicleInstances+1;
            }
            if(mode==10) bad["instances"][0]["position"][0]="invalid";
            publish(bad);rejects([&] {viewer::loadVehiclePlacements(map);});
        }
        auto empty=document;empty["instances"]=json::array();empty["counts"]["instances"]=0;publish(empty);
        require(viewer::loadVehiclePlacements(map)->empty(),"Empty sidecar must suppress the nearby fallback");
        std::cout<<"Vehicle placement transforms, validation and 1000-car randomization passed\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
