#include "grip_placement.hpp"
#include "camera.hpp"
#include "ground_items.hpp"
#include "item_model_geometry.hpp"
#include "inventory/crafted_item.hpp"
#include "object_interaction.hpp"
#include "physics_world.hpp"
#include <box3d/box3d.h>
#include <algorithm>
#include <chrono>
#include <map>
#include <limits>
#include <set>
#include <stdexcept>
#include <iostream>

namespace viewer {
namespace {
b3Vec3 vector(glm::vec3 v) { return {v.x,v.y,v.z}; }
template<class V> glm::vec3 vector(V v) { return {float(v.x),float(v.y),float(v.z)}; }
glm::quat quatFromPhysics(b3Quat q) { return {q.s,q.v.x,q.v.y,q.v.z}; }
glm::quat aligned(glm::vec3 normal,glm::quat previous) {
    const auto z=-glm::normalize(normal);
    auto y=previous*glm::vec3(0,1,0); y-=z*glm::dot(y,z);
    if (glm::length(y)<.01f) { y=std::abs(z.y)<.9f ? glm::vec3(0,1,0) : glm::vec3(1,0,0); y-=z*glm::dot(y,z); }
    y=glm::normalize(y); const auto x=glm::normalize(glm::cross(y,z));
    return glm::normalize(glm::quat_cast(glm::mat3(x,glm::cross(z,x),z)));
}
struct Ray { b3BodyId body{}; glm::vec3 point{0},normal{0}; float fraction=1; };
float hit(b3ShapeId shape,b3Pos point,b3Vec3 normal,float fraction,std::uint64_t,int,int,void* context) {
    if (b3Shape_IsSensor(shape)) return -1;
    auto& ray=*static_cast<Ray*>(context);
    if (fraction<=ray.fraction) { ray.body=b3Shape_GetBody(shape); ray.point=vector(point); ray.normal=vector(normal); ray.fraction=fraction; }
    return ray.fraction;
}
bool propBody(b3BodyId body) {
    if (!b3Body_IsValid(body) || b3Body_GetType(body)==b3_staticBody) return false;
    std::vector<b3ShapeId> shapes(static_cast<std::size_t>(b3Body_GetShapeCount(body)));
    const int count=b3Body_GetShapes(body,shapes.data(),static_cast<int>(shapes.size()));
    for (int i=0;i<count;++i)
        if (b3Shape_GetFilter(shapes[static_cast<std::size_t>(i)]).categoryBits&(interiorBoxCategory|zombieContentsCategory)) return true;
    return false;
}
std::vector<b3BodyId> assembly(b3BodyId root,const PhysicsWorld& world) {
    std::map<std::uint64_t,b3BodyId> bodies{{b3StoreBodyId(root),root}};
    std::vector<b3BodyId> pending{root};
    for (std::size_t next=0;next<pending.size();++next) {
        const auto body=pending[next];
        if (world.isGrabbed(body)) throw std::runtime_error("Release the edited assembly before saving its grip");
        std::vector<b3JointId> joints(static_cast<std::size_t>(b3Body_GetJointCount(body)));
        const int count=b3Body_GetJoints(body,joints.data(),static_cast<int>(joints.size()));
        for (int i=0;i<count;++i) {
            const auto joint=joints[static_cast<std::size_t>(i)];
            if (b3Joint_GetType(joint)!=b3_revoluteJoint && b3Joint_GetType(joint)!=b3_distanceJoint) continue;
            const auto a=b3Joint_GetBodyA(joint),b=b3Joint_GetBodyB(joint);
            const auto other=B3_ID_EQUALS(a,body) ? b : a;
            if (propBody(other) && bodies.emplace(b3StoreBodyId(other),other).second) pending.push_back(other);
        }
        if (pending.size()>64) throw std::runtime_error("Crafted assemblies support at most 64 physical parts");
    }
    return pending;
}
std::string material(std::uint64_t id) {
    return id==propMaterialId("wood") ? "wood" : id==propMaterialId("cardboard") ? "cardboard" : "unknown";
}
Item genericItem() {
    ItemInventoryConfig config; config.name="Crafted object"; config.type="equipment";
    config.width=config.height=2; config.acoustics="crate";
    return Item("crafted_object",config,WeaponConfig{},{"crafting"});
}
}

struct GripPlacement::Impl {
    struct Editor {
        GripPreviewPose pose;
        b3BodyId target{};
        glm::vec3 localPoint{0};
        glm::quat localRotation{1,0,0,0};
    };
    GroundItems& items;
    std::shared_ptr<PhysicsWorld> world;
    std::filesystem::path saveDirectory;
    BoxLayer* boxes=nullptr;
    std::uint64_t next=1;
    std::map<std::uint64_t,Editor> editors;
    std::vector<GripPreviewPose> poses;
    Impl(GroundItems& ground,std::shared_ptr<PhysicsWorld> physics,std::filesystem::path directory)
        : items(ground),world(std::move(physics)),saveDirectory(std::move(directory)) {
        world->registerStepCallbacks(this,[this] { publish(); },[this] { publish(); });
    }
    ~Impl() { world->unregisterStepCallbacks(this); }
    bool owned(b3BodyId body) const {
        const PhysicsProp prop(world,body);
        return items.idOf(prop).has_value() || (boxes && boxes->indexOf(prop).has_value());
    }
    void publish() {
        poses.clear();
        for (auto& [id,editor]:editors) {
            (void)id;
            if (B3_IS_NON_NULL(editor.target)) {
                if (!b3Body_IsValid(editor.target)) continue;
                editor.pose.position=vector(b3Body_GetWorldPoint(editor.target,vector(editor.localPoint)));
                editor.pose.rotation=glm::normalize(quatFromPhysics(b3Body_GetRotation(editor.target))*editor.localRotation);
            }
            poses.push_back(editor.pose);
        }
    }
    std::vector<ItemModelPart> parts(b3BodyId body) {
        const auto* source=items.find(PhysicsProp(world,body));
        if (source && !source->item.inventory().model.parts.empty()) {
            auto result=source->item.inventory().model.parts;
            const auto bounds=itemModelBounds(source->item.inventory().model);
            for (auto& part:result) part.center-=(bounds.minimum+bounds.maximum)*.5f;
            return result;
        }
        std::vector<ItemModelPart> result;
        if (boxes) if (const auto index=boxes->indexOf(PhysicsProp(world,body))) {
            for (const auto& collider:boxes->colliders(*index)) {
                ItemModelPart part; part.name="part"; part.center=collider.center; part.rotation=collider.rotation;
                part.size=collider.halfExtents*2.f; part.color={.48f,.34f,.22f}; part.material=material(collider.materialId);
                result.push_back(part);
            }
            return result;
        }
        // Other grabbable primitives (for example an articulated canopy panel)
        // have no authored item model. Preserve each physical part separately.
        std::vector<b3ShapeId> shapes(static_cast<std::size_t>(b3Body_GetShapeCount(body)));
        const int count=b3Body_GetShapes(body,shapes.data(),static_cast<int>(shapes.size()));
        for (int i=0;i<count;++i) {
            const auto shape=shapes[static_cast<std::size_t>(i)];
            if (b3Shape_GetType(shape)!=b3_hullShape) throw std::runtime_error("Set grip requires primitive solid parts");
            const auto* hull=b3Shape_GetHull(shape); const auto* vertices=b3GetHullPoints(hull);
            glm::vec3 minimum(std::numeric_limits<float>::infinity()),maximum=-minimum;
            for (int vertex=0;vertex<hull->vertexCount;++vertex) { minimum=glm::min(minimum,vector(vertices[vertex])); maximum=glm::max(maximum,vector(vertices[vertex])); }
            ItemModelPart part; part.name="part"; part.center=(minimum+maximum)*.5f; part.size=maximum-minimum;
            part.color={.48f,.34f,.22f}; part.material=material(b3Shape_GetSurfaceMaterial(shape).userMaterialId);
            result.push_back(part);
        }
        return result;
    }
    void save(Editor& editor) {
        const auto bodies=assembly(editor.target,*world);
        const auto origin=vector(b3Body_GetPosition(editor.target));
        const auto q=quatFromPhysics(b3Body_GetRotation(editor.target)); const auto inverse=glm::conjugate(q);
        auto base=items.find(PhysicsProp(world,editor.target)) ? items.find(PhysicsProp(world,editor.target))->item : genericItem();
        ItemModelConfig model; model.massKg=0;
        std::set<std::uint64_t> groundIds;
        std::vector<std::size_t> ordinary;
        for (const auto body:bodies) {
            if (!owned(body)) throw std::runtime_error("The assembly contains an unsupported prop");
            const auto bodyQ=quatFromPhysics(b3Body_GetRotation(body));
            const auto offset=inverse*(vector(b3Body_GetPosition(body))-origin); const auto localQ=inverse*bodyQ;
            for (auto part:parts(body)) {
                part.center=offset+localQ*part.center; part.rotation=glm::normalize(localQ*part.rotation);
                part.name="part_"+std::to_string(model.parts.size()); model.parts.push_back(std::move(part));
            }
            if (model.parts.size()>64) throw std::runtime_error("Crafted assemblies support at most 64 model parts");
            std::vector<b3ShapeId> shapes(static_cast<std::size_t>(b3Body_GetShapeCount(body)));
            const int count=b3Body_GetShapes(body,shapes.data(),static_cast<int>(shapes.size()));
            for (int i=0;i<count;++i) model.massKg+=b3Shape_ComputeMassData(shapes[static_cast<std::size_t>(i)]).mass;
            if (const auto id=items.idOf(PhysicsProp(world,body))) groundIds.insert(*id);
            else if (boxes) if (const auto index=boxes->indexOf(PhysicsProp(world,body))) ordinary.push_back(*index);
        }
        model.massKg=std::max(.001f,model.massKg);
        const auto bounds=itemModelBounds(model); const auto center=(bounds.minimum+bounds.maximum)*.5f;
        for (auto& part:model.parts) part.center-=center;
        const ItemGrip grip{editor.localPoint-center,editor.localRotation};
        auto item=base.withCrafting(model,{grip});
        if (!base.crafted()) item=item.renamed("crafted_"+std::to_string(std::chrono::system_clock::now().time_since_epoch().count())+"_"+std::to_string(next++));
        // Validate and publish the durable definition before replacing any body.
        saveCraftedItem(saveDirectory/(item.id()+".toml"),item);
        const auto id=items.add(item,0,{},origin+q*center,{},q);
        world->parkCraftingProp(items.prop(id)->body());
        for (const auto old:groundIds) items.erase(old);
        for (const auto index:ordinary) boxes->retire(index);
    }
};

GripPlacement::GripPlacement(GroundItems& items,std::shared_ptr<PhysicsWorld> world,std::filesystem::path directory)
    : impl_(std::make_unique<Impl>(items,std::move(world),std::move(directory))) {}
GripPlacement::~GripPlacement()=default;
void GripPlacement::setBoxes(BoxLayer* boxes) { if (boxes) impl_->boxes=boxes; }
std::uint64_t GripPlacement::begin(const Camera& camera) {
    if (impl_->editors.size()>=32) throw std::runtime_error("Too many active grip editors");
    const auto id=impl_->next++;
    impl_->editors.emplace(id,Impl::Editor{{camera.position()+camera.direction()*.7f,aligned(-camera.direction(),{1,0,0,0})},{},{},{1,0,0,0}});
    impl_->publish(); return id;
}
bool GripPlacement::active(std::uint64_t id) const { return impl_->editors.count(id)!=0; }
void GripPlacement::move(std::uint64_t id,const Camera& camera) {
    const auto found=impl_->editors.find(id); if (found==impl_->editors.end()) return;
    auto& editor=found->second;
    Ray ray; auto filter=b3DefaultQueryFilter(); filter.maskBits&=~(ragdollFloorCategory|playerBodyCategory|zombieDoorBlockerCategory);
    b3World_CastRay(impl_->world->id(),{camera.position().x,camera.position().y,camera.position().z},
        vector(camera.direction()*ObjectInteraction::rayReach(camera.direction())),filter,hit,&ray);
    if (propBody(ray.body) && impl_->owned(ray.body) && !impl_->world->isGrabbed(ray.body)) {
        editor.target=ray.body; editor.localPoint=vector(b3Body_GetLocalPoint(ray.body,vector(ray.point)));
        editor.pose={ray.point,aligned(ray.normal,editor.pose.rotation)};
        editor.localRotation=glm::conjugate(quatFromPhysics(b3Body_GetRotation(ray.body)))*editor.pose.rotation;
    } else {
        editor.target=b3_nullBodyId; editor.pose.position=camera.position()+camera.direction()*.7f;
    }
    impl_->publish();
}
void GripPlacement::release(std::uint64_t) { impl_->publish(); }
glm::quat GripPlacement::rotation(std::uint64_t id) const {
    const auto found=impl_->editors.find(id); return found==impl_->editors.end() ? glm::quat(1,0,0,0) : found->second.pose.rotation;
}
void GripPlacement::rotate(std::uint64_t id,glm::quat q) {
    if (!std::isfinite(glm::length(q)) || glm::length(q)<.001f) throw std::invalid_argument("Invalid grip rotation");
    const auto found=impl_->editors.find(id); if (found==impl_->editors.end()) return;
    auto& editor=found->second; editor.pose.rotation=glm::normalize(q);
    if (b3Body_IsValid(editor.target)) editor.localRotation=glm::conjugate(quatFromPhysics(b3Body_GetRotation(editor.target)))*editor.pose.rotation;
    impl_->publish();
}
void GripPlacement::finish(std::uint64_t id) {
    const auto found=impl_->editors.find(id); if (found==impl_->editors.end()) return;
    if (b3Body_IsValid(found->second.target)) {
        try { impl_->save(found->second); }
        catch (const std::exception& error) { std::cerr<<"Cannot save grip: "<<error.what()<<'\n'; }
    }
    impl_->editors.erase(found); impl_->publish();
}
void GripPlacement::cancel(std::uint64_t id) { impl_->editors.erase(id); impl_->publish(); }
const std::vector<GripPreviewPose>& GripPlacement::previews() const { return impl_->poses; }
}
