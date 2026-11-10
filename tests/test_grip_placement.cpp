#include "grip_placement.hpp"
#include "box_layer.hpp"
#include "camera.hpp"
#include "ground_items.hpp"
#include "inventory/crafted_item.hpp"
#include "inventory/item_config.hpp"
#include "item_model_geometry.hpp"
#include "physics_world.hpp"
#include <box3d/box3d.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
const std::filesystem::path root(VIEWER_ITEM_ROOT);
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
glm::vec3 vector(b3Vec3 value) { return {value.x,value.y,value.z}; }
bool sameRotation(glm::quat a,glm::quat b) { return std::abs(glm::dot(glm::normalize(a),glm::normalize(b)))>.99999f; }

b3BodyId bodyOf(const std::shared_ptr<viewer::PhysicsWorld>& world,const viewer::PhysicsProp& prop) {
    struct Query { const viewer::PhysicsProp& prop; std::shared_ptr<viewer::PhysicsWorld> world; b3BodyId body{}; } query{prop,world,{}};
    const auto center=prop.center();
    b3World_OverlapAABB(world->id(),{{center.x-3,center.y-3,center.z-3},{center.x+3,center.y+3,center.z+3}},
        b3DefaultQueryFilter(),[](b3ShapeId shape,void* context) {
            auto& current=*static_cast<Query*>(context);
            const auto body=b3Shape_GetBody(shape);
            if (viewer::PhysicsProp(current.world,body)==current.prop) { current.body=body; return false; }
            return true;
        },&query);
    require(b3Body_IsValid(query.body),"Could not resolve a fixture body");
    return query.body;
}
viewer::Camera cameraAt(glm::vec3 position,glm::vec3 target) {
    const auto direction=glm::normalize(target-position);
    viewer::Camera camera; camera.setPosition(position);
    camera.setOrientation(glm::degrees(std::atan2(direction.z,direction.x)),glm::degrees(std::asin(direction.y)));
    return camera;
}
struct Fixture {
    std::filesystem::path directory=std::filesystem::temp_directory_path()/
        ("gd-grip-placement-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::shared_ptr<viewer::PhysicsWorld> world=std::make_shared<viewer::PhysicsWorld>(root/"storage");
    viewer::BoxLayer boxes{world};
    viewer::GroundItems items{world};
    viewer::GripPlacement grips{items,world,directory};
    Fixture() { grips.setBoxes(&boxes); }
    ~Fixture() { std::error_code ignored; std::filesystem::remove_all(directory,ignored); }
    viewer::PhysicsProp broom(glm::vec3 center={0,1,0},glm::quat q={1,0,0,0}) {
        return *items.prop(items.add(viewer::loadItem(root/"storage/items","broom"),0,{},center,{},q));
    }
    void step() { world->step(1.f/60); }
    std::size_t savedCount() const {
        if (!std::filesystem::exists(directory)) return 0;
        return static_cast<std::size_t>(std::distance(std::filesystem::directory_iterator(directory),std::filesystem::directory_iterator{}));
    }
};

void palmAttachmentRotationAndRelease() {
    Fixture fixture;
    const auto prop=fixture.broom(); const auto body=bodyOf(fixture.world,prop);
    const auto camera=cameraAt({0,1,.8f},{0,1,0});
    const auto id=fixture.grips.begin(camera);
    require(fixture.items.count()==1 && fixture.grips.active(id) && fixture.grips.previews().size()==1,
        "The always-present grip tool must begin without acquiring or creating an inventory item");
    fixture.grips.move(id,camera);
    const auto snapped=fixture.grips.previews().front();
    require(glm::distance(snapped.position,{0,1,.013f})<1e-5f && glm::dot(snapped.rotation*glm::vec3(0,0,1),glm::vec3(0,0,-1))>.99999f,
        "The palm heel must coincide with the hit surface, with hand +Z pointing into the object");
    fixture.grips.release(id);
    require(fixture.grips.active(id) && fixture.savedCount()==0 && !fixture.items.poses()[0].entry.item.crafted(),
        "Mouse release must retain a preview without prematurely saving the item");
    const auto turned=glm::angleAxis(.6f,glm::normalize(glm::vec3(1,2,0)))*snapped.rotation;
    fixture.grips.rotate(id,turned);
    require(glm::distance(fixture.grips.previews()[0].position,snapped.position)<1e-6f && sameRotation(fixture.grips.rotation(id),turned),
        "Rotating the grip must preserve its palm-heel pivot");
    const auto localPoint=vector(b3Body_GetLocalPoint(body,{snapped.position.x,snapped.position.y,snapped.position.z}));
    const auto q=glm::angleAxis(.4f,glm::vec3(0,1,0));
    b3Body_SetGravityScale(body,0);
    b3Body_SetTransform(body,{.3f,1.2f,-.1f},{{q.x,q.y,q.z},q.w});
    fixture.step();
    const auto moved=fixture.grips.previews()[0];
    require(glm::distance(moved.position,vector(b3Body_GetWorldPoint(body,{localPoint.x,localPoint.y,localPoint.z})))<1e-5f &&
            sameRotation(moved.rotation,q*turned),
        "A released marker must follow the target's local point and rotation");
    require(b3World_GetCounters(fixture.world->id()).jointCount==0,"A ghost hand must not add a physical constraint");
    fixture.grips.cancel(id);
    require(!fixture.grips.active(id) && fixture.grips.previews().empty() && fixture.savedCount()==0 && prop.valid(),
        "Cancelling must clear the ghost and preserve the source object without a save");
}

struct ExpectedPart { glm::vec3 center,size; glm::quat rotation; std::string material; };
void appendExpected(std::vector<ExpectedPart>& result,const viewer::GroundItemPose& pose) {
    const auto bounds=viewer::itemModelBounds(pose.entry.item.inventory().model);
    const auto center=(bounds.minimum+bounds.maximum)*.5f;
    for (const auto& part:pose.entry.item.inventory().model.parts)
        result.push_back({pose.body.center+pose.body.rotation*(part.center-center),part.size,pose.body.rotation*part.rotation,part.material});
}
void join(const std::shared_ptr<viewer::PhysicsWorld>& world,b3BodyId a,b3BodyId b) {
    auto definition=b3DefaultRevoluteJointDef(); definition.base.bodyIdA=a; definition.base.bodyIdB=b;
    const auto point=b3Body_GetPosition(a);
    definition.base.localFrameA.p=b3Body_GetLocalPoint(a,point); definition.base.localFrameB.p=b3Body_GetLocalPoint(b,point);
    require(b3Joint_IsValid(b3CreateRevoluteJoint(world->id(),&definition)),"Could not join the fixture assembly");
}
void requirePrimitiveColliders(b3BodyId body,const viewer::ItemModelConfig& model) {
    std::vector<b3ShapeId> shapes(static_cast<std::size_t>(b3Body_GetShapeCount(body)));
    b3Body_GetShapes(body,shapes.data(),static_cast<int>(shapes.size()));
    require(shapes.size()==model.parts.size(),"Saved physics must keep a separate collider for each model primitive");
    const auto bounds=viewer::itemModelBounds(model); const auto center=(bounds.minimum+bounds.maximum)*.5f;
    std::vector<bool> matched(model.parts.size(),false);
    for (const auto shape:shapes) {
        require(b3Shape_GetType(shape)==b3_hullShape,"Saved primitive physics must use solid hulls");
        const auto* hull=b3Shape_GetHull(shape); const auto* vertices=b3GetHullPoints(hull);
        require(hull->vertexCount==8,"Each box or approximated frustum must use an eight-vertex collider");
        bool found=false;
        for (std::size_t index=0;index<model.parts.size() && !found;++index) {
            if (matched[index]) continue;
            const auto& part=model.parts[index]; auto half=part.size*.5f;
            if (part.shape==viewer::ItemModelShape::Frustum) { half.x=std::max(half.x,part.topSize.x*.5f); half.z=std::max(half.z,part.topSize.y*.5f); }
            bool allVertices=true;
            for (int vertex=0;vertex<hull->vertexCount && allVertices;++vertex) {
                bool cornerFound=false;
                for (int corner=0;corner<8;++corner) {
                    const auto local=glm::vec3(corner&1 ? half.x : -half.x,corner&2 ? half.y : -half.y,corner&4 ? half.z : -half.z);
                    cornerFound|=glm::distance(vector(vertices[vertex]),part.center-center+part.rotation*local)<2e-5f;
                }
                allVertices=cornerFound;
            }
            if (allVertices) {
                require(b3Shape_GetSurfaceMaterial(shape).userMaterialId==viewer::propMaterialId(part.material),
                    "Completed assemblies must use each part's material instead of inheriting a wooden root tag");
                matched[index]=true; found=true;
            }
        }
        require(found,"A saved collider must retain its primitive's local center, size, and rotation");
    }
}
void finishedAssemblyPreservesEveryPart() {
    Fixture fixture;
    const auto q=glm::angleAxis(.35f,glm::normalize(glm::vec3(1,2,3)));
    const glm::vec3 origin{0,1,0};
    const auto broomDefinition=viewer::loadItem(root/"storage/items","broom"); auto tags=broomDefinition.tags(); tags.push_back("wood");
    const auto woodenRoot=viewer::Item(broomDefinition.id(),broomDefinition.inventory(),{},tags);
    const auto broom=*fixture.items.prop(fixture.items.add(woodenRoot,0,{},origin,{},q)); const auto broomBody=bodyOf(fixture.world,broom);
    auto tape=viewer::loadItem(root/"storage/items","duct_tape");
    auto tapeConfig=tape.inventory(); tapeConfig.model.parts[0].rotation=glm::angleAxis(.4f,glm::vec3(0,0,1));
    tape=viewer::Item(tape.id(),tapeConfig,{},tape.tags());
    const auto tapeProp=*fixture.items.prop(fixture.items.add(tape,1000,{},origin+q*glm::vec3(.35f,.1f,0),{},q*glm::angleAxis(.55f,glm::vec3(0,1,0))));
    const auto partQ=glm::angleAxis(.65f,glm::vec3(0,0,1));
    const std::vector<viewer::PropCollider> colliders{
        {viewer::PropColliderShape::Box,{-.1f,0,0},{.12f,.035f,.04f},viewer::propMaterialId("wood"),partQ},
        {viewer::PropColliderShape::Box,{.15f,.04f,0},{.06f,.025f,.03f},0,glm::quat(1,0,0,0)}};
    viewer::BoxSpawnMotion motion; motion.rotation=q*glm::angleAxis(-.3f,glm::vec3(1,0,0));
    const auto boxIndex=fixture.boxes.addCompound(origin+q*glm::vec3(-.4f,.1f,0),colliders,motion);
    const auto boxProp=fixture.boxes.prop(boxIndex);
    join(fixture.world,broomBody,bodyOf(fixture.world,tapeProp)); join(fixture.world,broomBody,bodyOf(fixture.world,boxProp));
    std::vector<ExpectedPart> expected;
    for (const auto& pose:fixture.items.poses()) appendExpected(expected,pose);
    const auto boxPose=fixture.boxes.pose(boxIndex);
    for (const auto& part:colliders)
        expected.push_back({boxPose.center+boxPose.rotation*part.center,part.halfExtents*2.f,boxPose.rotation*part.rotation,
            part.materialId==viewer::propMaterialId("wood") ? "wood" : "unknown"});
    const auto camera=cameraAt(origin+q*glm::vec3(0,0,.8f),origin);
    const auto id=fixture.grips.begin(camera); fixture.grips.move(id,camera);
    const auto preview=fixture.grips.previews()[0]; fixture.grips.release(id); fixture.grips.finish(id);
    require(!fixture.grips.active(id) && fixture.grips.previews().empty() && fixture.items.count()==1 && fixture.savedCount()==1,
        "Finishing must replace the entire joined assembly with one saved equippable item");
    require(!broom.valid() && !tapeProp.valid() && !boxProp.valid() && !fixture.boxes.active(boxIndex) &&
            b3World_GetCounters(fixture.world->id()).jointCount==0,
        "Saving must remove every old body and joint, including ordinary BoxLayer parts");
    const auto saved=fixture.items.poses()[0]; const auto& item=saved.entry.item;
    require(item.crafted() && item.grips().size()==1 && item.inventory().model.parts.size()==expected.size() && saved.entry.quantity==0 && !saved.entry.weapon,
        "The completed item must own all actual model primitives and its grip as one nonstackable assembly");
    const auto bounds=viewer::itemModelBounds(item.inventory().model); const auto modelCenter=(bounds.minimum+bounds.maximum)*.5f;
    require(glm::distance(saved.body.center+saved.body.rotation*(item.grips()[0].position-modelCenter),preview.position)<2e-5f &&
            sameRotation(saved.body.rotation*item.grips()[0].rotation,preview.rotation),
        "Recentering an asymmetric assembly must preserve the authored grip in world space");
    std::vector<bool> matched(expected.size(),false);
    for (const auto& part:item.inventory().model.parts) {
        bool found=false;
        for (std::size_t index=0;index<expected.size();++index) {
            const auto& before=expected[index];
            if (!matched[index] && glm::distance(saved.body.center+saved.body.rotation*(part.center-modelCenter),before.center)<2e-5f &&
                glm::distance(part.size,before.size)<1e-6f && sameRotation(saved.body.rotation*part.rotation,before.rotation) && part.material==before.material) {
                matched[index]=true; found=true; break;
            }
        }
        require(found,"A joined component's position, rotated primitive, dimensions, or material changed while saving");
    }
    const auto reloaded=viewer::loadCraftedItem(fixture.directory/(item.id()+".toml"));
    require(reloaded.inventory().model.parts.size()==expected.size() && reloaded.grips()[0].position==item.grips()[0].position,
        "Reloading the saved assembly must retain its complete geometry and local grip");
    requirePrimitiveColliders(bodyOf(fixture.world,*fixture.items.prop(saved.entry.id)),item.inventory().model);
}

void genericBoxAndCancellation() {
    Fixture fixture;
    const auto index=fixture.boxes.add({0,1,0},{.4f,.3f,.2f}); const auto original=fixture.boxes.prop(index);
    const auto camera=cameraAt({0,1,.8f},{0,1,0});
    const auto cancelled=fixture.grips.begin(camera); fixture.grips.move(cancelled,camera); fixture.grips.cancel(cancelled);
    require(original.valid() && fixture.items.count()==0 && fixture.savedCount()==0,"Cancelling a generic prop edit must leave its owner untouched");
    const auto id=fixture.grips.begin(camera); fixture.grips.move(id,camera); fixture.grips.finish(id);
    require(!original.valid() && !fixture.boxes.active(index) && fixture.items.count()==1 && fixture.savedCount()==1,
        "An unmarked ordinary box must become a saved item without leaving a duplicate physical source");
    const auto pose=fixture.items.poses()[0]; const auto& item=pose.entry.item;
    require(item.crafted() && item.prototype()=="crafted_object" && item.inventory().model.parts.size()==1 &&
            item.inventory().model.parts[0].material=="unknown", "A generic box must preserve its primitive with an explicit unknown material");
    const auto floating=fixture.grips.begin(cameraAt({4,4,4},{4,4,3})); fixture.grips.finish(floating);
    require(fixture.savedCount()==1 && fixture.items.count()==1 && !fixture.grips.active(floating),
        "Finishing an unattached tool must not create an empty crafted object");
}
void unsupportedJoinedOwnerDoesNotDuplicate() {
    Fixture fixture;
    const auto broom=fixture.broom();
    auto bodyDefinition=b3DefaultBodyDef(); bodyDefinition.type=b3_dynamicBody; bodyDefinition.position={.4f,1,0};
    const auto foreign=b3CreateBody(fixture.world->id(),&bodyDefinition);
    auto shapeDefinition=b3DefaultShapeDef(); shapeDefinition.filter.categoryBits=viewer::zombieContentsCategory;
    const auto hull=b3MakeBoxHull(.1f,.1f,.1f);
    require(b3Shape_IsValid(b3CreateHullShape(foreign,&shapeDefinition,&hull.base)),"Could not create an unsupported-owner fixture");
    b3Body_ApplyMassFromShapes(foreign);
    const auto unsupported=fixture.grips.begin(cameraAt({.4f,1,.9f},{.4f,1,0}));
    fixture.grips.move(unsupported,cameraAt({.4f,1,.9f},{.4f,1,0})); fixture.grips.finish(unsupported);
    require(fixture.items.count()==1 && fixture.savedCount()==0 && b3Body_IsValid(foreign),
        "A body without a removable owner must not be duplicated by the grip tool");
    join(fixture.world,bodyOf(fixture.world,broom),foreign);
    const auto id=fixture.grips.begin(cameraAt({0,1,.8f},{0,1,0}));
    fixture.grips.move(id,cameraAt({0,1,.8f},{0,1,0})); fixture.grips.finish(id);
    require(fixture.items.count()==1 && fixture.savedCount()==0 && broom.valid() && b3Body_IsValid(foreign) &&
            b3World_GetCounters(fixture.world->id()).jointCount==1,
        "An unsupported connected owner must reject the complete save before changing any source or file");
}
}

int main() {
    try {
        palmAttachmentRotationAndRelease(); finishedAssemblyPreservesEveryPart(); genericBoxAndCancellation(); unsupportedJoinedOwnerDoesNotDuplicate();
        std::cout<<"Grip placement and assembly persistence checks passed\n";
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
