#include "doodad_layer.hpp"
#include "doodad_state.hpp"
#include "box_layer.hpp"
#include "door_layer.hpp"
#include "physics_scene.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>

namespace viewer {
namespace {
constexpr float loadRadius = 48, unloadRadius = 64;
constexpr float bodyRadius = 3, retireBodyRadius = 6;
bool finite(glm::vec3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
float horizontalDistance2(glm::vec3 a, glm::vec3 b) {
    const auto delta = glm::vec2(a.x-b.x,a.z-b.z);
    return glm::dot(delta,delta);
}
float cellDistance2(DoodadCell cell, glm::vec3 p) {
    const glm::vec2 low(cell.x*doodadCellSize,cell.z*doodadCellSize);
    const auto delta = glm::vec2(p.x,p.z)-glm::clamp(glm::vec2(p.x,p.z),low,low+glm::vec2(doodadCellSize));
    return glm::dot(delta,delta);
}
bool moved(const DoodadPlacement& a, const DoodadPlacement& b, float positionTolerance, float rotationTolerance) {
    return glm::distance(a.position,b.position)>positionTolerance ||
        std::abs(glm::dot(a.rotation,b.rotation))<1-rotationTolerance;
}
}

struct DoodadLayer::Impl {
    struct Entry {
        DoodadPlacement pose, saved;
        std::optional<std::size_t> body;
    };
    const DoodadPlacementGenerator& generator;
    DoodadState& state;
    BoxLayer bodies;
    std::array<glm::vec3,doodadKindCount> centers, sizes;
    std::set<DoodadCell> cells;
    std::map<std::uint64_t,Entry> entries;
    std::vector<std::uint64_t> bodyIds;
    std::vector<DoodadRenderInstance> instances;
    std::uint64_t revision = 1, solvedRevision = 0;
    bool dirty = true;

