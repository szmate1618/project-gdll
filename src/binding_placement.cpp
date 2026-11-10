#include "binding_placement.hpp"
#include "binding_geometry.hpp"
#include "camera.hpp"
#include "ground_items.hpp"
#include "physics_world.hpp"
#include <box3d/box3d.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace viewer {
namespace {
b3Vec3 vector(glm::vec3 v) { return {v.x,v.y,v.z}; }
using BodyKey = std::uint64_t;
BodyKey key(b3BodyId body) { return b3StoreBodyId(body); }
constexpr float pullForce = 30; // N per band; bounded tension cannot bypass contacts.
constexpr float minimumLength = .005f;

struct Target {
    b3BodyId body{};
    std::array<glm::vec3,5> points{};
    std::array<float,5> weights{};
    glm::vec3 point{0};
    float weight = 0;
    void add(const BindingSegment& segment) {
        const float length = glm::distance(segment.a,segment.b);
        const auto midpoint = (segment.a+segment.b)*.5f;
        points[segment.plane] += midpoint*length;
        weights[segment.plane] += length;
        point += midpoint*length; weight += length;
    }
    void normalize() {
        point /= weight;
        for (std::size_t plane=0;plane<points.size();++plane)
            points[plane] = weights[plane]>0 ? points[plane]/weights[plane] : point;
    }
};
struct Connection {
    b3BodyId a{}, b{};
    b3JointId joint{};
    std::uint8_t plane=0;
};
void destroy(std::vector<Connection>& connections) {
    for (const auto& connection:connections)
        if (b3Joint_IsValid(connection.joint)) b3DestroyJoint(connection.joint,true);
    connections.clear();
}
}

struct BindingPlacement::Impl {
    struct ReleasedPose { b3BodyId body; float gravity; bool parked; };
    struct Preview {
        PhysicsProp tool;
        std::array<BindingPlane,5> planes;
        BindingGeometry geometry;
        std::map<BodyKey,Target> targets;
        std::map<BodyKey,ReleasedPose> released;
        std::vector<Connection> pulls;
    };
    GroundItems& items;
    std::shared_ptr<PhysicsWorld> world;
    std::map<BodyKey,Preview> previews;
    std::map<BodyKey,BodyKey> owners; // One live pulling preview per movable body.
    std::vector<Connection> ties;
    std::vector<glm::vec3> lines;

