#include "forest_layer.hpp"
#include "forest_placement.hpp"
#include "forest_shrub.hpp"
#include "vegetation_blast.hpp"
#include "collision_world.hpp"
#include "file_fingerprint.hpp"

#include <json.hpp>
#include <stb_image.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <unordered_set>

namespace viewer {
namespace {
using Json=nlohmann::json;
double number(const Json& value) {
    if (!value.is_number() || !std::isfinite(value.get<double>()))
        throw std::runtime_error("Forest configuration requires finite numbers");
    return value.get<double>();
}
glm::dvec2 point(const Json& value) {
    if (!value.is_array()||value.size()!=2) throw std::runtime_error("Forest coordinates must be [east,north]");
    return {number(value[0]),number(value[1])};
}
std::vector<glm::dvec2> ring(const Json& value) {
    if (!value.is_array() || value.size()>1024) throw std::runtime_error("Invalid forest ring array");
    std::vector<glm::dvec2> result;
    for (const auto& p:value) result.push_back(point(p));
    return result;
}
float setting(const Json& value,const char* key,float fallback,float low,float high) {
    const auto v=value.contains(key)?number(value.at(key)):fallback;
    if (v<low||v>high) throw std::runtime_error(std::string("Invalid forest runtime setting: ")+key);
    return static_cast<float>(v);
}
std::filesystem::path assetPath(const Json& settings,const char* key,const char* fallback,
                                const std::filesystem::path& parent) {
    auto path=std::filesystem::path(settings.value(key,std::string(fallback)));
    if (path.is_relative()) path=parent/path;
    return std::filesystem::weakly_canonical(path);
}
ImageData image(const std::filesystem::path& path) {
    ImageData result; int channels=0;
    if (!stbi_info(path.u8string().c_str(),&result.width,&result.height,&channels) ||
        result.width<1||result.height<1||result.width>4096||result.height>4096)
        throw std::runtime_error("Invalid forest texture: "+path.string());
    std::unique_ptr<unsigned char,decltype(&stbi_image_free)> pixels(
        stbi_load(path.u8string().c_str(),&result.width,&result.height,&channels,4),stbi_image_free);
    if (!pixels) throw std::runtime_error("Cannot decode forest texture: "+path.string());
    result.rgba.assign(pixels.get(),pixels.get()+static_cast<std::size_t>(result.width)*result.height*4);
    return result;
}
float assetHeight(const std::filesystem::path& path) {
    // Calibrated tree GLBs record visible height, distinct from padded bounds.
    std::ifstream stream(path,std::ios::binary);
    std::array<unsigned char,20> header{}; stream.read(reinterpret_cast<char*>(header.data()),20);
    if (!stream || header[0]!='g'||header[1]!='l'||header[2]!='T'||header[3]!='F')
        throw std::runtime_error("Forest tree asset must be a calibrated impostor GLB");
    const std::uint32_t size=header[12]|(static_cast<std::uint32_t>(header[13])<<8)|
        (static_cast<std::uint32_t>(header[14])<<16)|(static_cast<std::uint32_t>(header[15])<<24);
    if (size>8*1024*1024) throw std::runtime_error("Tree asset metadata exceeds load limit");
    std::string encoded(size,'\0'); stream.read(encoded.data(),size);
    if (!stream) throw std::runtime_error("Truncated tree asset metadata");
    const auto extras=Json::parse(encoded).at("extras");
    if (extras.at("units")!="meters"||extras.at("upAxis")!="+Y"||extras.at("origin")!="visible trunk base")
        throw std::runtime_error("Forest asset has incompatible tree calibration");
    const auto height=number(extras.at("visibleHeightMeters"));
    if (height<=0||height>100) throw std::runtime_error("Invalid calibrated tree height");
    return static_cast<float>(height);
}
void validateTreeMaterials(const Model& model) {
    if (model.draws.empty()) throw std::runtime_error("Forest tree asset has no geometry");
    for (const auto& draw:model.draws) {
        const auto material=model.primitives.at(draw.primitive).material;
        if (material<0||static_cast<std::size_t>(material)>=model.materials.size())
            throw std::runtime_error("Forest tree asset needs explicit masked materials");
        const auto& m=model.materials[material];
        if (!m.unlit||!m.doubleSided||m.alphaMode!="MASK")
            throw std::runtime_error("Forest trees must be unlit double-sided alpha masks");
    }
}
void include(VisibilityBounds& bounds,glm::vec3 p) {
    bounds.minimum=glm::min(bounds.minimum,p); bounds.maximum=glm::max(bounds.maximum,p);
}
TreeInstance placedInstance(std::string id,std::size_t asset,const Model& model,
                            glm::vec3 feet,float scale,float yaw,float visibleHeight) {
    TreeInstance instance; instance.id=std::move(id); instance.asset=asset;
    instance.transform=glm::scale(glm::rotate(glm::translate(glm::mat4(1),feet),yaw,{0,1,0}),glm::vec3(scale));
    const auto linear=glm::mat3(instance.transform);
    const auto center=glm::vec3(instance.transform*glm::vec4((model.boundsMin+model.boundsMax)*.5f,1));
    glm::vec3 radius(0); const auto extent=(model.boundsMax-model.boundsMin)*.5f;
    for (int axis=0;axis<3;++axis) radius+=glm::abs(linear[axis])*extent[axis];
    instance.boundsMin=center-radius-glm::vec3(.01f); instance.boundsMax=center+radius+glm::vec3(.01f);
    instance.dimensions=(model.boundsMax-model.boundsMin)*scale;
    instance.dimensions.y=visibleHeight;
    return instance;
}
TreeInstance enlargedTree(TreeInstance tree) {
    const glm::vec3 feet(tree.transform[3]);
    tree.transform=glm::scale(tree.transform,glm::vec3(1.2f));
    tree.dimensions*=1.2f;
    tree.boundsMin=feet+(tree.boundsMin-feet)*1.2f;
    tree.boundsMax=feet+(tree.boundsMax-feet)*1.2f;
    return tree;
}
}

bool forestUsesTrees(const ForestPatch& patch,glm::vec3 eye,bool scoped,float distance) {
    const auto nearest=glm::clamp(eye,patch.bounds.minimum,patch.bounds.maximum);
    return scoped || glm::length(eye-nearest)<=distance;
}

ForestLayer loadForestLayer(const std::filesystem::path& path,const std::filesystem::path& map,
    const Model& scene,const CollisionWorld& placementWorld,TreeLayer& trees) {
    try {
        std::ifstream stream(path);
        if (!stream) throw std::runtime_error("Cannot open forest config");
        const auto doc=Json::parse(stream);
        if (doc.at("schema")!="godollo.forest_patches"||!doc.at("version").is_number_integer()||doc.at("version")!=1)
            throw std::runtime_error("Unsupported forest config schema/version");
        if (doc.at("source").at("map_sha256")!=fileSha256(map))
            throw std::runtime_error("Forest map SHA-256 mismatch; revalidate polygons for this map");
        const auto& frame=doc.at("coordinate_frame");
        if (!scene.geographicFrame||frame.at("units")!="meters"||
            frame.at("coordinate_order")!=Json::array({"east","north"})||
            frame.at("crs")!=scene.geographicFrame->crs||
            glm::length(point(frame.at("origin_projected_m"))-scene.geographicFrame->origin)>1e-5)
            throw std::runtime_error("Forest coordinate frame disagrees with the published scene");
        const auto settings=doc.value("runtime",Json::object());
        if (!settings.is_object()) throw std::runtime_error("Forest runtime settings must be an object");
        const float spacing=setting(settings,"tree_spacing_m",9,2,100);
        const float minHeight=setting(settings,"min_tree_height_m",12,1,100);
        const float maxHeight=setting(settings,"max_tree_height_m",20,minHeight,100);
        const float volumeHeight=setting(settings,"volume_height_m",16,1,100);
        const float tile=setting(settings,"texture_tile_m",32,1,200);
        const auto seedJson=settings.value("seed",Json(20261011u));
        if (!seedJson.is_number_integer()||number(seedJson)<0||number(seedJson)>4294967295.0)
            throw std::runtime_error("Forest seed must be a uint32 integer");
        const auto seed=seedJson.get<std::uint32_t>();
        ForestLayer layer;
        layer.detailDistance=setting(settings,"detail_distance_m",350,1,10000);
        const auto parent=std::filesystem::absolute(path).parent_path();
        const auto treePath=assetPath(settings,"tree_asset","assets/tree_impostor.glb",parent);
        std::size_t asset=trees.assets.size();
        for (std::size_t i=0;i<trees.assets.size();++i)
            if (std::filesystem::weakly_canonical(trees.assets[i].path)==treePath) {asset=i;break;}
        std::optional<TreeAsset> newAsset;
        if (asset==trees.assets.size()) newAsset=TreeAsset{"forest_tree",treePath,loadModel(treePath)};
        const Model& treeModel=newAsset?newAsset->model:trees.assets[asset].model;
        const float nativeHeight=assetHeight(treePath);
        validateTreeMaterials(treeModel);
        const auto sidePath=assetPath(settings,"side_texture","assets/forests/side.png",parent);
        // Default alongside the side texture so existing nested OSM configs
        // resolve the same shared atlas without rewriting published inputs.
        const auto defaultShrubPath=(sidePath.parent_path()/"shrub.png").u8string();
        const auto shrubKey=settings.contains("shrub_texture")?"shrub_texture":"bush_texture";
        auto shrubPath=assetPath(settings,shrubKey,defaultShrubPath.c_str(),parent);
        // Keep previously published configs/packs usable across the asset rename.
        if (!std::filesystem::exists(shrubPath)) {
            const auto alternate=shrubPath.filename()=="shrub.png"?"bush.png":
                                 shrubPath.filename()=="bush.png"?"shrub.png":nullptr;
            if (alternate && std::filesystem::exists(shrubPath.parent_path()/alternate))
                shrubPath=shrubPath.parent_path()/alternate;
        }
        std::size_t shrubAsset=trees.assets.size()+(newAsset?1:0);
        std::optional<TreeAsset> newShrubAsset;
        for (std::size_t i=0;i<trees.assets.size();++i)
            if (std::filesystem::weakly_canonical(trees.assets[i].path)==shrubPath) {shrubAsset=i;break;}
        if (shrubAsset>=trees.assets.size())
            newShrubAsset=TreeAsset{"forest_shrub",shrubPath,forestShrubModel(image(shrubPath))};
        const Model& shrubModel=newShrubAsset?newShrubAsset->model:trees.assets[shrubAsset].model;
        const auto defaultDryPath=(treePath.parent_path()/"dry_tree_impostor.glb").u8string();
        const auto dryPath=assetPath(settings,"dry_tree_asset",defaultDryPath.c_str(),parent);
        std::size_t dryAsset=trees.assets.size()+(newAsset?1:0)+(newShrubAsset?1:0);
        std::optional<TreeAsset> newDryAsset;
        for (std::size_t i=0;i<trees.assets.size();++i)
            if (std::filesystem::weakly_canonical(trees.assets[i].path)==dryPath) {dryAsset=i;break;}
        if (dryAsset>=trees.assets.size())
            newDryAsset=TreeAsset{"forest_dry_tree",dryPath,loadModel(dryPath),false};
        const Model& dryModel=newDryAsset?newDryAsset->model:trees.assets[dryAsset].model;
        const float dryNativeHeight=assetHeight(dryPath);
        validateTreeMaterials(dryModel);
        for (const auto& texturePath:{sidePath,
                                    assetPath(settings,"top_texture","assets/forests/top.png",parent)}) {
            layer.volumes.images.push_back(image(texturePath));
            Texture texture; texture.image=static_cast<int>(layer.volumes.textures.size());
            layer.volumes.textures.push_back(texture); // GL_REPEAT and mipmaps.
            Material material; material.baseColorTexture=texture.image; material.unlit=true;
            layer.volumes.materials.push_back(material);
        }
        // Sample exactly the rendered terrain triangles. Roads, buildings and
        // vegetation are excluded from this independent ground-only BVH.
        Model terrain;
        terrain.boundsMin=scene.boundsMin; terrain.boundsMax=scene.boundsMax;
        glm::vec3 terrainLo(std::numeric_limits<float>::max()),terrainHi(std::numeric_limits<float>::lowest());
        for (const auto& draw:scene.draws) if (draw.name.rfind("terrain",0)==0) {
            auto copy=draw; copy.primitive=terrain.primitives.size();
            terrain.primitives.push_back(scene.primitives.at(draw.primitive)); terrain.draws.push_back(copy);
            for (const auto& v:terrain.primitives.back().vertices) {
                const auto p=glm::vec3(draw.transform*glm::vec4(v.position,1));
                terrainLo=glm::min(terrainLo,p); terrainHi=glm::max(terrainHi,p);
            }
        }
        if (terrain.draws.empty()) throw std::runtime_error("Forest placement requires published terrain");
        const CollisionWorld groundWorld(terrain);
        const ForestGround ground=[&](glm::dvec2 p)->std::optional<float> {
            // The config rounds edge positions to millimetres.
            if (p.x<terrainLo.x-.002||p.x>terrainHi.x+.002||-p.y<terrainLo.z-.002||-p.y>terrainHi.z+.002)
                return std::nullopt;
            const auto hit=groundWorld.groundAt(glm::clamp(static_cast<float>(p.x),terrainLo.x,terrainHi.x),
                glm::clamp(static_cast<float>(-p.y),terrainLo.z,terrainHi.z),terrainLo.y-1,terrainHi.y+1,true);
            return hit?std::optional<float>(hit->height):std::nullopt;
        };
        // Avoid adding a second tree next to existing detector placements.
        std::map<std::pair<int,int>,std::vector<glm::vec2>> detected;
        for (const auto& t:trees.instances) {
            if (!t.collidable) continue;
            const glm::vec2 p(t.transform[3].x,t.transform[3].z);
            detected[{static_cast<int>(std::floor(p.x/3)),static_cast<int>(std::floor(p.y/3))}].push_back(p);
        }
        const auto nearDetected=[&](glm::vec3 feet) {
            const glm::vec2 p(feet.x,feet.z);
            const int x=static_cast<int>(std::floor(p.x/3)),z=static_cast<int>(std::floor(p.y/3));
            for (int i=x-1;i<=x+1;++i) for (int j=z-1;j<=z+1;++j) {
                const auto entry=detected.find({i,j});
                if (entry!=detected.end()) for (const auto q:entry->second) if (glm::length(p-q)<3) return true;
            }
            return false;
        };
        std::vector<TreeInstance> generated;
        std::size_t generatedTreeCount=0;
        std::map<std::size_t,TreeInstance> enlargedDetected;
        std::unordered_set<std::string> identifiers;
        const auto& patches=doc.at("patches");
        if (!patches.is_array()||patches.size()>10000) throw std::runtime_error("Invalid forest patch array");
        for (const auto& record:patches) {
            ForestPatch patch; patch.id=record.at("id").get<std::string>();
            if (patch.id.empty()||!identifiers.insert(patch.id).second) throw std::runtime_error("Duplicate/empty forest patch ID");
            ForestPolygon polygon; polygon.exterior=ring(record.at("polygon"));
            if (!record.at("holes").is_array()) throw std::runtime_error("Forest holes must be an array");
            for (const auto& hole:record.at("holes")) polygon.holes.push_back(ring(hole));
            validateForestPolygon(polygon);
            patch.oldForest=oldForestPolygon(polygon);
            auto lo=polygon.exterior.front(),hi=lo;
            for (const auto p:polygon.exterior) {lo=glm::min(lo,p);hi=glm::max(hi,p);}
            if (hi.x-lo.x>500.001||hi.y-lo.y>500.001) throw std::runtime_error("Forest patch exceeds 500 x 500 m");
            patch.bounds={glm::vec3(std::numeric_limits<float>::max()),glm::vec3(std::numeric_limits<float>::lowest())};
            patch.firstTree=trees.instances.size()+generated.size();
            const auto append=[&](TreeInstance instance) {
                const auto animated=vegetationBlastBounds(instance);
                include(patch.bounds,animated.minimum); include(patch.bounds,animated.maximum);
                generated.push_back(std::move(instance));
                if (generated.size()>4000000) throw std::runtime_error("Forest vegetation count exceeds load-time limit");
            };
            const auto shrubs=[&](glm::dvec2 tree,const std::string& treeId) {
                std::size_t index=0;
                for (const auto& shrub:forestShrubCandidates(tree,treeId,seed)) {
                    const auto id="shrub:"+patch.id+":"+treeId+":"+std::to_string(index++);
                    if (!forestContains(polygon,shrub.eastNorth)||!forestContains(patch.oldForest,shrub.eastNorth)) continue;
                    const auto y=ground(shrub.eastNorth);
                    if (!y) continue;
                    const glm::vec3 feet(shrub.eastNorth.x,*y,-shrub.eastNorth.y);
                    if (placementWorld.insideBuilding(feet+glm::vec3(0,.05f,0),.4f,shrub.height)) continue;
                    auto instance=placedInstance(id,shrubAsset,shrubModel,feet,shrub.height,shrub.yaw,shrub.height);
                    instance.collidable=false;
                    instance.blastResponsive=true;
                    instance.undergrowth=shrub.dry?UndergrowthKind::dry:UndergrowthKind::leafy;
                    append(std::move(instance));
                }
            };
            std::size_t oldTreeCount=0;
            for (std::size_t i = 0; i < trees.instances.size(); ++i) {
                const auto& tree = trees.instances[i];
                const glm::dvec2 p(tree.transform[3].x, -tree.transform[3].z);
                if (p.x < lo.x || p.x > hi.x || p.y < lo.y || p.y > hi.y || !forestContains(polygon, p)) continue;
                patch.detectedTrees.push_back(i);
                const bool old=tree.collidable&&forestContains(patch.oldForest,p);
                const auto& adjusted=old?enlargedDetected.emplace(i,enlargedTree(tree)).first->second:tree;
                include(patch.bounds, adjusted.boundsMin);
                include(patch.bounds, adjusted.boundsMax);
                if (old) {++oldTreeCount;shrubs(p,tree.id);}
            }
            auto volume=forestVolume(polygon,ground,volumeHeight,tile);
            patch.firstDraw=layer.volumes.draws.size(); patch.drawCount=2;
            for (auto* mesh:{&volume.first,&volume.second}) {
                for (const auto& v:mesh->vertices) include(patch.bounds,v.position);
                const auto index=layer.volumes.primitives.size();
                layer.volumes.primitives.push_back(std::move(*mesh));
                layer.volumes.draws.push_back({index,glm::mat4(1),"forest_"+patch.id});
            }
            std::size_t candidateIndex=0;
            for (const auto& candidate:oldForestTreeCandidates(polygon,patch.oldForest,patch.id,seed,spacing,minHeight,maxHeight)) {
                ++candidateIndex;
                const auto y=ground(candidate.eastNorth);
                if (!y) throw std::runtime_error("Forest tree candidate has no terrain support");
                const glm::vec3 feet(candidate.eastNorth.x,*y,-candidate.eastNorth.y);
                if (placementWorld.insideBuilding(feet+glm::vec3(0,.05f,0),.4f,candidate.height)) continue;
                if (nearDetected(feet)) continue;
                if (++generatedTreeCount>2000000) throw std::runtime_error("Forest tree count exceeds load-time limit");
                const auto id="forest:"+patch.id+":"+std::to_string(candidateIndex);
                append(placedInstance(id,asset,treeModel,feet,candidate.height/nativeHeight,candidate.yaw,candidate.height));
                if (candidate.oldForest) {++oldTreeCount;shrubs(candidate.eastNorth,id);}
            }
            const auto dryCandidates=forestDryTreeCandidates(polygon,patch.oldForest,patch.id,seed,oldTreeCount,
                [&](const ForestDryTreeCandidate& tree) {
                    const auto y=ground(tree.eastNorth);
                    if (!y) return false;
                    const glm::vec3 feet(tree.eastNorth.x,*y,-tree.eastNorth.y);
                    return !nearDetected(feet)&&
                        !placementWorld.insideBuilding(feet+glm::vec3(0,.05f,0),.4f,tree.height);
                });
            for (const auto& tree:dryCandidates) {
                if (++generatedTreeCount>2000000) throw std::runtime_error("Forest tree count exceeds load-time limit");
                const auto id="dry:"+patch.id+":"+std::to_string(tree.primaryIndex)+(tree.companion?":companion":"");
                const glm::vec3 feet(tree.eastNorth.x,*ground(tree.eastNorth),-tree.eastNorth.y);
                append(placedInstance(id,dryAsset,dryModel,feet,tree.height/dryNativeHeight,tree.yaw,tree.height));
            }
            patch.treeCount=trees.instances.size()+generated.size()-patch.firstTree;
            layer.patches.push_back(std::move(patch));
        }
        // Publish only after the complete config, geometry and placements pass.
        trees.instances.reserve(trees.instances.size()+generated.size());
        trees.assets.reserve(trees.assets.size()+(newAsset?1:0)+(newShrubAsset?1:0)+(newDryAsset?1:0));
        if (newAsset) trees.assets.push_back(std::move(*newAsset));
        if (newShrubAsset) trees.assets.push_back(std::move(*newShrubAsset));
        if (newDryAsset) trees.assets.push_back(std::move(*newDryAsset));
        trees.instances.insert(trees.instances.end(),std::make_move_iterator(generated.begin()),std::make_move_iterator(generated.end()));
        for (auto& entry:enlargedDetected) trees.instances[entry.first]=std::move(entry.second);
        return layer;
    } catch (const std::exception& error) {
        throw std::runtime_error("Failed to load runtime forests '"+path.string()+"': "+error.what());
    }
}
} // namespace viewer
