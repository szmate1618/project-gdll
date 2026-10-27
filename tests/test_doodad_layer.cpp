#include "doodad_layer.hpp"
#include "doodad_state.hpp"
#include "forest_layer.hpp"
#include "box_layer.hpp"
#include "collision_world.hpp"
#include "door_layer.hpp"
#include "physics_grab.hpp"
#include "physics_scene.hpp"
#include "interior_fixture.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
viewer::Model ground() {
    viewer::Model model;
    model.materials.push_back({});
    interior_fixture::quad(model, {-1024,0,-1024}, {-1024,0,1024}, {1024,0,1024}, {1024,0,-1024}, "terrain");
    interior_fixture::bounds(model);
    return model;
}
viewer::ForestLayer woodland() {
    viewer::ForestLayer forests;
    viewer::ForestPatch patch;
    patch.id="test-forest";
    patch.footprint.exterior={{-96,-96},{96,-96},{96,96},{-96,96},{-96,-96}};
    forests.patches.push_back(std::move(patch));
    return forests;
}
std::array<viewer::Model,viewer::doodadKindCount> models() {
    auto model=viewer::BoxLayer::unitCube();
    for (auto& primitive:model.primitives) for (auto& vertex:primitive.vertices)
        vertex.position=vertex.position*.1f+glm::vec3(0,.05f,0);
    model.boundsMin={-.05f,0,-.05f}; model.boundsMax={.05f,.1f,.05f};
    return {model,model,model};
}
struct Fixture {
    viewer::Model model=ground();
    viewer::ForestLayer forests=woodland();
    std::shared_ptr<viewer::PhysicsWorld> physics=std::make_shared<viewer::PhysicsWorld>();
    viewer::CollisionWorld collision{model};
    viewer::PhysicsScene scene{physics,collision,model.boundsMin,model.boundsMax};
    viewer::DoorLayer doors{{},physics};
    viewer::DoodadPlacementGenerator generator{forests,collision};
    viewer::DoodadState state{"fixture-map"};
    std::array<viewer::Model,viewer::doodadKindCount> assets=models();
    void tick(viewer::DoodadLayer& layer, glm::vec3 eye) {
        layer.update(eye,scene,doors);
        physics->step(1.f/60);
        layer.captureChanges();
    }
};
const viewer::DoodadRenderInstance* instance(const viewer::DoodadLayer& layer, std::uint64_t id) {
    const auto& values=layer.instances();
    const auto it=std::find_if(values.begin(),values.end(),[&](const auto& item){return item.id==id;});
    return it==values.end()?nullptr:&*it;
}
void boundedStreamingAndDeterministicReturn() {
    Fixture f;
    viewer::DoodadLayer layer(f.physics,f.generator,f.state,f.assets);
    layer.update({0,1.7f,0},f.scene,f.doors);
    require(layer.cellCount()>0 && layer.cellCount()<100,"Streaming must retain only a bounded neighborhood of cells");
    require(layer.instances().size()>100 && layer.activeBodyCount()<32,
            "Dense visual clutter must not create one physics body per rendered instance");
    const auto candidate=f.generator.generate({1,1}).front();
    const auto* first=instance(layer,candidate.id);
    require(first,"A nearby forest candidate must be rendered");
    const auto expected=first->transform;
    for (int i=0;i<90;++i) f.tick(layer,{0,1.7f,0});
    layer.update({512,1.7f,512},f.scene,f.doors);
    require(!instance(layer,candidate.id),"Distant untouched cells must unload");
    require(layer.cellCount()<100,"Exploring must not accumulate every visited cell");
    layer.update({0,1.7f,0},f.scene,f.doors);
    const auto* again=instance(layer,candidate.id);
    require(again && glm::length(glm::vec3(again->transform[3]-expected[3]))<1e-6f,
            "Returning to an untouched candidate must recover its original position");
    std::set<std::uint64_t> ids;
    for (const auto& item:layer.instances()) require(ids.insert(item.id).second,"A streamed identity must appear only once");
}
void heldBodySurvivesEviction() {
    Fixture f;
    const auto candidate=f.generator.generate({0,0}).front();
    viewer::DoodadLayer layer(f.physics,f.generator,f.state,f.assets);
    layer.update(candidate.position+glm::vec3(0,1.7f,0),f.scene,f.doors);
    const auto* rendered=instance(layer,candidate.id);
    require(rendered,"Held fixture must stream the selected candidate");
    const auto center=(rendered->minimum+rendered->maximum)*.5f;
    const auto hit=viewer::raycastPhysicsProp(f.physics,center+glm::vec3(0,.5f,0),{0,-1,0},1);
    require(hit.has_value(),"Nearby doodads must join the existing grabbable-prop query");
    viewer::PhysicsGrab grip(hit->prop,hit->localPoint,180);
    require(hit->prop.grabbed(),"Streaming must observe the shared hand constraint");
    layer.update({512,1.7f,512},f.scene,f.doors);
    require(hit->prop.valid() && grip.valid() && instance(layer,candidate.id),
            "Evicting a held object's source cell must preserve the body, joint and render instance");
}
void movingAcrossCellsAndReconstructingLayer() {
    Fixture f;
    const auto original=f.generator.generate({0,0}).front();
    glm::vec3 movedPosition;
    {
        viewer::DoodadLayer layer(f.physics,f.generator,f.state,f.assets);
        layer.update(original.position+glm::vec3(0,1.7f,0),f.scene,f.doors);
        const auto* rendered=instance(layer,original.id);
        require(rendered,"Cross-cell fixture must stream the original candidate");
        const auto center=(rendered->minimum+rendered->maximum)*.5f;
        const auto hit=viewer::raycastPhysicsProp(f.physics,center+glm::vec3(0,.5f,0),{0,-1,0},1);
        require(hit.has_value(),"Cross-cell fixture must find its physical prop");
        const auto startingPoint=hit->point;
        {
            viewer::PhysicsGrab grip(hit->prop,hit->localPoint,180);
            for (int frame=0;frame<600;++frame) {
                // Carry beyond the forest boundary and many source cells.
                const auto target=startingPoint+glm::vec3(frame*.2f,.8f,0);
                grip.setTarget(target);
                f.tick(layer,target+glm::vec3(0,1.7f,0));
            }
        }
        const auto eye=hit->prop.center()+glm::vec3(0,1.7f,0);
        for (int frame=0;frame<180;++frame) f.tick(layer,eye);
        layer.captureChanges();
        require(f.state.contains(original.id),"Moving a prop must create a sparse override");
        movedPosition=f.state.changes().at(original.id).position;
        require(movedPosition.x>100 && viewer::doodadCellAt(movedPosition).x!=viewer::doodadCellAt(original.position).x,
                "An existing grab must carry a doodad across cell boundaries and outside woodland");
        require(instance(layer,original.id),"A moved prop must remain rendered in its destination");
    }
    viewer::DoodadLayer returned(f.physics,f.generator,f.state,f.assets);
    returned.update(movedPosition+glm::vec3(0,1.7f,0),f.scene,f.doors);
    const auto* restored=instance(returned,original.id);
    require(restored && glm::distance(glm::vec3(restored->transform[3]),movedPosition)<.003f,
            "Reconstructing the layer must load an override in its nonforest destination cell");
    returned.update(original.position+glm::vec3(0,1.7f,0),f.scene,f.doors);
    const auto* old=instance(returned,original.id);
    require(!old || glm::distance(glm::vec3(old->transform[3]),original.position)>50,
            "Returning to the source cell must never regenerate a moved object at its old position");
}
}
int main() {
    try {
        boundedStreamingAndDeterministicReturn();
        heldBodySurvivesEviction();
        movingAcrossCellsAndReconstructingLayer();
        std::cout<<"Doodad streaming and shared-physics checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
