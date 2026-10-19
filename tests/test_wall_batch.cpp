#include "building_batch.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <stdexcept>
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
viewer::Model fixture(){
    viewer::Model model;model.materials.resize(2);viewer::Primitive mesh;mesh.material=0;
    for(const auto p:{glm::vec3(1,0,-20),glm::vec3(3,0,-20),glm::vec3(1,4,-20)}){
        viewer::Vertex v;v.position=p;v.normal={0,0,1};v.uv={p.x,p.y};mesh.vertices.push_back(v);
    }
    mesh.indices={0,1,2};model.primitives.push_back(mesh);
    model.draws.push_back({0,glm::mat4(1),"building_a_walls",30,true,"a"});
    model.draws.push_back({0,glm::translate(glm::mat4(1),{8,0,0}),"building_b_walls",60,true,"b"});return model;
}
void grouping(){
    auto model=fixture();auto mesh=model.primitives[0];mesh.material=1;model.primitives.push_back(mesh);
    model.draws.push_back({1,glm::mat4(1),"building_c_walls"});
    model.draws.push_back({0,glm::translate(glm::mat4(1),{128,0,0}),"building_d_walls"});
    model.draws.push_back({0,glm::translate(glm::mat4(1),{-8,0,0}),"building_e_walls"});
    const auto excluded=model.draws.size();
    model.draws.push_back({0,glm::mat4(1),"building_a_detailed_walls",30,false,"a"});
    model.draws.push_back({0,glm::mat4(1),"building_a_doors",30,false,"a"});
    model.draws.push_back({0,glm::mat4(1),"building_a_roof"});
    model.draws.push_back({0,glm::mat4(1),"building_detail_walls",10});
    model.draws.push_back({0,glm::mat4(1),"building_moving_walls"});model.draws.back().horizontalMotionRadius=2;
    model.draws.push_back({0,glm::mat4(1),"building_fence_walls"});model.draws.back().fence=true;
    mesh.material=2;model.primitives.push_back(mesh);model.materials.emplace_back();model.materials.back().alphaMode="BLEND";
    model.draws.push_back({2,glm::mat4(1),"building_blended_walls"});
    model.draws.push_back({0,glm::scale(glm::mat4(1),{0,1,1}),"building_singular_walls"});
    const auto batches=viewer::buildWallBatches(model,viewer::ModelVisibility(model));
    require(batches.meshes.size()==4,"Wall material or positive/negative cells merged incorrectly");
    require(batches.batchForDraw[0]==batches.batchForDraw[1],"Compatible walls did not merge");
    require(batches.meshes[0].indices==std::vector<std::uint32_t>({0,1,2,3,4,5}),"Wall concatenation produced incorrect indices");
    for(std::size_t i=excluded;i<model.draws.size();++i)require(batches.batchForDraw[i]==viewer::noBuildingBatch,"Detailed, movable or transparent surface was batched");
    require(model.primitives[0].vertices.size()==3&&model.draws[1].transform[3].x==8,"Batching changed authoritative source geometry");
}
void detailBounds(){
    auto model=fixture();
    // An alternative extends much closer than the simple shell. Its moving
    // panel padding must participate even when that alternative is offscreen.
    auto alternative=model.primitives[0];for(auto& v:alternative.vertices)v.position.z=10;
    model.primitives.push_back(alternative);
    model.draws.push_back({1,glm::mat4(1),"building_a_doors",30,false,"a"});model.draws.back().horizontalMotionRadius=5;
    const auto batches=viewer::buildWallBatches(model,viewer::ModelVisibility(model));
    const auto& detail=batches.details[0];
    require(detail.bounds.maximum.z==15&&detail.distance==60,"Batch ignored alternative group bounds, movement padding or largest detail distance");
    const auto projection=glm::perspective(glm::radians(60.f),1.f,.1f,1000.f);
    require(viewer::buildingDetailsNear(detail.bounds,detail.distance,{2,2,60},projection),"Cell containing a near alternative was allowed to batch");
    require(!viewer::buildingDetailsNear(detail.bounds,detail.distance,{2,2,300},projection),"Wholly distant cell failed to batch");
    const auto scoped=glm::perspective(glm::radians(10.f),1.f,.1f,1000.f);
    require(viewer::buildingDetailsNear(detail.bounds,detail.distance,{2,2,300},scoped),"Scope failed to preserve detailed openings");
    require(!viewer::buildingDetailsNear(detail.bounds,0,{2,2,0},scoped),"Legacy walls without alternatives should batch at any range");
}
void transforms(){
    auto model=fixture();model.draws.resize(1);auto& m=model.draws[0].transform;
    m=glm::scale(glm::translate(glm::mat4(1),{20,5,10}),{-2,3,4});m[1].x=.5f;
    const auto result=viewer::buildWallBatches(model,viewer::ModelVisibility(model));const auto& mesh=result.meshes[0];
    require(glm::length(mesh.vertices[0].position-glm::vec3(18,5,-70))<1e-5f,"Wall transform was not applied exactly once");
    const auto a=mesh.vertices[mesh.indices[0]].position,b=mesh.vertices[mesh.indices[1]].position,c=mesh.vertices[mesh.indices[2]].position;
    require(glm::dot(glm::cross(b-a,c-a),mesh.vertices[0].normal)>0,"Reflected wall winding changed");
    require(mesh.vertices[0].uv==model.primitives[0].vertices[0].uv,"Wall texture coordinates changed");
}
}
int main(){try{grouping();detailBounds();transforms();std::cout<<"Wall batches preserve geometry and conservative LoD eligibility\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
