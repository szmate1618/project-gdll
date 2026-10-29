#include "zombie_cavity_physics.hpp"
#include "box_layer.hpp"
#include "physics_world.hpp"
#include "physics_scene.hpp"
#include "zombie_wound_surface.hpp"
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
Shell makeShell(const AnimatedModel& asset, const ZombieWoundGeometry& geometry, const ZombieWound& wound) {
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
            if (!geometry.regionTriangles[source]) continue;
            std::array<int,3> ids{};
            for (int i=0; i<3; ++i) ids[i] = vertex(geometry.referenceVertices[p][primitive.indices[t+i]]);
            if (ids[0]==ids[1] || ids[1]==ids[2] || ids[2]==ids[0]) continue;
            if (!wound.removedTriangles[source])
                shell.indices.insert(shell.indices.end(), {ids[0],ids[2],ids[1]}); // Inward-facing contacts.
        }
    }
    // The camera and cavity use the same closures, prepared before damage.
    for (const auto& triangle : geometry.closures) {
        if (zombieWoundClosureRemoved(triangle,wound)) continue;
        std::array<int,3> ids{};
        for (int i=0; i<3; ++i)
            ids[i] = vertex(geometry.referenceVertices[triangle[i].primitive][triangle[i].vertex]);
        shell.indices.insert(shell.indices.end(), {ids[0],ids[2],ids[1]});
    }
    return shell;
}
}
struct ZombieCavityPhysics::Impl {
    struct Entry {
        explicit Entry(std::shared_ptr<PhysicsWorld> world) : objects(std::move(world)) {}
        b3BodyId cavity{};
        std::vector<b3ShapeId> shapes;
        struct DeleteMesh { void operator()(b3MeshData* mesh) const { b3DestroyMesh(mesh); } };
        std::vector<std::unique_ptr<b3MeshData, DeleteMesh>> meshes;
        int ownerGroup = 0;
        BoxLayer objects;
        glm::vec3 scale{1};
        std::uint64_t revision = 0;
        ~Entry() {
            if (b3Body_IsValid(cavity)) b3DestroyBody(cavity);
        }
    };
    std::shared_ptr<PhysicsWorld> world;
    std::unordered_map<std::size_t,std::array<std::unique_ptr<Entry>,2>> entries;
};
ZombieCavityPhysics::ZombieCavityPhysics(std::shared_ptr<PhysicsWorld> world) : impl_(std::make_unique<Impl>()) {
    impl_->world=std::move(world);
}
ZombieCavityPhysics::~ZombieCavityPhysics() = default;
void ZombieCavityPhysics::wound(std::size_t id, const AnimatedModel& asset, const ZombieWoundGeometry& geometry,
                               const ZombieWound& wound, const glm::mat4& frame, glm::vec3 velocity) {
    auto& owned=impl_->entries[id][std::size_t(geometry.region)];
    const bool fresh=!owned;
    if (fresh) owned=std::make_unique<Impl::Entry>(impl_->world);
    auto& entry=*owned;
    if (entry.revision==wound.revision) return;
    auto shell=makeShell(asset,geometry,wound);
    // Box3D caps a contact's triangle cache at 256. Keep each shape below
    // that limit, even when its query padding covers the complete small cavity.
    std::vector<std::unique_ptr<b3MeshData, Impl::Entry::DeleteMesh>> meshes;
    constexpr std::size_t trianglesPerShape = 192;
    for (std::size_t begin = 0; begin < shell.indices.size(); begin += trianglesPerShape*3) {
        b3MeshDef def{};
        def.vertices=shell.vertices.data(); def.vertexCount=int(shell.vertices.size());
        def.indices=shell.indices.data()+begin;
        def.triangleCount=int(std::min(trianglesPerShape*3,shell.indices.size()-begin)/3);
        def.weldVertices=true; def.weldTolerance=.00001f; def.identifyEdges=true;
        auto* mesh=b3CreateMesh(&def,nullptr,0);
        if (!mesh) throw std::runtime_error("Cannot create hollow zombie cavity collider");
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
        BoxSpawnMotion motion;
        motion.velocity=velocity; motion.continuous=true;
        motion.mass=700*contentsSize.x*contentsSize.y*contentsSize.z;
        motion.friction=.6f; motion.linearDamping=.2f; motion.angularDamping=.4f;
        motion.filter={zombieContentsCategory,
            ordinaryBodyCategory|interiorBoxCategory|doorBodyCategory|zombieCavityCategory|zombieContentsCategory,entry.ownerGroup};
        const bool head = geometry.region == ZombieWoundRegion::head;
        for (std::size_t i=0; i<(head ? headObjectCount : torsoObjectCount); ++i) {
            // Two columns and three rows leave gaps between the larger contents.
            const auto local=localCenter+(head ? glm::vec3(0) : glm::vec3((float(i%2)-.5f)*.09f,(float(i/2)-1)*.10f,0));
            const auto center=frame*glm::vec4(local,1);
            entry.objects.add(glm::vec3(center),contentsSize,motion);
        }
    }
    entry.objects.wake();
    entry.revision=wound.revision;
}
void ZombieCavityPhysics::move(std::size_t id, ZombieWoundRegion region, const glm::mat4& frame, float seconds) {
    auto& entry=*impl_->entries.at(id)[std::size_t(region)];
    if (seconds>0) b3Body_SetTargetTransform(entry.cavity,transform(frame),seconds,true);
}
bool ZombieCavityPhysics::has(std::size_t id, ZombieWoundRegion region) const {
    const auto found = impl_->entries.find(id);
    return found != impl_->entries.end() && bool(found->second[std::size_t(region)]);
}
void ZombieCavityPhysics::prepare(PhysicsScene* scene) {
    if (!scene) return;
    for (const auto& item : impl_->entries)
        for (const auto& entry : item.second) if (entry) scene->prepare(entry->objects,1);
}
void ZombieCavityPhysics::removeContentsBelow(float cutoffY) {
    for (const auto& item : impl_->entries)
        for (const auto& entry : item.second) if (entry)
            for (std::size_t i = 0; i < entry->objects.count();) {
                if (entry->objects.belowHeight(i, cutoffY)) entry->objects.remove(i);
                else ++i;
            }
}
void ZombieCavityPhysics::remove(std::size_t id) { impl_->entries.erase(id); }
std::vector<std::pair<std::size_t,ZombieWoundRegion>> ZombieCavityPhysics::ids() const {
    std::vector<std::pair<std::size_t,ZombieWoundRegion>> result;
    for (const auto& item : impl_->entries)
        for (std::size_t region=0; region<item.second.size(); ++region)
            if (item.second[region]) result.emplace_back(item.first,ZombieWoundRegion(region));
    return result;
}
std::vector<glm::mat4> ZombieCavityPhysics::contents() const {
    std::vector<glm::mat4> result;
    for (const auto& item : impl_->entries)
        for (const auto& entry : item.second) if (entry)
            for (std::size_t i=0; i<entry->objects.count(); ++i)
                result.push_back(entry->objects.pose(i).transform());
    return result;
}

std::vector<glm::mat4> ZombieCavityPhysics::contentsForIds(const std::vector<std::size_t>& ids,
        std::size_t maxCount) const {
    std::vector<glm::mat4> result;
    if (!maxCount) return result;
    result.reserve(maxCount);
    for (const auto id : ids) {
        const auto found = impl_->entries.find(id);
        if (found == impl_->entries.end()) continue;
        for (const auto& entry : found->second) if (entry)
            for (std::size_t i = 0; i < entry->objects.count(); ++i) {
                result.push_back(entry->objects.pose(i).transform());
                if (result.size() == maxCount) return result;
            }
    }
    return result;
}
} // namespace viewer
