#include "forest_layer.hpp"
#include "forest_placement.hpp"
#include "forest_bush.hpp"
#include "vegetation_blast.hpp"
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
void checkOldForest() {
    const viewer::ForestPolygon square{{{0,0},{500,0},{500,500},{0,500},{0,0}},
        {{{200,200},{200,300},{300,300},{300,200},{200,200}}}};
    const auto core=viewer::oldForestPolygon(square);
    require(core.exterior==std::vector<glm::dvec2>{{125,125},{375,125},{375,375},{125,375},{125,125}},
            "Old forest must halve distances about the vertex mean without double-counting closure");
    const auto base=viewer::forestTreeCandidates(square,"density",123,5,12,20);
    const auto old=viewer::oldForestTreeCandidates(square,core,"density",123,5,12,20);
    require(old.size()>base.size(), "Old forest did not add trees");
    std::size_t inner=0;
    for (std::size_t i=0;i<old.size();++i) {
        const auto& tree=old[i];
        require(viewer::forestContains(square,tree.eastNorth), "Old forest filled a clearing or left its footprint");
        if (i<base.size()) {
            require(tree.eastNorth==base[i].eastNorth && tree.yaw==base[i].yaw,
                    "Old forest moved the original tree lattice");
            const bool inside=viewer::forestContains(core,tree.eastNorth);
            require(tree.oldForest==inside && std::abs(tree.height-base[i].height*(inside?1.2f:1.f))<1e-5,
                    "Old forest size increase leaked outside the contracted ring");
            if (inside) ++inner;
        } else require(tree.oldForest && viewer::forestContains(core,tree.eastNorth), "Additional tree escaped old forest");
    }
    const double density=static_cast<double>(old.size()-base.size())/inner;
    require(density>.18 && density<.22, "Old forest is not 20% denser");
    // The vertex mean of this C shape is in its open notch. Contraction alone
    // can extend outside the original footprint and must be intersected with it.
    const viewer::ForestPolygon concave{{{0,0},{100,0},{100,20},{20,20},{20,80},{100,80},{100,100},{0,100},{0,0}}, {}};
    const auto contracted=viewer::oldForestPolygon(concave);
    for (const auto& tree:viewer::oldForestTreeCandidates(concave,contracted,"concave",5,3,10,10))
        require(viewer::forestContains(concave,tree.eastNorth), "Contraction planted trees in a concave notch");
    // A nonuniform boundary distinguishes the requested vertex mean from an area centroid.
    const viewer::ForestPolygon uneven{{{0,0},{20,0},{40,0},{40,40},{0,40},{0,0}}, {}};
    require(viewer::oldForestPolygon(uneven).exterior.front()==glm::dvec2(10,8),
            "Old forest center must average border vertices, not use the area centroid");
}
void checkBushes() {
    std::size_t primary=0, clusters=0, dry=0, total=0;
    for (int i=0;i<10000;++i) {
        const auto id="tree-"+std::to_string(i);
        const auto bushes=viewer::forestBushCandidates({40,-30},id,123);
        const auto repeat=viewer::forestBushCandidates({40,-30},id,123);
        require(bushes.size()==repeat.size(), "Bush counts are not deterministic");
        if (bushes.empty()) continue;
        ++primary;
        if (bushes.size()==3) ++clusters;
        require(bushes.size()==1||bushes.size()==3, "Bush clusters recursed or have the wrong count");
        require(bushes.front().eastNorth==glm::dvec2(40,-30), "Primary bush is not at its tree");
        for (std::size_t j=0;j<bushes.size();++j) {
            const auto& bush=bushes[j];
            require(bush.eastNorth==repeat[j].eastNorth && bush.height==repeat[j].height && bush.yaw==repeat[j].yaw && bush.dry==repeat[j].dry,
                    "Bush placement changed between runs");
            require(glm::length(bush.eastNorth-glm::dvec2(40,-30))<=5.00001, "Bush neighbor is outside 5 m radius");
            require(bush.height>=1.5f&&bush.height<=3.5f&&bush.yaw>=0&&bush.yaw<6.283186f, "Bush size or yaw is outside limits");
            if (j) require(bush.yaw!=bushes.front().yaw, "Bush neighbors reused the primary rotation");
            ++total; if(bush.dry) ++dry;
        }
    }
    require(primary>5700&&primary<6300, "Primary bush chance is not 60%");
    const double chance=static_cast<double>(clusters)/primary;
    require(chance>.17&&chance<.23, "Bush neighbor chance is not 20% conditional on a primary");
    require(dry>total*.22&&dry<total*.28,"Dry shrub subset is not approximately 25%");
    const auto bush=viewer::forestBushModel({2,2,std::vector<std::uint8_t>(16,255)});
    require(bush.draws.size()==1&&bush.primitives.size()==1, "Bush must use a single shared mesh");
    const auto& mesh=bush.primitives[0];
    require(mesh.vertices.size()==12&&mesh.indices.size()==18, "Bush must have exactly three cards");
    for (std::size_t i=0;i<mesh.vertices.size();++i) {
        const auto p=mesh.vertices[i].position;
        if (i<4) require(p.z==0, "First bush upright is not in XY");
        else if (i<8) require(p.x==0, "Second bush upright is not perpendicular");
        else require(p.y==.25f, "Horizontal bush card is not at 25% height");
    }
    require(bush.boundsMax-bush.boundsMin==glm::vec3(4,1,4), "Bush height must be one quarter of its width");
    for (std::size_t i=0;i<mesh.vertices.size();i+=4) {
        const auto a=mesh.vertices[i+1].position-mesh.vertices[i].position;
        const auto b=mesh.vertices[i+2].position-mesh.vertices[i+1].position;
        require(glm::length(a)==(i<8?1.f:4.f)&&glm::length(b)==4&&glm::dot(a,b)==0,
                "Bush upright cards must be 4:1 and the horizontal card square");
    }
    require(bush.materials[0].unlit&&bush.materials[0].doubleSided&&bush.materials[0].alphaMode=="MASK",
            "Bush lost unlit double-sided alpha masking");
    // A shifted divider must not leak a strip of the side view onto the top.
    viewer::ImageData atlas{16,2,std::vector<std::uint8_t>(128,0)};
    for (int y=0;y<2;++y) for (int x=1;x<15;++x)
        if (x!=9) atlas.rgba[(y*16+x)*4+3]=255;
    const auto shifted=viewer::forestBushModel(atlas);
    require(shifted.primitives[0].vertices[8].uv.x==10.f/16,
            "Bush atlas divider mixed side pixels into top view");
}
void checkDryTrees() {
    const viewer::ForestPolygon polygon{{{0,0},{500,0},{500,500},{0,500},{0,0}},
        {{{200,200},{200,300},{300,300},{300,200},{200,200}}}};
    const auto core=viewer::oldForestPolygon(polygon);
    const auto usable=[](const viewer::ForestDryTreeCandidate& tree) {return tree.eastNorth.x>160;};
    const auto first=viewer::forestDryTreeCandidates(polygon,core,"dry-test",123,10000,usable);
    const auto repeat=viewer::forestDryTreeCandidates(polygon,core,"dry-test",123,10000,usable);
    require(first.size()==repeat.size(), "Dry tree count is not deterministic");
    std::size_t primaries=0,companions=0;
    for (std::size_t i=0;i<first.size();++i) {
        const auto& tree=first[i]; const auto& other=repeat[i];
        require(tree.eastNorth==other.eastNorth&&tree.height==other.height&&tree.yaw==other.yaw&&
                tree.primaryIndex==other.primaryIndex&&tree.companion==other.companion,
                "Dry trees changed between identical runs");
        require(viewer::forestContains(polygon,tree.eastNorth)&&viewer::forestContains(core,tree.eastNorth)&&usable(tree),
                "Dry tree escaped forest/hole/exclusion constraints");
        require(tree.height>=3.5f&&tree.height<=6&&tree.yaw>=0&&tree.yaw<6.283186f,
                "Dry tree height or rotation outside requested range");
        if (!tree.companion) {
            require(tree.primaryIndex==primaries++, "Dry tree primary indices are unstable");
        } else {
            ++companions;
            require(i>0&&!first[i-1].companion&&first[i-1].primaryIndex==tree.primaryIndex,
                    "Dry companions recursed or lack a primary");
            const auto& parent=first[i-1];
            const auto distance=glm::length(parent.eastNorth-tree.eastNorth);
            require(distance>=1.5&&distance<=5, "Companion is outside the nearby 1.5..5 m annulus");
            require(parent.yaw!=tree.yaw&&parent.height!=tree.height, "Companion reused its parent's size or rotation");
        }
    }
    require(primaries==900, "Dry primary count must be 9% of existing old trees");
    require(companions>60&&companions<120, "Dry companion chance is not 10%");
    for (const auto count:{0u,5u,6u,50u,100u}) {
        const auto trees=viewer::forestDryTreeCandidates(polygon,core,"rounding",123,count,usable);
        std::size_t primary=0;
        for (const auto& tree:trees) primary+=!tree.companion;
        const std::size_t expected=count==6?1:count==50?5:count==100?9:0;
        require(primary==expected, "Dry tree quota was not rounded to nearest integer");
    }
    const auto other=viewer::forestDryTreeCandidates(polygon,core,"other-patch",123,10000,usable);
    require(first.front().eastNorth!=other.front().eastNorth, "Dry trees ignore patch identity");
    bool rejected=false;
    try {
        (void)viewer::forestDryTreeCandidates(polygon,core,"blocked",123,100,
            [](const viewer::ForestDryTreeCandidate&) {return false;});
    } catch (const std::runtime_error&) {rejected=true;}
    require(rejected, "An impossible dry-tree quota must fail within bounded work");
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
        std::ofstream dry(directory/"dry_tree_impostor.glb",std::ios::binary);
        dry.write(reinterpret_cast<const char*>(glb.data()),glb.size());
    }
    ~Fixtures() {std::error_code error; std::filesystem::remove_all(directory,error);}
    Json document() const {
        return {{"schema","godollo.forest_patches"},{"version",1},
            {"source",{{"map_sha256",viewer::fileSha256(map)}}},
            {"coordinate_frame",{{"units","meters"},{"coordinate_order",{"east","north"}},{"crs","EPSG:32634"},{"origin_projected_m",{100000,200000}}}},
            {"runtime",{{"tree_asset","tree.glb"},{"side_texture","tile.png"},{"top_texture","tile.png"},{"bush_texture","tile.png"},{"tree_spacing_m",5}}},
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
    require(layer.patches.size()==2 && trees.assets.size()==3 && !trees.instances.empty(), "Runtime layer is empty");
    std::size_t bushCount=0;
    for (const auto& tree:trees.instances) {
        const auto feet=glm::vec3(tree.transform[3]);
        require(feet.z<0 && std::abs(feet.y-(feet.x*.1f-feet.z*.2f))<.002f, "Tree frame or terrain elevation is wrong");
        if (!tree.collidable) {
            ++bushCount;
            require(tree.blastResponsive, "Generated bush is missing explosion response");
            require(tree.undergrowth!=viewer::UndergrowthKind::none,"Generated shrub is missing contact metadata");
            require(tree.asset==1&&tree.dimensions.x==4*tree.dimensions.y&&tree.dimensions.z==tree.dimensions.x&&
                    tree.dimensions.x>=6.f&&tree.dimensions.x<=14.f, "Bush uses wrong asset, size or quarter-height proportions");
            bool inCore=false;
            for (const auto& patch:layer.patches)
                inCore=inCore||viewer::forestContains(patch.oldForest,{feet.x,-feet.z});
            require(inCore, "Bush escaped old forest");
        } else require(!tree.blastResponsive, "Trunk trees unexpectedly acquired shrub deformation");
    }
    require(bushCount>0, "Loader did not spawn any non-colliding bushes");
    for (const auto& patch:layer.patches)
        for (auto i=patch.firstTree;i<patch.firstTree+patch.treeCount;++i) {
            const auto bounds=viewer::vegetationBlastBounds(trees.instances[i]);
            require(glm::all(glm::greaterThanEqual(bounds.minimum,patch.bounds.minimum))&&
                    glm::all(glm::lessThanEqual(bounds.maximum,patch.bounds.maximum)),
                    "Patch bounds omit a generated shrub's blast motion");
        }
    std::size_t dryCount=0;
    for (const auto& patch:layer.patches) {
        std::size_t healthy=0,primary=0;
        for (std::size_t i=patch.firstTree;i<patch.firstTree+patch.treeCount;++i) {
            const auto& tree=trees.instances[i];
            const glm::dvec2 p(tree.transform[3].x,-tree.transform[3].z);
            if (tree.id.rfind("dry:",0)==0) {
                ++dryCount;
                if (tree.id.find(":companion")==std::string::npos) ++primary;
                require(tree.asset==2&&!trees.assets[tree.asset].matchCanopyColor,
                        "Dry tree uses the wrong asset or receives a green canopy tint");
                require(tree.dimensions.y>=3.5f&&tree.dimensions.y<=6&&viewer::forestContains(patch.oldForest,p),
                        "Dry tree received old-growth enlargement or escaped the old core");
            } else if (tree.collidable&&viewer::forestContains(patch.oldForest,p)) ++healthy;
        }
        require(primary==(healthy*9+50)/100, "Loaded dry-tree count includes bushes or dry trees in its denominator");
    }
    require(dryCount>0, "Loader did not add the dry-tree population");
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
    require(rejected && trees.instances.size()==positions.size() && trees.assets.size()==3, "Stale config accepted or mutated trees");
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
    require(withDetected.assets.size()==3, "Shared vegetation asset uploaded twice");
    require(withDetection.patches.front().detectedTrees==std::vector<std::size_t>{0}, "Detected tree not associated with its forest LoD");
    const auto existing=glm::vec3(withDetected.instances.front().transform[3]);
    for (std::size_t i=1;i<withDetected.instances.size();++i)
        if (withDetected.instances[i].collidable) require(glm::length(glm::vec2(withDetected.instances[i].transform[3].x-existing.x,
            withDetected.instances[i].transform[3].z-existing.z))>=3, "Tree generated too close to detector placement");
}
void checkDetectedOldTrees() {
    const Fixtures files; auto doc=files.document();
    const auto scene=terrainScene(); const viewer::CollisionWorld world(scene);
    viewer::TreeLayer trees;
    // An authoritative reflected/sheared matrix must retain its axes and feet.
    viewer::TreeInstance inner; inner.id="detected-inner"; inner.dimensions={4,10,4};
    inner.transform[0]={-1,0,0,0}; inner.transform[2]={.3f,0,1,0};
    inner.transform[3]={10,3,-10,1}; inner.boundsMin={6,3,-14}; inner.boundsMax={14,13,-6};
    auto outer=inner; outer.id="detected-outer"; outer.transform[3]={2,.6f,-2,1};
    outer.boundsMin={-2,.6f,-6}; outer.boundsMax={6,10.6f,2};
    trees.assets.push_back({"tree",files.directory/"tree.glb",viewer::loadModel(files.directory/"tree.glb")});
    trees.instances={inner,outer};
    doc["patches"][1]["id"]="A"; files.write(doc);
    bool rejected=false;
    try {(void)viewer::loadForestLayer(files.config,files.map,scene,world,trees);} catch (const std::runtime_error&) {rejected=true;}
    require(rejected&&trees.instances.size()==2&&trees.assets.size()==1&&trees.instances[0].transform==inner.transform,
            "Failed load partially enlarged detected trees or published bushes");
    doc=files.document(); files.write(doc);
    const auto forest=viewer::loadForestLayer(files.config,files.map,scene,world,trees);
    const auto& enlarged=trees.instances[0];
    require(enlarged.transform[3]==inner.transform[3]&&enlarged.dimensions==inner.dimensions*1.2f,
            "Detected old tree moved its base or kept its original size");
    for (int axis=0;axis<3;++axis)
        require(enlarged.transform[axis]==inner.transform[axis]*1.2f, "Detected tree lost reflection or shear");
    require(trees.instances[1].transform==outer.transform&&trees.instances[1].boundsMin==outer.boundsMin&&
            trees.instances[1].boundsMax==outer.boundsMax&&trees.instances[1].dimensions==outer.dimensions,
            "Detected tree outside the old forest changed");
    require(forest.patches[0].detectedTrees==std::vector<std::size_t>({0,1}), "Detected trees lost forest LoD membership");
    for (const auto& tree:trees.instances) if (tree.id.rfind("bush:",0)==0)
        require(!tree.collidable, "Generated bush acquired a trunk collider");
}
void checkDryTreeDenominator() {
    const Fixtures files; auto doc=files.document();
    doc["patches"]=Json::array({{{"id","core"},{"polygon",{{0,0},{60,0},{60,60},{0,60},{0,0}}},
                                  {"holes",Json::array()}}});
    files.write(doc);
    const auto scene=terrainScene(); const viewer::CollisionWorld world(scene);
    viewer::TreeLayer trees;
    trees.assets.push_back({"tree",files.directory/"tree.glb",viewer::loadModel(files.directory/"tree.glb")});
    // Co-located synthetic inputs make the denominator distinguish mapped
    // trees, bushes and the newly generated population without filling the core.
    for (int i=0;i<150;++i) {
        viewer::TreeInstance tree;
        tree.id="input-"+std::to_string(i); tree.collidable=i<100;
        tree.transform[3]={30,9,-30,1}; tree.dimensions={4,10,4};
        tree.boundsMin={28,9,-32}; tree.boundsMax={32,19,-28};
        trees.instances.push_back(tree);
    }
    const auto forest=viewer::loadForestLayer(files.config,files.map,scene,world,trees);
    const auto& patch=forest.patches.front();
    std::size_t healthy=100,primary=0;
    for (std::size_t i=patch.firstTree;i<patch.firstTree+patch.treeCount;++i) {
        const auto& tree=trees.instances[i];
        const glm::dvec2 p(tree.transform[3].x,-tree.transform[3].z);
        if (tree.id.rfind("dry:",0)==0) {
            if (tree.id.find(":companion")==std::string::npos) ++primary;
        } else if (tree.collidable&&viewer::forestContains(patch.oldForest,p)) ++healthy;
    }
    require(primary==(healthy*9+50)/100, "Dry quota must include mapped trees but exclude existing and generated bushes");
    require(primary>=9, "Mapped trees did not contribute to the dry-tree quota");
    require(trees.instances[0].dimensions.y==12&&trees.instances[149].dimensions.y==10,
            "Dry integration changed healthy enlargement or enlarged an existing bush");
    // Prove that failed placement also preserves the already-loaded tree layer.
    doc=files.document(); doc["patches"]=Json::array({doc["patches"][0]}); files.write(doc);
    viewer::TreeLayer blocked; blocked.assets.push_back(trees.assets.front());
    for (int x=5;x<=15;x+=2) for (int n=5;n<=15;n+=2) {
        auto tree=trees.instances.front(); tree.id="blocked-"+std::to_string(x)+":"+std::to_string(n);
        tree.transform[3]={x,x*.1f+n*.2f,-n,1}; blocked.instances.push_back(tree);
    }
    const auto before=blocked.instances;
    bool rejected=false;
    try {(void)viewer::loadForestLayer(files.config,files.map,scene,world,blocked);}
    catch (const std::runtime_error& error) {rejected=std::string(error.what()).find("Cannot place requested dry trees")!=std::string::npos;}
    require(rejected&&blocked.assets.size()==1&&blocked.instances.size()==before.size()&&
            blocked.instances.front().transform==before.front().transform,
            "Failed dry quota partially published vegetation or enlarged mapped trees");
}
}
int main() {
    try {checkGeometry(); checkPlacement(); checkOldForest(); checkBushes(); checkDryTrees(); checkLoader(); checkDetectedOldTrees(); checkDryTreeDenominator(); std::cout<<"Forest geometry, old growth, bushes, dry trees, placement and loader tests passed\n";}
    catch (const std::exception& e) {std::cerr<<e.what()<<'\n'; return 1;}
}
