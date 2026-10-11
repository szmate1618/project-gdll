#include "forest_layer.hpp"
#include "forest_placement.hpp"
#include "collision_world.hpp"
#include "file_fingerprint.hpp"
#include <json.hpp>
#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>

namespace {
using Json = nlohmann::json;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
viewer::ForestPolygon shape() {
    return {{{0,0},{60,0},{60,20},{20,20},{20,60},{0,60},{0,0}},
            {{{4,4},{4,12},{12,12},{12,4},{4,4}}}};
}
void checkGeometry() {
    const auto polygon = shape();
    viewer::validateForestPolygon(polygon);
    require(std::abs(viewer::forestArea(polygon)-1936)<1e-8, "Wrong polygon area");
    require(!viewer::forestContains(polygon,{8,8}), "Hole classified as forest");
    require(!viewer::forestContains(polygon,{40,40}), "Concavity filled");
    const auto ground = [](glm::dvec2 p) { return std::optional<float>(p.x*.1+p.y*.2); };
    const auto meshes = viewer::forestVolume(polygon,ground,16,16);
    double area = 0;
    const auto& top = meshes.second;
    for (std::size_t i=0; i<top.indices.size(); i+=3) {
        const auto a=top.vertices[top.indices[i]].position;
        const auto b=top.vertices[top.indices[i+1]].position;
        const auto c=top.vertices[top.indices[i+2]].position;
        const auto normal=glm::cross(b-a,c-a);
        require(normal.y>=-1e-5f, "Canopy winding faces down");
        area+=normal.y*.5;
        if (normal.y>1e-6f) {
            const auto center=(a+b+c)/3.f;
            require(viewer::forestContains(polygon,{center.x,-center.z}), "Canopy triangle fills excluded area");
        }
    }
    require(std::abs(area-1936)<.01, "Canopy triangulation lost polygon area");
    for (const auto& vertex:top.vertices)
        require(std::abs(vertex.position.y-(*ground({vertex.position.x,-vertex.position.z})+16))<.001,
                "Canopy does not follow terrain in X east/Z south frame");
    bool repeats=false;
    for (const auto& vertex:meshes.first.vertices) if (vertex.uv.x>1) repeats=true;
    require(repeats, "Forest walls do not repeat their texture in metres");
    auto invalid=shape(); invalid.holes[0][1]={40,40};
    bool rejected=false;
    try {viewer::validateForestPolygon(invalid);} catch (const std::runtime_error&) {rejected=true;}
    require(rejected, "Malformed hole accepted");
}
void checkPlacement() {
    const auto polygon=shape();
    const auto first=viewer::forestTreeCandidates(polygon,"patch-A",123,5,12,20);
    (void)viewer::forestTreeCandidates(polygon,"unrelated",123,5,12,20);
    const auto second=viewer::forestTreeCandidates(polygon,"patch-A",123,5,12,20);
    const auto other=viewer::forestTreeCandidates(polygon,"patch-B",123,5,12,20);
    require(!first.empty() && first.size()==second.size(), "Placement is not repeatable");
    for (std::size_t i=0; i<first.size(); ++i) {
        require(first[i].eastNorth==second[i].eastNorth && first[i].height==second[i].height &&
                first[i].yaw==second[i].yaw, "Seeded placement changed between runs");
        require(viewer::forestContains(polygon,first[i].eastNorth), "Tree planted outside forest or in a hole");
        require(first[i].height>=12 && first[i].height<=20, "Tree height outside configured range");
    }
    require(first.front().eastNorth!=other.front().eastNorth, "Patch IDs do not influence placement");
    viewer::ForestPatch patch; patch.bounds={{-10,0,-10},{10,20,10}};
    require(viewer::forestUsesTrees(patch,{0,2,0},false,350), "Camera inside forest selected volume");
    require(!viewer::forestUsesTrees(patch,{1000,2,0},false,350), "Distant forest selected trees");
    require(viewer::forestUsesTrees(patch,{1000,2,0},true,350), "Scope failed to force individual trees");
    require(!viewer::forestUsesTrees(patch,{0,1000,0},false,350), "High aerial camera selected nearby trees");
}
void append32(std::vector<unsigned char>& bytes, std::uint32_t value) {
    for (int i=0;i<4;++i) bytes.push_back(static_cast<unsigned char>(value>>(i*8)));
}
struct Fixtures {
    std::filesystem::path directory=std::filesystem::current_path()/
        ("forest-fixtures-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::path map=directory/"map.glb", config=directory/"forests.json";
    Fixtures() {
        std::filesystem::create_directories(directory);
        std::ofstream(map)<<"fingerprinted synthetic map";
        // A tiny solid-green PNG; the viewer intentionally supports PNG/JPEG.
        const unsigned char png[]={0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0,0,0,0x0d,
            0x49,0x48,0x44,0x52,0,0,0,2,0,0,0,2,8,2,0,0,0,0xfd,0xd4,0x9a,0x73,
            0,0,0,0x16,0x49,0x44,0x41,0x54,0x78,0x9c,0x63,0x64,8,0x60,0x60,0x60,
            0x60,0x60,0x62,0x60,0x60,0x60,0x60,0x60,0,0,3,0xea,0,0x54,0x21,0xb8,
            0x51,0xca,0,0,0,0,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82};
        std::ofstream texture(directory/"tile.png",std::ios::binary);
        texture.write(reinterpret_cast<const char*>(png),sizeof(png));
        std::vector<unsigned char> binary;
        for (const auto p:{glm::vec3(-2,0,0),glm::vec3(2,0,0),glm::vec3(0,10,2)})
            for (int i=0;i<3;++i) {std::uint32_t bits; std::memcpy(&bits,&p[i],4); append32(binary,bits);}
        for (const auto i:{0u,1u,2u}) append32(binary,i);
        Json doc={
            {"asset",{{"version","2.0"}}},{"scene",0},
            {"scenes",Json::array({{{"nodes",{0}}}})},
            {"nodes",Json::array({{{"mesh",0}}})},
            {"meshes",Json::array({{{"primitives",Json::array({{{"attributes",{{"POSITION",0}}},{"indices",1},{"material",0}}})}}})},
            {"materials",Json::array({{{"alphaMode","MASK"},{"doubleSided",true},{"extensions",{{"KHR_materials_unlit",Json::object()}}}}})},
            {"extensionsUsed",{"KHR_materials_unlit"}},
            {"buffers",Json::array({{{"byteLength",binary.size()}}})},
            {"bufferViews",Json::array({{{"buffer",0},{"byteOffset",0},{"byteLength",36}},{{"buffer",0},{"byteOffset",36},{"byteLength",12}}})},
            {"accessors",Json::array({{{"bufferView",0},{"componentType",5126},{"count",3},{"type","VEC3"}},{{"bufferView",1},{"componentType",5125},{"count",3},{"type","SCALAR"}}})},
            {"extras",{{"units","meters"},{"upAxis","+Y"},{"origin","visible trunk base"},{"visibleHeightMeters",10}}}};
        auto text=doc.dump(); while (text.size()%4) text+=' ';
        std::vector<unsigned char> glb;
        append32(glb,0x46546c67); append32(glb,2); append32(glb,static_cast<std::uint32_t>(28+text.size()+binary.size()));
        append32(glb,static_cast<std::uint32_t>(text.size())); append32(glb,0x4e4f534a);
        glb.insert(glb.end(),text.begin(),text.end()); append32(glb,static_cast<std::uint32_t>(binary.size())); append32(glb,0x004e4942);
        glb.insert(glb.end(),binary.begin(),binary.end());
        std::ofstream tree(directory/"tree.glb",std::ios::binary);
        tree.write(reinterpret_cast<const char*>(glb.data()),glb.size());
    }
    ~Fixtures() {std::error_code error; std::filesystem::remove_all(directory,error);}
    Json document() const {
        return {{"schema","godollo.forest_patches"},{"version",1},
            {"source",{{"map_sha256",viewer::fileSha256(map)}}},
            {"coordinate_frame",{{"units","meters"},{"coordinate_order",{"east","north"}},{"crs","EPSG:32634"},{"origin_projected_m",{100000,200000}}}},
            {"runtime",{{"tree_asset","tree.glb"},{"side_texture","tile.png"},{"top_texture","tile.png"},{"tree_spacing_m",5}}},
            {"patches",Json::array({{{"id","A"},{"polygon",{{0,0},{20,0},{20,20},{0,20},{0,0}}},{"holes",Json::array()}},
                                   {{"id","B"},{"polygon",{{30,30},{60,30},{60,60},{30,60},{30,30}}},{"holes",Json::array()}}})}};
    }
    void write(const Json& doc) const {std::ofstream(config)<<doc.dump();}
};
viewer::Model terrainScene() {
    viewer::Model model; model.geographicFrame=viewer::Model::GeographicFrame{"EPSG:32634",{100000,200000}};
    model.boundsMin={0,0,-60}; model.boundsMax={60,18,0};
    viewer::Primitive terrain;
    for (const auto p:{glm::vec2(0,0),glm::vec2(60,0),glm::vec2(60,60),glm::vec2(0,60)}) {
        viewer::Vertex v; v.position={p.x,p.y,.1f*p.x+.2f*p.y}; terrain.vertices.push_back(v);
    }
    terrain.indices={0,1,2,0,2,3}; model.primitives.push_back(terrain);
    glm::mat4 root(1); root[1]={0,0,-1,0}; root[2]={0,1,0,0};
    model.draws.push_back({0,root,"terrain"});
    return model;
}
void checkLoader() {
    const Fixtures files; auto doc=files.document(); files.write(doc);
    const auto scene=terrainScene(); const viewer::CollisionWorld world(scene);
    viewer::TreeLayer trees;
    const auto layer=viewer::loadForestLayer(files.config,files.map,scene,world,trees);
    require(layer.patches.size()==2 && trees.assets.size()==1 && !trees.instances.empty(), "Runtime layer is empty");
    for (const auto& tree:trees.instances) {
        const auto feet=glm::vec3(tree.transform[3]);
        require(feet.z<0 && std::abs(feet.y-(feet.x*.1f-feet.z*.2f))<.002f, "Tree frame or terrain elevation is wrong");
    }
    std::map<std::string,glm::mat4> positions;
    for (const auto& tree:trees.instances) positions[tree.id]=tree.transform;
    std::swap(doc["patches"][0],doc["patches"][1]); files.write(doc);
    viewer::TreeLayer reordered;
    (void)viewer::loadForestLayer(files.config,files.map,scene,world,reordered);
    require(reordered.instances.size()==trees.instances.size(), "Reordering patches changed tree count");
    for (const auto& tree:reordered.instances) require(positions.at(tree.id)==tree.transform, "Reordering changed forest placements");
    doc["source"]["map_sha256"]=std::string(64,'0'); files.write(doc);
    bool rejected=false;
    try {(void)viewer::loadForestLayer(files.config,files.map,scene,world,trees);} catch (const std::runtime_error&) {rejected=true;}
    require(rejected && trees.instances.size()==positions.size() && trees.assets.size()==1, "Stale config accepted or mutated trees");
    doc=files.document(); doc["patches"][1]["id"]="A"; files.write(doc);
    rejected=false;
    try {(void)viewer::loadForestLayer(files.config,files.map,scene,world,trees);} catch (const std::runtime_error&) {rejected=true;}
    require(rejected && trees.instances.size()==positions.size(), "Failed partial load appended trees");
    doc=files.document(); doc["coordinate_frame"]["origin_projected_m"][0]=100100; files.write(doc);
    rejected=false;
    try {(void)viewer::loadForestLayer(files.config,files.map,scene,world,trees);} catch (const std::runtime_error&) {rejected=true;}
    require(rejected && trees.instances.size()==positions.size(), "Wrong coordinate frame accepted");
    doc=files.document(); files.write(doc);
    viewer::TreeLayer withDetected;
    withDetected.assets=trees.assets;
    withDetected.instances.push_back(trees.instances.front());
    const auto withDetection=viewer::loadForestLayer(files.config,files.map,scene,world,withDetected);
    require(withDetected.assets.size()==1, "Shared tree asset uploaded twice");
    require(withDetection.patches.front().detectedTrees==std::vector<std::size_t>{0}, "Detected tree not associated with its forest LoD");
    const auto existing=glm::vec3(withDetected.instances.front().transform[3]);
    for (std::size_t i=1;i<withDetected.instances.size();++i)
        require(glm::length(glm::vec2(withDetected.instances[i].transform[3].x-existing.x,
            withDetected.instances[i].transform[3].z-existing.z))>=3, "Tree generated too close to detector placement");
}
}
int main() {
    try {checkGeometry(); checkPlacement(); checkLoader(); std::cout<<"Forest geometry, placement and loader tests passed\n";}
    catch (const std::exception& e) {std::cerr<<e.what()<<'\n'; return 1;}
}
