#include "zombie_torso_physics.hpp"
#include "physics_world.hpp"
#include "physics_scene.hpp"
#include "zombie_torso_surface.hpp"
#include <box3d/box3d.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <map>
#include <algorithm>
#include <cmath>
#include <array>
#include <limits>
#include <stdexcept>

namespace viewer {
namespace {
const glm::vec3 contentsSize{.075f,.09f,.075f}; // Full dimensions in metres.
b3Vec3 vec(glm::vec3 p) { return {p.x,p.y,p.z}; }
glm::vec3 vec(b3Pos p) { return {float(p.x),float(p.y),float(p.z)}; }
b3WorldTransform transform(const glm::mat4& matrix) {
    auto x = glm::normalize(glm::vec3(matrix[0]));
    auto y = glm::normalize(glm::vec3(matrix[1])-x*glm::dot(x,glm::vec3(matrix[1])));
    auto q = glm::normalize(glm::quat_cast(glm::mat3(x,y,glm::cross(x,y))));
    return {{matrix[3].x,matrix[3].y,matrix[3].z}, {{q.x,q.y,q.z},q.w}};
}
struct Shell {
    std::vector<b3Vec3> vertices;
    std::vector<int32_t> indices;
};
Shell makeShell(const AnimatedModel& asset, const ZombieTorsoGeometry& geometry, const ZombieTorsoWound& wound) {
    Shell shell;
    std::map<std::array<long,3>,int> welded;
    const auto inverse = glm::inverse(geometry.referenceFrame);
    const auto vertex = [&](glm::vec3 point) {
        const auto local = glm::vec3(inverse * glm::vec4(geometry.center+(point-geometry.center)*.8f,1));
        const std::array<long,3> key{std::lround(local.x*100000),std::lround(local.y*100000),std::lround(local.z*100000)};
        const auto inserted = welded.emplace(key, int(shell.vertices.size()));
        if (inserted.second) shell.vertices.push_back(vec(local));
        return inserted.first->second;
    };
    for (std::size_t p=0; p<asset.model.primitives.size(); ++p) {
        const auto& primitive = asset.model.primitives[p];
        for (std::size_t t=0; t<primitive.indices.size(); t+=3) {
            const auto source = geometry.triangleOffsets[p]+t/3;
            if (!geometry.torsoTriangles[source]) continue;
            std::array<int,3> ids{};
            for (int i=0; i<3; ++i) ids[i] = vertex(geometry.referenceVertices[p][primitive.indices[t+i]]);
            if (ids[0]==ids[1] || ids[1]==ids[2] || ids[2]==ids[0]) continue;
            if (!wound.removedTriangles[source])
                shell.indices.insert(shell.indices.end(), {ids[0],ids[2],ids[1]}); // Inward-facing contacts.
        }
    }
    // The camera and cavity use the same closures, prepared before damage.
    for (const auto& triangle : geometry.closures) {
        if (zombieTorsoClosureRemoved(triangle,wound)) continue;
        std::array<int,3> ids{};
        for (int i=0; i<3; ++i)
            ids[i] = vertex(geometry.referenceVertices[triangle[i].primitive][triangle[i].vertex]);
        shell.indices.insert(shell.indices.end(), {ids[0],ids[2],ids[1]});
    }
    return shell;
}
}
struct ZombieTorsoPhysics::Impl {
    struct Entry {
        b3BodyId cavity{};
        std::vector<b3ShapeId> shapes;
        struct DeleteMesh { void operator()(b3MeshData* mesh) const { b3DestroyMesh(mesh); } };
        std::vector<std::unique_ptr<b3MeshData, DeleteMesh>> meshes;
        int ownerGroup = 0;
        std::array<b3BodyId,objectCount> objects{};
        glm::vec3 scale{1};
        std::uint64_t revision = 0;
        ~Entry() {
            for (auto body : objects) if (b3Body_IsValid(body)) b3DestroyBody(body);
            if (b3Body_IsValid(cavity)) b3DestroyBody(cavity);
        }
    };
    std::shared_ptr<PhysicsWorld> world;
    std::unordered_map<std::size_t,std::unique_ptr<Entry>> entries;
};
ZombieTorsoPhysics::ZombieTorsoPhysics(std::shared_ptr<PhysicsWorld> world) : impl_(std::make_unique<Impl>()) {
    impl_->world=std::move(world);
}
ZombieTorsoPhysics::~ZombieTorsoPhysics() = default;
void ZombieTorsoPhysics::wound(std::size_t id, const AnimatedModel& asset, const ZombieTorsoGeometry& geometry,
                               const ZombieTorsoWound& wound, const glm::mat4& frame, glm::vec3 velocity) {
    auto& owned=impl_->entries[id];
    const bool fresh=!owned;
    if (fresh) owned=std::make_unique<Impl::Entry>();
    auto& entry=*owned;
    if (entry.revision==wound.revision) return;
    auto shell=makeShell(asset,geometry,wound);
    // Box3D caps a contact's triangle cache at 256. Keep each shape below
    // that limit, even when its query padding covers the complete small torso.
    std::vector<std::unique_ptr<b3MeshData, Impl::Entry::DeleteMesh>> meshes;
    constexpr std::size_t trianglesPerShape = 192;
    for (std::size_t begin = 0; begin < shell.indices.size(); begin += trianglesPerShape*3) {
        b3MeshDef def{};
        def.vertices=shell.vertices.data(); def.vertexCount=int(shell.vertices.size());
        def.indices=shell.indices.data()+begin;
        def.triangleCount=int(std::min(trianglesPerShape*3,shell.indices.size()-begin)/3);
        def.weldVertices=true; def.weldTolerance=.00001f; def.identifyEdges=true;
        auto* mesh=b3CreateMesh(&def,nullptr,0);
        if (!mesh) throw std::runtime_error("Cannot create hollow zombie torso collider");
        meshes.emplace_back(mesh);
    }
    if (fresh) {
        const auto target=transform(frame);
        auto body=b3DefaultBodyDef(); body.type=b3_kinematicBody;
        body.position=target.p; body.rotation=target.q;
        entry.cavity=b3CreateBody(impl_->world->id(),&body);
        entry.scale={glm::length(glm::vec3(frame[0])),glm::length(glm::vec3(frame[1])),glm::length(glm::vec3(frame[2]))};
    }
    for (const auto shape : entry.shapes) b3DestroyShape(shape,true);
    entry.shapes.clear();
    entry.meshes = std::move(meshes);
    // Opposite signed groups identify the owning cavity/contents. Positive
    // prop groups retain collisions between objects belonging to this zombie.
    if (fresh) entry.ownerGroup=-impl_->world->allocateCollisionGroup();
    for (const auto& mesh : entry.meshes) {
        auto shape=b3DefaultShapeDef(); shape.density=0;
        shape.filter.categoryBits=zombieCavityCategory; shape.filter.maskBits=zombieContentsCategory;
        shape.filter.groupIndex=-entry.ownerGroup; shape.enableCustomFiltering=true;
        shape.baseMaterial.friction=.5f;
        entry.shapes.push_back(b3CreateMeshShape(entry.cavity,&shape,mesh.get(),vec(entry.scale)));
    }
    if (fresh) {
        const auto localCenter=glm::vec3(glm::inverse(geometry.referenceFrame)*glm::vec4(geometry.center,1));
        for (std::size_t i=0; i<entry.objects.size(); ++i) {
            // Two columns and three rows leave gaps between the larger contents.
            const auto local=localCenter+glm::vec3((float(i%2)-.5f)*.09f,(float(i/2)-1)*.10f,0);
            const auto center=frame*glm::vec4(local,1);
            auto body=b3DefaultBodyDef(); body.type=b3_dynamicBody;
            body.position={center.x,center.y,center.z};
            body.linearVelocity=vec(velocity); body.isBullet=true;
            body.linearDamping=.2f; body.angularDamping=.4f;
            const auto object=b3CreateBody(impl_->world->id(),&body);
            entry.objects[i]=object;
            auto shape=b3DefaultShapeDef(); shape.density=700;
            shape.baseMaterial.friction=.6f; shape.baseMaterial.rollingResistance=.15f;
            shape.filter.categoryBits=zombieContentsCategory;
            shape.filter.maskBits=ordinaryBodyCategory|interiorBoxCategory|doorBodyCategory|zombieCavityCategory|zombieContentsCategory;
            shape.filter.groupIndex=entry.ownerGroup;
            const auto box=b3MakeBoxHull(contentsSize.x*.5f,contentsSize.y*.5f,contentsSize.z*.5f);
            b3CreateHullShape(object,&shape,&box.base);
        }
    }
    for (auto object : entry.objects) b3Body_SetAwake(object,true);
    entry.revision=wound.revision;
}
void ZombieTorsoPhysics::move(std::size_t id, const glm::mat4& frame, float seconds) {
    auto& entry=*impl_->entries.at(id);
    if (seconds>0) b3Body_SetTargetTransform(entry.cavity,transform(frame),seconds,true);
}
void ZombieTorsoPhysics::prepare(PhysicsScene* scene) {
    if (!scene) return;
    for (const auto& item : impl_->entries) for (const auto body : item.second->objects) {
        const auto point=vec(b3Body_GetPosition(body));
        const auto v=b3Body_GetLinearVelocity(body);
        const auto travel=glm::vec3(v.x,v.y,v.z)*.2f;
        scene->ensureNear(point+glm::min(travel,glm::vec3(0))-glm::vec3(1),point+glm::max(travel,glm::vec3(0))+glm::vec3(1));
    }
}
void ZombieTorsoPhysics::remove(std::size_t id) { impl_->entries.erase(id); }
std::vector<std::size_t> ZombieTorsoPhysics::ids() const {
    std::vector<std::size_t> result;
    for (const auto& item : impl_->entries) result.push_back(item.first);
    return result;
}
std::vector<glm::mat4> ZombieTorsoPhysics::contents() const {
    std::vector<glm::mat4> result;
    for (const auto& item : impl_->entries) for (const auto body : item.second->objects) {
        const auto p=b3Body_GetPosition(body); const auto q=b3Body_GetRotation(body);
        result.push_back(glm::translate(glm::mat4(1),vec(p))*glm::mat4_cast(glm::quat(q.s,q.v.x,q.v.y,q.v.z))*
                         glm::scale(glm::mat4(1),contentsSize));
    }
    return result;
}
} // namespace viewer