    Impl(GroundItems& ground,std::shared_ptr<PhysicsWorld> simulation)
        : items(ground),world(std::move(simulation)) {
        if (!world) throw std::invalid_argument("Binding requires a physics world");
        world->registerStepCallbacks(this,[this] { prepare(); },[this] { redraw(); });
        try { world->registerGrabCallback(this,[this](b3BodyId body) { resumeBundle(body); }); }
        catch (...) { world->unregisterStepCallbacks(this); throw; }
    }
    ~Impl() {
        world->unregisterGrabCallback(this);
        world->unregisterStepCallbacks(this);
        while (!previews.empty()) erase(previews.begin());
        destroy(ties);
    }
    void resumeBundle(b3BodyId body) {
        // This callback runs before a new hand saves its gravity/type. Return
        // temporary preview state now, so that hand cannot capture gravity=0.
        for (auto& [tool,preview]:previews) {
            (void)tool;
            const auto released=preview.released.find(key(body));
            if (released==preview.released.end()) continue;
            destroy(preview.pulls);
            restore(released->first,released->second,false);
            preview.released.erase(released);
            preview.targets.erase(key(body));
        }
        std::set<BodyKey> connected{key(body)};
        bool changed;
        do {
            changed=false;
            for (const auto& tie:ties) if (b3Joint_IsValid(tie.joint)) {
                if (connected.count(key(tie.a))) changed |= connected.insert(key(tie.b)).second;
                if (connected.count(key(tie.b))) changed |= connected.insert(key(tie.a)).second;
            }
        } while (changed);
        for (const auto& tie:ties) if (b3Joint_IsValid(tie.joint)) {
            if (connected.count(key(tie.a))) world->resumeCraftingProp(tie.a);
            if (connected.count(key(tie.b))) world->resumeCraftingProp(tie.b);
        }
    }
    void restore(BodyKey id,const ReleasedPose& released,bool allowPark=true) {
        owners.erase(id);
        if (!b3Body_IsValid(released.body)) return;
        b3Body_SetGravityScale(released.body,released.gravity);
        if (released.parked && allowPark && !world->isGrabbed(released.body)) world->parkCraftingProp(released.body);
    }
    void erase(std::map<BodyKey,Preview>::iterator preview) {
        destroy(preview->second.pulls);
        for (const auto& [id,released]:preview->second.released) restore(id,released);
        previews.erase(preview);
    }
    std::map<BodyKey,Target> query(Preview& preview) {
        preview.geometry=queryBindingGeometry(world,preview.planes,preview.tool.body());
        std::map<BodyKey,Target> targets;
        for (const auto& segment:preview.geometry.segments) {
            const auto body=segment.body;
            if (!b3Body_IsValid(body) || world->isGrabbed(body)) continue;
            const auto owner=owners.find(key(body));
            if (owner!=owners.end() && owner->second!=key(preview.tool.body())) continue;
            // A surface-pinned nail owns a separate placement preview. It can
            // follow its target but must not be pulled out by a binding probe.
            const auto* item=items.find(PhysicsProp(world,body));
            if (item && item->item.hasTag("nail") && world->isCraftingHeld(body)) continue;
            auto& target=targets[key(body)]; target.body=body; target.add(segment);
        }
        for (auto& [id,target]:targets) { (void)id; target.normalize(); }
        return targets;
    }
    void refresh(Preview& preview) {
        auto targets=query(preview);
        for (auto it=preview.released.begin();it!=preview.released.end();) {
            if (!targets.count(it->first)) { restore(it->first,it->second); it=preview.released.erase(it); }
            else ++it;
        }
        for (auto it=targets.begin();it!=targets.end();) {
            const auto body=it->second.body;
            if (b3Body_GetType(body)==b3_staticBody) { ++it; continue; }
            if (!preview.released.count(it->first)) {
                const bool parked=world->isCraftingHeld(body);
                if (parked) world->resumeCraftingProp(body);
                if (b3Body_GetType(body)!=b3_dynamicBody || world->isCraftingHeld(body)) {
                    if (parked) world->parkCraftingProp(body);
                    it=targets.erase(it); continue;
                }
                const float gravity=b3Body_GetGravityScale(body);
                preview.released.emplace(it->first,ReleasedPose{body,gravity,parked});
                owners[it->first]=key(preview.tool.body());
                if (parked) b3Body_SetGravityScale(body,0); // Keep the suspended height while tightening.
            }
            ++it;
        }
        preview.targets=std::move(targets);
        connect(preview,false);
    }
    void connect(Preview& preview,bool permanent) {
        if (preview.targets.size()<2) { if (!permanent) destroy(preview.pulls); return; }
        auto& output=permanent ? ties : preview.pulls;
        std::vector<Connection> next;
        if (permanent) output.reserve(output.size()+5*(preview.targets.size()-1));
        else next.reserve(5*(preview.targets.size()-1));
        for (std::uint8_t plane=0;plane<5;++plane) {
            auto anchor=preview.targets.end();
            for (auto it=preview.targets.begin();it!=preview.targets.end();++it) if (it->second.weights[plane]>0) {
                if (anchor==preview.targets.end()) anchor=it;
                if (b3Body_GetType(it->second.body)==b3_staticBody) { anchor=it; break; }
            }
            if (anchor==preview.targets.end()) continue;
            for (const auto& [id,target]:preview.targets) {
                if (id==anchor->first || target.weights[plane]<=0 ||
                    (b3Body_GetType(target.body)!=b3_dynamicBody && b3Body_GetType(anchor->second.body)!=b3_dynamicBody)) continue;
                auto definition=b3DefaultDistanceJointDef();
                definition.base.bodyIdA=anchor->second.body; definition.base.bodyIdB=target.body;
                definition.base.localFrameA.p=b3Body_GetLocalPoint(definition.base.bodyIdA,vector(anchor->second.points[plane]));
                definition.base.localFrameB.p=b3Body_GetLocalPoint(definition.base.bodyIdB,vector(target.points[plane]));
                definition.base.collideConnected=true;
                definition.enableSpring=true;
                definition.length=permanent ? std::max(minimumLength,glm::distance(anchor->second.points[plane],target.points[plane])) : minimumLength;
                definition.hertz=permanent ? 0 : 2;
                definition.dampingRatio=1;
                definition.lowerSpringForce=permanent ? 0 : -pullForce;
                definition.upperSpringForce=0; // Tension pulls; it never pushes objects apart.
                definition.enableLimit=permanent;
                definition.minLength=minimumLength;
                definition.maxLength=definition.length;
                b3JointId joint{};
                if (!permanent) {
                    const auto existing=std::find_if(output.begin(),output.end(),[&](const Connection& connection) {
                        return connection.plane==plane && B3_ID_EQUALS(connection.a,definition.base.bodyIdA) &&
                            B3_ID_EQUALS(connection.b,definition.base.bodyIdB) && b3Joint_IsValid(connection.joint);
                    });
                    if (existing!=output.end()) {
                        joint=existing->joint; existing->joint=b3_nullJointId;
                        b3Joint_SetLocalFrameA(joint,definition.base.localFrameA);
                        b3Joint_SetLocalFrameB(joint,definition.base.localFrameB);
                        b3Joint_WakeBodies(joint);
                    }
                }
                if (!b3Joint_IsValid(joint)) joint=b3CreateDistanceJoint(world->id(),&definition);
                if (!b3Joint_IsValid(joint)) throw std::runtime_error("Cannot create binding constraint");
                (permanent ? output : next).push_back({definition.base.bodyIdA,definition.base.bodyIdB,joint,plane});
            }
        }
        if (!permanent) { destroy(output); output=std::move(next); }
    }
    void prepare() {
        ties.erase(std::remove_if(ties.begin(),ties.end(),[](const Connection& tie) { return !b3Joint_IsValid(tie.joint); }),ties.end());
        for (auto it=previews.begin();it!=previews.end();) {
            if (!it->second.tool.valid()) { const auto removed=it++; erase(removed); }
            else { refresh(it->second); ++it; }
        }
        redraw(false);
    }
    void redraw(bool queryAgain=true) {
        lines.clear();
        for (auto& [id,preview]:previews) {
            (void)id;
            if (!preview.tool.valid()) continue;
            if (queryAgain) preview.geometry=queryBindingGeometry(world,preview.planes,preview.tool.body());
            for (const auto& segment:preview.geometry.segments) {
                if (lines.size()+2>2048) return;
                lines.push_back(segment.a); lines.push_back(segment.b);
            }
        }
    }
};

