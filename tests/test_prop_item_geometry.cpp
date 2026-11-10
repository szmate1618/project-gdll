#include "prop_item_geometry.hpp"
#include "physics_grab.hpp"
#include "zombie_cavity_physics.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
template<class Action> void rejects(Action action,const char* message) {
    try { action(); } catch (const std::exception&) { return; }
    throw std::runtime_error(message);
}
void bodyLocalCaptureAndParkedMass() {
    auto physics=std::make_shared<viewer::PhysicsWorld>();
    viewer::BoxLayer boxes(physics),foreign(physics);
    viewer::BoxSpawnMotion motion; motion.mass=12; motion.rotation=glm::angleAxis(.7f,glm::vec3(0,1,0));
    const std::vector<viewer::PropCollider> parts{
        {viewer::PropColliderShape::Box,{-.3f,0,0},{.2f,.3f,.1f},viewer::propMaterialId("wood"),glm::angleAxis(.4f,glm::vec3(1,0,0))},
        {viewer::PropColliderShape::Box,{.3f,.1f,0},{.1f,.2f,.1f},viewer::propMaterialId("cardboard")}};
    const auto index=boxes.addCompound({1,2,3},parts,motion);
    const auto prop=boxes.prop(index);
    const auto capture=viewer::captureBoxPropItem(boxes,prop,"Crate");
    require(capture && capture->name=="Crate" && capture->model.massKg==12 && capture->model.parts.size()==2 &&
            capture->pose.center==glm::vec3(1,2,3),"Capturing a prop must retain body pose, mass and separate collision parts");
    require(capture->model.parts[0].center==parts[0].center && capture->model.parts[0].size==parts[0].halfExtents*2.f &&
            std::abs(glm::dot(capture->model.parts[0].rotation,parts[0].rotation))>.99999f &&
            capture->model.parts[0].material=="wood" && capture->model.parts[1].material=="cardboard",
            "Captured geometry must stay body-local and preserve rotated material-classified parts");
    require(prop.valid() && boxes.active(index) && !viewer::captureBoxPropItem(foreign,prop),
            "Capturing a borrowed prop must not change ownership or match a different owner");
    {
        viewer::PhysicsGrab grip(prop,{},180); grip.setCraftingMode(true);
        require(viewer::captureBoxPropItem(boxes,prop)->model.massKg==12,
                "Temporarily kinematic Crafting props must retain their original item mass");
        grip.park();
    }
    require(viewer::captureBoxPropItem(boxes,prop)->model.massKg==12,"Parked props must retain their item mass after the hand releases them");
    boxes.retire(index);
    require(!prop.valid() && !viewer::captureBoxPropItem(boxes,prop),"Retired props must no longer be capturable");
}
void exactVisuals() {
    viewer::Model model; model.materials.emplace_back(); model.materials[0].baseColor={.5f,.25f,.75f,1};
    auto& primitive=model.primitives.emplace_back(); primitive.material=0;
    for (const glm::vec3 point:{glm::vec3(0,0,0),{1,0,0},{0,1,0}}) {
        viewer::Vertex vertex; vertex.position=point; vertex.normal={0,0,1}; vertex.color={.4f,.8f,.2f,1};
        primitive.vertices.push_back(vertex);
    }
    primitive.indices={0,1,2};
    viewer::DrawInstance draw; draw.transform=glm::translate(glm::mat4(1),{.2f,.1f,0});
    model.draws.push_back(draw); model.draws.push_back(draw);
    model.draws.back().transform=glm::rotate(glm::mat4(1),.6f,glm::vec3(1,0,0));
    const auto frame=glm::scale(glm::mat4(1),{-2,.5f,1});
    const auto mesh=viewer::propItemVisual(model,frame);
    require(mesh.size()==6 && glm::distance(mesh[0].position,glm::vec3(frame*draw.transform*glm::vec4(0,0,0,1)))<1e-6f,
            "Capturing visuals must flatten every authored draw transform into the item frame");
    require(glm::distance(mesh[0].color,glm::vec3(.2f,.2f,.15f))<1e-6f,
            "Visual capture must retain material and per-vertex colors");
    for (std::size_t triangle=0;triangle<mesh.size();triangle+=3) {
        const auto normal=glm::normalize(glm::cross(mesh[triangle+1].position-mesh[triangle].position,
            mesh[triangle+2].position-mesh[triangle].position));
        require(glm::dot(normal,mesh[triangle].normal)>.99999f && std::abs(glm::length(mesh[triangle].normal)-1)<1e-6f,
                "Reflections and nonuniform transforms must preserve outward winding and unit normals");
    }
    model.materials[0].baseColorTexture=0;
    rejects([&] { viewer::propItemVisual(model,frame); },"Textured models must not silently lose their textures");
    model.materials[0].baseColorTexture=-1;
    rejects([&] { viewer::propItemVisual(model,glm::scale(glm::mat4(1),{0,1,1})); },"Singular visual transforms must be rejected");
    model.draws.resize(1); model.primitives[0].indices.resize(4098,0);
    rejects([&] { viewer::propItemVisual(model,frame); },"Captured visuals must obey the bounded custom mesh transport limit");
}
void cavityOwnerCleanup() {
    auto physics=std::make_shared<viewer::PhysicsWorld>(); viewer::ZombieCavityPhysics cavities(physics);
    viewer::AnimatedModel asset; viewer::ZombieWoundGeometry geometry;
    geometry.region=viewer::ZombieWoundRegion::head;
    viewer::ZombieWound wound; wound.revision=1;
    cavities.wound(0,asset,geometry,wound,glm::translate(glm::mat4(1),{0,2,0}),{});
    geometry.region=viewer::ZombieWoundRegion::torso;
    cavities.wound(1,asset,geometry,wound,glm::translate(glm::mat4(1),{5,2,0}),{});
    const auto hit=viewer::raycastPhysicsProp(physics,{0,2,1},{0,0,-1},2);
    require(hit.has_value(),"The cavity fixture must expose a normally grabbable loose prop");
    const auto captured=cavities.captureItem(hit->prop);
    require(captured && captured->model.parts.size()==1 && captured->model.parts[0].color==glm::vec3(.5f,.025f,.035f) &&
            captured->model.massKg>0 && cavities.contents().size()==7,
            "Cavity capture must preserve its red rendered cube without changing the wound or its contents");
    require(cavities.eraseItem(hit->prop) && !hit->prop.valid() && !cavities.eraseItem(hit->prop) &&
            cavities.contents().size()==6 && cavities.contentsForIds({0},20).empty() && cavities.contentsForIds({1},20).size()==6,
            "Acquiring one cavity prop must remove only its owned body and omit it from rendered and replicated contents");
    wound.revision=2; geometry.region=viewer::ZombieWoundRegion::head;
    cavities.wound(0,asset,geometry,wound,glm::translate(glm::mat4(1),{0,2,0}),{});
    require(cavities.contents().size()==6,"Later wound geometry updates must not respawn an acquired loose prop");
}
}
int main() {
    try {
        bodyLocalCaptureAndParkedMass(); exactVisuals(); cavityOwnerCleanup();
        std::cout<<"Physical prop item geometry, visual capture and cavity acquisition checks passed\n";
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
