#include "doodad_placement.hpp"
#include "forest_layer.hpp"
#include "collision_world.hpp"
#include "interior_fixture.hpp"
#include <algorithm>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
viewer::ForestPolygon rectangle(double x0,double n0,double x1,double n1) {
    return {{{x0,n0},{x1,n0},{x1,n1},{x0,n1},{x0,n0}}, {}};
}
viewer::ForestLayer forests(std::vector<viewer::ForestPolygon> polygons) {
    viewer::ForestLayer result;
    for (auto& polygon:polygons) {
        viewer::ForestPatch patch; patch.footprint=std::move(polygon);
        result.patches.push_back(std::move(patch));
    }
    return result;
}
viewer::Model terrain() {
    viewer::Model model; model.materials.push_back({});
    interior_fixture::quad(model,{-128,2,-128},{-128,2,128},{128,2,128},{128,2,-128},"terrain");
    interior_fixture::bounds(model);
    return model;
}
void repeatability() {
    const auto model=terrain(); const viewer::CollisionWorld world(model);
    auto layer=forests({rectangle(-80,-80,80,80),rectangle(90,90,100,100)});
    const viewer::DoodadPlacementGenerator generator(layer,world);
    const auto first=generator.generate({-1,-1});
    require(first.size()==64,"Full woodland cell must retain all 64 candidates");
    (void)generator.generate({3,3});
    const auto repeated=generator.generate({-1,-1});
    auto reorderedLayer=layer;
    std::reverse(reorderedLayer.patches.begin(),reorderedLayer.patches.end());
    const viewer::DoodadPlacementGenerator reordered(reorderedLayer,world);
    const auto shuffled=reordered.generate({-1,-1});
    std::set<std::uint64_t> ids;
    for (std::size_t i=0;i<first.size();++i) {
        const auto& a=first[i]; const auto& b=repeated[i]; const auto& c=shuffled[i];
        require(a.id==b.id&&a.id==c.id&&a.position==b.position&&a.position==c.position&&
                a.rotation==b.rotation&&a.scale==b.scale&&a.kind==b.kind,"Placement changed with query/patch order");
        require(viewer::doodadCellAt(a.position)==viewer::DoodadCell{-1,-1},"Candidate left its owning cell");
        require(a.position.y==2,"Ground datum was lost or subtracted twice");
        require(ids.insert(a.id).second,"Duplicate candidate ID");
    }
    for (const auto& p:generator.generate({0,0})) require(ids.insert(p.id).second,"Adjacent cells reused IDs");
    require(viewer::doodadCellAt({-.001f,0,-16.001f})==viewer::DoodadCell{-1,-2},"Negative coordinates must use floor");
}
void partialCellsAndHoles() {
    const auto model=terrain(); const viewer::CollisionWorld world(model);
    auto polygon=rectangle(8,-40,40,8);
    polygon.holes.push_back({{16,-32},{16,-16},{32,-16},{32,-32},{16,-32}});
    const auto layer=forests({polygon,rectangle(-12,-12,-11,-11)});
    const viewer::DoodadPlacementGenerator generator(layer,world);
    require(generator.intersectsForest({0,0}),"Cell partially crossing forest edge was discarded");
    const auto partial=generator.generate({0,0});
    require(!partial.empty()&&partial.size()<64,"Partial cell did not clip its population");
    for (const auto& p:partial)
        require(viewer::forestContains(polygon,{p.position.x,-p.position.z}),"Candidate escaped actual forest footprint");
    require(generator.intersectsForest({-1,0}),"Tiny forest wholly inside a cell was missed");
    require(!generator.intersectsForest({-2,0})&&generator.generate({-2,0}).empty(),"Nonforest cell generated doodads");
    const auto stripLayer=forests({rectangle(-8,6,24,7)});
    const viewer::DoodadPlacementGenerator strip(stripLayer,world);
    require(strip.intersectsForest({0,-1}),"Forest strip crossing a cell without containing its corners was missed");
    // A clearing larger than one cell must not acquire decoration at its center.
    auto clearing=rectangle(-64,-64,64,64);
    clearing.holes.push_back({{-20,-20},{-20,20},{20,20},{20,-20},{-20,-20}});
    const auto cleared=forests({clearing});
    const viewer::DoodadPlacementGenerator holes(cleared,world);
    require(!holes.intersectsForest({0,0})&&holes.generate({0,0}).empty(),"Clearing interior classified as woodland");
    auto concave=rectangle(-64,-64,64,64);
    concave.holes.push_back({{-4,-4},{-4,20},{7,20},{7,8},{9,8},{9,20},{20,20},{20,-4},{-4,-4}});
    const auto notchedLayer=forests({concave});
    const viewer::DoodadPlacementGenerator notch(notchedLayer,world);
    require(notch.intersectsForest({0,-1}),"Woodland entering a cell through a concave clearing was missed");
    for (const auto& p:generator.generate({1,1}))
        require(viewer::forestContains(polygon,{p.position.x,-p.position.z}),"Clearing hole received a candidate");
}
void supportAndBuildings() {
    const auto layer=forests({rectangle(-64,-64,64,64)});
    const viewer::Model empty;
    const viewer::CollisionWorld missing(empty);
    const viewer::DoodadPlacementGenerator unsupported(layer,missing);
    require(unsupported.generate({0,0}).empty(),"Missing terrain support accepted");
    const auto town=interior_fixture::town();
    const viewer::CollisionWorld world(town);
    const viewer::DoodadPlacementGenerator generator(layer,world);
    bool outside=false;
    for (const auto& p:generator.generate({0,0})) {
        require(!world.insideBuilding(p.position,.4f,.3f),"Doodad generated inside building");
        outside=true;
    }
    require(outside,"Building exclusion removed all outdoor candidates");
}
void followsSlopedTerrain() {
    viewer::Model model;model.materials.push_back({});
    const auto height=[](float x,float z) {return 2+.1f*x-.2f*z;};
    const auto point=[&](float x,float z) {return glm::vec3(x,height(x,z),z);};
    interior_fixture::quad(model,point(-64,-64),point(-64,64),point(64,64),point(64,-64),"terrain");
    interior_fixture::bounds(model);
    const viewer::CollisionWorld world(model);
    const auto layer=forests({rectangle(-48,-48,48,48)});
    const viewer::DoodadPlacementGenerator generator(layer,world);
    const auto normal=glm::normalize(glm::vec3(-.1f,1,.2f));
    std::size_t aligned=0;
    for (const auto& p:generator.generate({0,0})) {
        require(std::abs(p.position.y-height(p.position.x,p.position.z))<1e-4f,"Sloped terrain origin sampled wrong height");
        if (p.kind==viewer::DoodadKind::BeerBottle) continue;
        ++aligned;
        require(glm::distance(p.rotation*glm::vec3(0,1,0),normal)<1e-5f,"Branch or stone underside not aligned to terrain normal");
        for (const auto local:std::vector<glm::vec3>{{-.3f,0,-.4f},{.3f,0,.4f}}) {
            const auto placed=p.position+p.rotation*(local*p.scale);
            require(std::abs(placed.y-height(placed.x,placed.z))<1e-4f,"Broad flat underside floats above or intersects its terrain plane");
        }
    }
    require(aligned>0,"Sloped terrain fixture did not exercise a branch or stone");
}
}
int main() {
    try {repeatability();partialCellsAndHoles();supportAndBuildings();followsSlopedTerrain();}
    catch (const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
    std::cout<<"Doodad placement checks passed\n";
}
