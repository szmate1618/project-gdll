#include "vehicle_lod.hpp"
#include "file_fingerprint.hpp"
#include <json.hpp>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
using nlohmann::json;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void fixture(const std::filesystem::path& path) {
    const float vertices[]{-1,0,0, 1,0,0, 0,1,0};
    json d = {{"asset",{{"version","2.0"}}}, {"buffers", {{{"byteLength",36}}}},
        {"bufferViews", {{{"buffer",0},{"byteLength",36}}}},
        {"accessors", {{{"bufferView",0},{"componentType",5126},{"count",3},{"type","VEC3"},{"min",{-1,0,0}},{"max",{1,1,0}}}}},
        {"meshes", {{{"primitives", {{{"attributes",{{"POSITION",0}}}}}}}}},
        {"nodes", {{{"mesh",0}}}}, {"scenes", {{{"nodes",{0}}}}}, {"scene",0}};
    auto text = d.dump(); while (text.size()%4) text += ' ';
    const std::uint32_t header[]{0x46546c67,2,static_cast<std::uint32_t>(28+text.size()+sizeof(vertices)),
                               static_cast<std::uint32_t>(text.size()),0x4e4f534a};
    const std::uint32_t chunk[]{sizeof(vertices),0x004e4942};
    std::ofstream out(path,std::ios::binary);
    out.write(reinterpret_cast<const char*>(header),sizeof(header)); out << text;
    out.write(reinterpret_cast<const char*>(chunk),sizeof(chunk));
    out.write(reinterpret_cast<const char*>(vertices),sizeof(vertices));
}
int main(int argc, char** argv) {
    try {
        require(argc == 2,"Expected scratch path");
        const std::filesystem::path root(argv[1]); std::filesystem::create_directories(root);
        require(viewer::loadVehicleLods(root/"missing","car").empty(),"Missing cache must fall back");
        constexpr float p=1.7320508f;
        require(viewer::vehicleCameraDetail(21,p).level==0,"Hysteresis must keep near level");
        require(viewer::vehicleCameraDetail(23,p).level==1,"Medium car needs LOD1");
        require(viewer::vehicleCameraDetail(19,p,1).level==1,"Hysteresis must hold previous level");
        require(viewer::vehicleCameraDetail(17,p,1).level==0,"Approaching car must regain detail");
        require(viewer::vehicleCameraDetail(60,p).level==2,"Distant car needs LOD2");
        require(viewer::vehicleCameraDetail(120,p).level==3,"Far car needs LOD3");
        require(!viewer::vehicleCameraDetail(301,p).visible,"Distance cutoff failed");
        require(viewer::vehicleCameraDetail(1000,p,3,false).level==0 && viewer::vehicleCameraDetail(1000,p,3,false).visible,"Disabled policy must retain near models");
        require(viewer::vehicleCameraDetail(300,114.59f,3).level==0,"Scope must retain apparent detail");
        require(viewer::vehicleCameraDetail(20,0).visible && viewer::vehicleCameraDetail(std::numeric_limits<float>::quiet_NaN(),p).level==0,"Invalid view must keep geometry");
        fixture(root/"source.glb"); fixture(root/"level.glb");
        json entry={{"source","source.glb"},{"source_sha256",viewer::fileSha256(root/"source.glb")},{"levels",json::array()}};
        for(int i=0;i<4;++i) entry["levels"].push_back({{"path","level.glb"},{"sha256",viewer::fileSha256(root/"level.glb")},{"triangles",1}});
        const auto publish=[&](const json& value) { std::ofstream(root/"lods.json") << json{{"version",1},{"models",{{"car",value}}}}; };
        publish(entry); require(viewer::loadVehicleLods(root,"car").size()==4,"Valid cache must load all levels");
        require(viewer::loadVehicleLods(root,"other").empty(),"Missing type must use prototype");
        auto bad=entry; bad["source_sha256"]="old"; publish(bad);
        require(viewer::loadVehicleLods(root,"car").empty(),"Stale source must fall back atomically");
        bad=entry; bad["levels"][2]["sha256"]="bad"; publish(bad);
        require(viewer::loadVehicleLods(root,"car").empty(),"Corrupt LoD must reject whole set");
        bad=entry; bad["levels"][2]["triangles"]=2; publish(bad);
        require(viewer::loadVehicleLods(root,"car").empty(),"Manifest triangle count must match geometry");
        for(const auto* path:{"../outside.glb","/tmp/outside.glb","C:\\outside.glb"}) {
            bad=entry; bad["levels"][0]["path"]=path; publish(bad);
            require(viewer::loadVehicleLods(root,"car").empty(),"Unsafe path accepted");
        }
        bad=entry; bad["levels"].erase(3); publish(bad);
        require(viewer::loadVehicleLods(root,"car").empty(),"Partial cache accepted");
        std::cout << "Vehicle LoD policy and cache validation passed\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
