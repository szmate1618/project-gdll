#include "canopy.hpp"
#include "physics_scene.hpp"
#include <box3d/box3d.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <stdexcept>

namespace viewer {
namespace {
b3Vec3 vector(glm::vec3 v) { return {v.x,v.y,v.z}; }
glm::vec3 vector(b3Vec3 v) { return {v.x,v.y,v.z}; }
b3Quat rotation(glm::quat q) { return {{q.x,q.y,q.z},q.w}; }
int slot(int panel) { return panel < Canopy::rootPanel ? panel : panel-1; }
}
glm::vec3 Canopy::panelOffset(int index) {
    return {(float(index%side)-2.5f)*panelSize,0,(float(index/side)-2.5f)*panelSize};
}
BoxSpawnMotion Canopy::motion(std::shared_ptr<PhysicsWorld> world, bool replica) {
    BoxSpawnMotion result;
    result.mass = panelMass; result.continuous = true;
    result.friction = 1.2f; result.linearDamping = .15f; result.angularDamping = .7f;
    // Adjacent plates share an exact edge and may fold through each other at
    // that edge. Suppress sheet self-contact; scene/prop contact remains live.
    result.filter = {interiorBoxCategory | canopyBodyCategory,
                     ~(ragdollFloorCategory | playerBodyCategory), replica ? 0 : world->allocateCollisionGroup()};
    result.acoustics = "canopy";
    return result;
}
std::vector<std::pair<int,int>> Canopy::edges() {
    std::vector<std::pair<int,int>> result;
    for (int z=0; z<side; ++z) for (int x=0; x<side; ++x) {
        const int a=z*side+x;
        if (x+1<side) result.emplace_back(a,a+1);
        if (z+1<side) result.emplace_back(a,a+side);
    }
    return result;
}
Canopy::Canopy(PhysicsProp root, bool replica) : root_(std::move(root)), panels_(root_.world_) {
    if (!root_.valid()) throw std::invalid_argument("Canopy requires its owning panel");
    const auto rootBody = root_.body();
    const auto raw = b3Body_GetRotation(rootBody);
    const glm::quat q(raw.s,raw.v.x,raw.v.y,raw.v.z);
    const auto center = root_.center()-q*panelOffset(rootPanel);
    auto spawn=motion(root_.world_,true);
    b3ShapeId rootShape{};
    b3Body_GetShapes(rootBody,&rootShape,1);
    // All 36 plates use the same collision group, including the borrowed one.
    spawn.filter = b3Shape_GetFilter(rootShape);
    spawn.rotation=q;
    spawn.velocity=vector(b3Body_GetLinearVelocity(rootBody));
    spawn.angularVelocity=vector(b3Body_GetAngularVelocity(rootBody));
    for (int p=0; p<panelCount; ++p) if (p!=rootPanel) {
        spawn.velocity=root_.pointState(panelOffset(p)-panelOffset(rootPanel)).velocity;
        panels_.add(center+q*panelOffset(p),{panelSize,thickness,panelSize},spawn);
    }
    if (replica) {
        for (int p=0; p<panelCount; ++p) b3Body_SetType(prop(p).body(),b3_kinematicBody);
        return;
    }
    joints_.reserve(60);
    for (const auto& [a,b] : edges()) {
        const auto bodyA=prop(a).body(), bodyB=prop(b).body();
        const auto midpoint=(panelOffset(a)+panelOffset(b))*.5f;
        const auto edgeAxis=b-a==1 ? glm::vec3(0,0,1) : glm::vec3(1,0,0);
        const auto frame=edgeAxis.z==1 ? glm::quat(1,0,0,0) : glm::angleAxis(glm::radians(90.f),glm::vec3(0,1,0));
        auto joint=b3DefaultRevoluteJointDef();
        joint.base.bodyIdA=bodyA; joint.base.bodyIdB=bodyB;
        joint.base.localFrameA.p=vector(midpoint-panelOffset(a));
        joint.base.localFrameB.p=vector(midpoint-panelOffset(b));
        joint.base.localFrameA.q=joint.base.localFrameB.q=rotation(frame);
        joint.base.collideConnected=false;
        joint.enableSpring=true; joint.targetAngle=0;
        joint.hertz=1.5f; joint.dampingRatio=.9f;
        joint.enableLimit=true; joint.lowerAngle=-2.9f; joint.upperAngle=2.9f;
        joints_.push_back(b3CreateRevoluteJoint(root_.world_->id(),&joint));
    }
}
Canopy::~Canopy() {
    for (const auto joint : joints_) if (b3Joint_IsValid(joint)) b3DestroyJoint(joint,true);
}
PhysicsProp Canopy::prop(int panel) const {
    if (panel<0 || panel>=panelCount) throw std::out_of_range("Invalid canopy panel");
    return panel==rootPanel ? root_ : panels_.prop(slot(panel));
}
std::vector<BoxPose> Canopy::poses() const {
    std::vector<BoxPose> result; result.reserve(panelCount);
    for (int p=0; p<panelCount; ++p) {
        const auto body=prop(p).body();
        const auto raw=b3Body_GetRotation(body);
        result.push_back({std::size_t(p),vector(b3Body_GetPosition(body)),{panelSize*.5f,thickness*.5f,panelSize*.5f},
                          {raw.s,raw.v.x,raw.v.y,raw.v.z}});
    }
    return result;
}
bool Canopy::owns(const PhysicsProp& body) const { return root_==body || panels_.indexOf(body).has_value(); }
bool Canopy::grabbed() const {
    for (int p=0; p<panelCount; ++p) if (prop(p).grabbed()) return true;
    return false;
}
void Canopy::prepare(PhysicsScene& scene) {
    scene.prepare(panels_);
    // GroundItems brakes ordinary supported equippables. Cloth must keep
    // bending and sliding while one of its plates touches a support.
    b3Body_SetLinearDamping(root_.body(),.15f);
    b3Body_SetAngularDamping(root_.body(),.7f);
}
void Canopy::synchronize(const std::vector<BoxPose>& poses) {
    if (poses.size()!=panelCount) throw std::invalid_argument("Canopy snapshot needs 36 panels");
    for (int p=0; p<panelCount; ++p) {
        const auto& pose=poses[p];
        if (p!=rootPanel) panels_.setPose(slot(p),pose.center,pose.rotation);
    }
}
}
