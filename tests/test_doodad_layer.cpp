#include "doodad_layer.hpp"
#include "doodad_state.hpp"
#include "forest_layer.hpp"
#include "box_layer.hpp"
#include "collision_world.hpp"
#include "door_layer.hpp"
#include "physics_grab.hpp"
#include "physics_scene.hpp"
#include "interior_fixture.hpp"
#include <box3d/box3d.h>
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
    model.draws.emplace_back();
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
void cumulativePersistenceMargins() {
    Fixture f;
    const auto original=f.generator.generate({0,0}).front();
    viewer::DoodadLayer layer(f.physics,f.generator,f.state,f.assets);
    layer.update(original.position+glm::vec3(0,1.7f,0),f.scene,f.doors);
    const auto* rendered=instance(layer,original.id);
    require(rendered,"Save-margin fixture must stream its candidate");
    const auto center=(rendered->minimum+rendered->maximum)*.5f;
    auto filter=b3DefaultQueryFilter();
    filter.maskBits=viewer::interiorBoxCategory;
    const auto hit=b3World_CastRayClosest(f.physics->id(),{center.x,center.y+.5f,center.z},{0,-1,0},filter);
    require(hit.hit,"Save-margin fixture must find its physical prop");
    const auto body=b3Shape_GetBody(hit.shapeId);
    // Inject solved poses without contact settling or gravity changing the
    // displacement under test. The real motion events still drive layer sync.
    b3Body_SetGravityScale(body,0);
    auto shapeFilter=b3Shape_GetFilter(hit.shapeId);
    shapeFilter.maskBits=0;
    b3Shape_SetFilter(hit.shapeId,shapeFilter,true);
    const auto setPose=[&](float offset, float degrees, bool negateQuaternion=false) {
        auto rotation=glm::angleAxis(glm::radians(degrees),glm::vec3(0,1,0))*original.rotation;
        if (negateQuaternion) rotation=-rotation;
        const auto kind=static_cast<std::size_t>(original.kind);
        const auto localCenter=(f.assets[kind].boundsMin+f.assets[kind].boundsMax)*.5f;
        const auto position=original.position+glm::vec3(offset,0,0)+rotation*(localCenter*original.scale);
        b3Body_SetTransform(body,{position.x,position.y,position.z},
                            {{rotation.x,rotation.y,rotation.z},rotation.w});
        b3Body_SetLinearVelocity(body,{0,0,0});
        b3Body_SetAngularVelocity(body,{0,0,0});
        b3Body_SetAwake(body,true);
        f.physics->step(1.f/60);
        layer.captureChanges();
    };
    for (const float offset:{.01f,.03f,.049f}) setPose(offset,0);
    require(!f.state.contains(original.id),"Displacements below 5 cm must not create an override");
    require(glm::distance(glm::vec3(instance(layer,original.id)->transform[3]),original.position)>.048f,
            "Subthreshold movement must still update the rendered pose");
    setPose(.051f,0);
    require(f.state.contains(original.id),"Cumulative movement beyond 5 cm must create an override");
    const auto firstRevision=f.state.revision();
    setPose(.09f,0);
    require(f.state.revision()==firstRevision,"Position threshold must use the last recorded pose");
    setPose(.102f,0);
    require(f.state.revision()>firstRevision,"Further cumulative movement must update the override");
    const auto positionRevision=f.state.revision();
    for (const float angle:{3.f,7.f,9.9f}) setPose(.102f,angle);
    require(f.state.revision()==positionRevision,"Rotations below 10 degrees must not update an override");
    setPose(.102f,10.1f);
    require(f.state.revision()>positionRevision,"Cumulative rotation beyond 10 degrees must update the override");
    const auto rotationRevision=f.state.revision();
    setPose(.102f,19.9f);
    require(f.state.revision()==rotationRevision,"Rotation threshold must use the last recorded pose");
    setPose(.102f,10.1f,true);
    require(f.state.revision()==rotationRevision,"Negating a quaternion must not count as rotation");
    setPose(.102f,20.2f);
    require(f.state.revision()>rotationRevision,"Further cumulative rotation must update the override");
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
void capturedGeometryAndAcquisition() {
    Fixture f;
    const auto candidate=f.generator.generate({0,0}).front();
    {
        viewer::DoodadLayer layer(f.physics,f.generator,f.state,f.assets);
        const auto eye=candidate.position+glm::vec3(0,1.7f,0);
        layer.update(eye,f.scene,f.doors);
        const auto* rendered=instance(layer,candidate.id);
        require(rendered,"The acquisition fixture must render its deterministic source");
        const auto sourceTransform=rendered->transform;
        const auto center=(rendered->minimum+rendered->maximum)*.5f;
        const auto hit=viewer::raycastPhysicsProp(f.physics,center+glm::vec3(0,.5f,0),{0,-1,0},1);
        require(hit.has_value(),"The acquisition fixture must expose its grabbable body");
        const auto captured=layer.captureItem(hit->prop);
        const auto& asset=f.assets[static_cast<std::size_t>(candidate.kind)];
        require(captured && captured->model.mesh.size()==36 && captured->model.parts.size()==1 &&
                captured->model.massKg>0 && instance(layer,candidate.id),
                "Doodad capture must retain its visual triangles and physical box without removing the source");
        const auto& mesh=asset.primitives[0];
        const auto& vertex=mesh.vertices[mesh.indices[0]];
        const auto expected=glm::vec3(sourceTransform*asset.draws[0].transform*glm::vec4(vertex.position,1));
        const auto actual=captured->pose.center+captured->pose.rotation*captured->model.mesh[0].position;
        require(glm::distance(actual,expected)<1e-5f &&
                glm::distance(captured->model.mesh[0].color,glm::vec3(asset.materials[0].baseColor)*glm::vec3(vertex.color))<1e-6f,
                "Doodad capture must preserve the exact asset origin, instance scale, rotation and material colors");
        const auto bodies=layer.activeBodyCount();
        require(layer.eraseItem(hit->prop) && !hit->prop.valid() && !layer.eraseItem(hit->prop) &&
                !instance(layer,candidate.id) && f.state.removed(candidate.id) && layer.activeBodyCount()+1==bodies,
                "Acquisition must remove the owning body and render instance and leave a persistent source tombstone");
        layer.update(eye,f.scene,f.doors);
        layer.update({512,1.7f,512},f.scene,f.doors); layer.update(eye,f.scene,f.doors);
        require(!instance(layer,candidate.id),"Streaming away and returning must not regenerate an acquired doodad");
    }
    viewer::DoodadLayer restored(f.physics,f.generator,f.state,f.assets);
    restored.update(candidate.position+glm::vec3(0,1.7f,0),f.scene,f.doors);
    require(!instance(restored,candidate.id),"Reconstructing the layer must preserve acquisition removal");
}
}
int main() {
    try {
        boundedStreamingAndDeterministicReturn();
        heldBodySurvivesEviction();
        cumulativePersistenceMargins();
        movingAcrossCellsAndReconstructingLayer();
        capturedGeometryAndAcquisition();
        std::cout<<"Doodad streaming and shared-physics checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