BindingPlacement::BindingPlacement(GroundItems& items,std::shared_ptr<PhysicsWorld> world)
    : impl_(std::make_unique<Impl>(items,std::move(world))) {}
BindingPlacement::~BindingPlacement()=default;
bool BindingPlacement::isBinding(const PhysicsProp& prop) const {
    const auto* entry=prop.valid() ? impl_->items.find(prop) : nullptr;
    return entry && entry->item.hasTag("bind") && (!entry->item.inventory().stackable || entry->quantity>0);
}
void BindingPlacement::update(const PhysicsProp& prop,const Camera& camera) {
    if (!isBinding(prop)) return;
    const auto planes=bindingPlanes(prop.center(),camera.direction());
    auto found=impl_->previews.find(key(prop.body()));
    if (found==impl_->previews.end()) found=impl_->previews.emplace(key(prop.body()),Impl::Preview{prop,planes,{},{},{},{}}).first;
    else found->second.planes=planes;
    impl_->refresh(found->second);
    impl_->redraw(false);
}
bool BindingPlacement::finish(const PhysicsProp& prop) {
    if (!prop.valid()) return false;
    const auto found=impl_->previews.find(key(prop.body()));
    if (found==impl_->previews.end()) return false;
    impl_->refresh(found->second);
    const auto before=impl_->ties.size();
    impl_->connect(found->second,true);
    impl_->erase(found);
    impl_->redraw(false);
    return impl_->ties.size()>before;
}
void BindingPlacement::cancel(const PhysicsProp& prop) {
    if (!prop.valid()) return;
    const auto found=impl_->previews.find(key(prop.body()));
    if (found!=impl_->previews.end()) impl_->erase(found);
    impl_->redraw(false);
}
const std::vector<glm::vec3>& BindingPlacement::lines() const { return impl_->lines; }
std::size_t BindingPlacement::jointCount() const {
    return static_cast<std::size_t>(std::count_if(impl_->ties.begin(),impl_->ties.end(),[](const Connection& tie) {
        return b3Joint_IsValid(tie.joint);
    }));
}
}