    Impl(std::shared_ptr<PhysicsWorld> world, const DoodadPlacementGenerator& placements,
         DoodadState& changes, const std::array<Model,doodadKindCount>& models)
        : generator(placements), state(changes), bodies(std::move(world)) {
        for (std::size_t i=0; i<models.size(); ++i) {
            if (models[i].primitives.empty() || !finite(models[i].boundsMin) || !finite(models[i].boundsMax) ||
                glm::any(glm::lessThanEqual(models[i].boundsMax,models[i].boundsMin)))
                throw std::invalid_argument("Doodads require finite, nonempty model bounds");
            centers[i]=(models[i].boundsMin+models[i].boundsMax)*.5f;
            sizes[i]=models[i].boundsMax-models[i].boundsMin;
        }
    }
    void sync() {
        if (solvedRevision==bodies.revision()) return;
        for (std::size_t index=0; index<bodyIds.size(); ++index) {
            auto& entry=entries.at(bodyIds[index]);
            const auto body=bodies.pose(index);
            auto pose=entry.pose;
            pose.rotation=glm::normalize(body.rotation);
            pose.position=body.center-pose.rotation*(centers[static_cast<std::size_t>(pose.kind)]*pose.scale);
            if (moved(pose,entry.pose,1e-6f,1e-8f)) { entry.pose=pose; dirty=true; }
            // Compare against the last saved pose, so slow cumulative movement
            // cannot disappear underneath a per-frame epsilon.
            if (moved(pose,entry.saved,.002f,3e-6f)) {
                state.set(pose);
                entry.saved=pose;
            }
        }
        solvedRevision=bodies.revision();
    }
    void insert(const DoodadPlacement& pose) {
        if (entries.emplace(pose.id,Entry{pose,pose,{}}).second) dirty=true;
    }
    void loadCell(DoodadCell cell) {
        if (cells.count(cell)) return;
        if (generator.intersectsForest(cell))
            for (const auto& pose:generator.generate(cell)) if (!state.contains(pose.id)) insert(pose);
        // A moved branch can be put on a road, indoors or outside any woodland.
        for (const auto& pose:state.inCell(cell)) insert(pose);
        cells.insert(cell);
    }
    void promote(Entry& entry) {
        const auto kind=static_cast<std::size_t>(entry.pose.kind);
        const auto scale=entry.pose.scale;
        BoxSpawnMotion motion;
        motion.rotation=entry.pose.rotation;
        constexpr std::array<float,doodadKindCount> masses{.25f,1.f,.35f};
        motion.mass=masses[kind]*scale*scale*scale;
        motion.friction=.8f;
        motion.continuous=true;
        motion.linearDamping=.15f;
        motion.angularDamping=.4f;
        const auto center=entry.pose.position+motion.rotation*(centers[kind]*scale);
        const auto index=bodies.add(center,glm::max(sizes[kind]*scale,glm::vec3(.01f)),motion);
        try { bodyIds.push_back(entry.pose.id); }
        catch (...) { bodies.remove(index); throw; }
        entry.body=index;
    }
    void retire(std::size_t index) {
        entries.at(bodyIds[index]).body.reset();
        bodies.remove(index);
        if (index+1<bodyIds.size()) {
            bodyIds[index]=bodyIds.back();
            entries.at(bodyIds[index]).body=index;
        }
        bodyIds.pop_back();
    }
    void rebuild() {
        if (!dirty) return;
        instances.clear();
        instances.reserve(entries.size());
        for (const auto& item:entries) {
            const auto& pose=item.second.pose;
            const auto kind=static_cast<std::size_t>(pose.kind);
            const auto transform=glm::translate(glm::mat4(1),pose.position)*glm::mat4_cast(pose.rotation)*
                glm::scale(glm::mat4(1),glm::vec3(pose.scale));
            const auto center=glm::vec3(transform*glm::vec4(centers[kind],1));
            const auto half=sizes[kind]*pose.scale*.5f;
            const auto rotation=glm::mat3_cast(pose.rotation);
            const auto extent=glm::abs(rotation[0])*half.x+glm::abs(rotation[1])*half.y+glm::abs(rotation[2])*half.z;
            instances.push_back({pose.id,pose.kind,transform,center-extent,center+extent});
        }
        dirty=false;
        ++revision;
    }
};

DoodadLayer::DoodadLayer(std::shared_ptr<PhysicsWorld> world,
        const DoodadPlacementGenerator& generator, DoodadState& state,
        const std::array<Model,doodadKindCount>& models)
    : impl_(std::make_unique<Impl>(std::move(world),generator,state,models)) {}
DoodadLayer::~DoodadLayer() = default;
void DoodadLayer::update(glm::vec3 player, PhysicsScene& scene, DoorLayer& doors) {
    if (!finite(player)) throw std::invalid_argument("Invalid doodad observer position");
    auto& data=*impl_;
    data.sync();
    for (auto it=data.cells.begin(); it!=data.cells.end();) {
        if (cellDistance2(*it,player)>unloadRadius*unloadRadius) it=data.cells.erase(it);
        else ++it;
    }
    const auto first=doodadCellAt(player-glm::vec3(loadRadius,0,loadRadius));
    const auto last=doodadCellAt(player+glm::vec3(loadRadius,0,loadRadius));
    for (int x=first.x; x<=last.x; ++x) for (int z=first.z; z<=last.z; ++z) {
        const DoodadCell cell{x,z};
        if (cellDistance2(cell,player)<=loadRadius*loadRadius) data.loadCell(cell);
    }
    for (auto it=data.entries.begin(); it!=data.entries.end();) {
        auto& entry=it->second;
        const auto distance=horizontalDistance2(entry.pose.position,player);
        if (entry.body) {
            const auto prop=data.bodies.prop(*entry.body);
            // Retaining the actual body preserves both hands and in-flight
            // motion even after its origin or destination cell is unloaded.
            if (distance>retireBodyRadius*retireBodyRadius && !prop.grabbed() && !prop.awake())
                data.retire(*entry.body);
        } else if (distance<=bodyRadius*bodyRadius) data.promote(entry);
        if (!entry.body && !data.cells.count(doodadCellAt(entry.pose.position))) {
            it=data.entries.erase(it);
            data.dirty=true;
        } else ++it;
    }
    scene.prepare(data.bodies);
    data.bodies.dampOnGroundContact(12,14);
    for (std::size_t i=0; i<data.bodies.count(); ++i) doors.activateNear(data.bodies.pose(i).center,8);
    data.rebuild();
}
void DoodadLayer::captureChanges() { impl_->sync(); impl_->rebuild(); }
const std::vector<DoodadRenderInstance>& DoodadLayer::instances() const { return impl_->instances; }
std::uint64_t DoodadLayer::revision() const { return impl_->revision; }
std::size_t DoodadLayer::cellCount() const { return impl_->cells.size(); }
std::size_t DoodadLayer::activeBodyCount() const { return impl_->bodies.count(); }
} // namespace viewer
