#include "zombie_navigation.hpp"
#include "collision_world.hpp"
#include "file_fingerprint.hpp"
#include "zombie_navmesh_paths.hpp"
#include "zombie_path_follow.hpp"
#include "zombie_steering.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace viewer {
namespace {
glm::vec2 horizontal(glm::vec3 p) { return {p.x,p.z}; }
navmesh_path::Point point(glm::vec2 p) { return {p.x,p.y}; }
bool finite(glm::vec2 p) { return std::isfinite(p.x)&&std::isfinite(p.y); }
bool finite(glm::vec3 p) { return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z); }
std::uint32_t slots(std::size_t population) {
    if (population>std::numeric_limits<std::uint32_t>::max()/4)
        throw std::invalid_argument("Zombie navigation population exceeds path metadata capacity");
    return static_cast<std::uint32_t>(std::max<std::size_t>(1,population));
}
void validateMesh(const navmesh_path::Mesh& mesh) {
    if (mesh.polygons().empty() || mesh.radius()<.3 || mesh.clearance()<.002 ||
        mesh.buildingExtra()<.05 || mesh.numericMargin()<0)
        throw std::invalid_argument("Zombie navmesh must cover radius0.30, skin0.002 and building extra0.05m");
    for (const auto& polygon:mesh.polygons())
        if (polygon.edgeCount>4) throw std::invalid_argument("Zombie navigation requires at most four vertices per region");
}
}

struct ZombieNavigation::Impl {
    struct State {
        steering::FollowState follow;
        glm::vec2 velocity{0}, plannedGoal{0}, anchor{0}, lastPosition{0};
        float sincePlan=0, stuck=0;
        bool attempted=false, hasPlan=false, recorded=false;
    };
    navmesh_path::Mesh mesh;
    navmesh_path::PathManager paths;
    steering::Controller controller;
    std::vector<State> states;
    std::vector<NearbyTrunk> trunks;
    std::vector<steering::Obstacle> obstacles;
    ZombieNavigationStats stats;
    navmesh_path::Status status=navmesh_path::Status::noPath;
    std::string meshHash;

    Impl(navmesh_path::Mesh source,std::size_t population,std::size_t trunkCapacity)
        :mesh(std::move(source)),paths(mesh,slots(population),50'000,8.0),
         controller({},trunkCapacity),states(population),trunks(trunkCapacity),obstacles(trunkCapacity) {
        validateMesh(mesh);
        if (!trunkCapacity) throw std::invalid_argument("Zombie navigation needs a positive trunk capacity");
    }
    void release(std::size_t id) {
        paths.releaseZombie(id);
        states[id].hasPlan=false;
        states[id].follow={};
        states[id].velocity={0,0};
    }
};

ZombieNavigation::ZombieNavigation(navmesh_path::Mesh mesh,std::size_t population,std::size_t trunkCapacity)
    :impl_(std::make_unique<Impl>(std::move(mesh),population,trunkCapacity)) {}
ZombieNavigation::ZombieNavigation(const std::filesystem::path& path,std::size_t population)
    :ZombieNavigation(navmesh_path::Mesh::load(path.string()),population) {
    impl_->meshHash=fileSha256(path);
}
ZombieNavigation::~ZombieNavigation()=default;

glm::vec2 ZombieNavigation::desiredVelocity(std::size_t id,glm::vec3 feet,glm::vec3 target,
                                           float speed,float dt,const CollisionWorld& world) {
    auto& p=*impl_;
    if (id>=p.states.size() || !finite(feet) || !finite(target) || !std::isfinite(speed) ||
        speed<=0 || !std::isfinite(dt) || dt<=0) return {0,0};
    auto& s=p.states[id];
    const auto position=horizontal(feet),goal=horizontal(target);
    // External placement/teleport invalidates both route progress and sampled
    // velocity. Ordinary accepted movement is recorded after the physical guard.
    if (s.recorded && glm::length(position-s.lastPosition)>.5f) {
        reset(id);
    }
    s.sincePlan+=dt;
    const auto startPolygon=p.mesh.locate(point(position)),goalPolygon=p.mesh.locate(point(goal));
    if (startPolygon==navmesh_path::noPolygon || goalPolygon==navmesh_path::noPolygon ||
        p.mesh.component(startPolygon)!=p.mesh.component(goalPolygon)) {
        p.release(id);
        p.status=startPolygon==navmesh_path::noPolygon || goalPolygon==navmesh_path::noPolygon
            ?navmesh_path::Status::blockedEndpoint:navmesh_path::Status::noPath;
        ++p.stats.blockedUpdates;
        return {0,0};
    }
    const bool changed=glm::length(goal-s.plannedGoal)>=1.f;
    if (!s.attempted || (s.hasPlan && ((changed && s.sincePlan>=.5f) || s.stuck>=1.f)) ||
        (!s.hasPlan && s.sincePlan>=1.f)) {
        ++p.stats.requests;
        const auto result=p.paths.request(id,point(position),point(goal));
        p.status=p.paths.lastStatus();
        s.attempted=true;s.sincePlan=0;s.stuck=0;s.plannedGoal=goal;s.anchor=position;s.follow={};
        s.hasPlan=result!=navmesh_path::invalidPath;
        if (s.hasPlan) ++p.stats.foundPaths;
        else { ++p.stats.failedPaths;p.release(id); }
    }
    if (!s.hasPlan) return {0,0};
    const auto path=p.paths.view(p.paths.pathFor(id));
    auto desired=steering::followPath(path.points,path.size,position,speed,dt,s.follow,&p.mesh);
    // A final step toward a double portal corner can round a float body outside
    // the mesh, or stop a few ulps short while physical motion ignores sub-10um
    // steps. Extend that step slightly along its approach only when both the
    // original corner, extension and following leg are covered. Never relax coverage.
    const auto corner=s.follow.segment+1;
    const float precision=4*std::numeric_limits<float>::epsilon()*
                          std::max({1.f,std::abs(position.x),std::abs(position.y)});
    const float minimumStep=std::max(2e-5f,precision);
    if (!s.follow.arrived && corner+1<path.size) {
        const glm::vec2 toCorner{float(path[corner].x-position.x),float(path[corner].z-position.y)};
        const float gap=glm::length(toCorner);
        const bool reachesCorner=gap<=speed*dt+minimumStep;
        const bool tiny=reachesCorner && glm::length(desired*dt)<=minimumStep;
        const bool roundedOutside=reachesCorner && !tiny &&
            !p.mesh.segmentClear(point(position),point(position+desired*dt));
        if ((tiny || roundedOutside) &&
            p.mesh.segmentClear(point(position),path[corner])) {
            const glm::vec2 incoming{float(path[corner].x-path[corner-1].x),
                                     float(path[corner].z-path[corner-1].z)};
            for (const auto direction:{toCorner,incoming}) {
                const float length=glm::length(direction);
                if (length<=0) continue;
                const auto step=direction*(std::min(speed*dt,gap+minimumStep)/length);
                if (p.mesh.segmentClear(point(position),point(position+step)) &&
                    p.mesh.segmentClear(point(position+step),path[corner+1])) {
                    desired=step/dt;
                    break;
                }
            }
        }
    }
    // The shared follower uses a0.15m waypoint arrival radius. Gameplay still
    // finishes at its actual target; a validated final connection handles that
    // gap and small goal movement between scheduled replans.
    const float remaining=glm::length(goal-position);
    if (s.follow.arrived && remaining>1e-4f) {
        if (!p.mesh.segmentClear(point(position),point(goal))) {
            p.release(id);s.sincePlan=1;return {0,0};
        }
        desired=(goal-position)*(std::min(speed,remaining/dt)/remaining);
    }
    if (!finite(desired) || glm::dot(desired,desired)<1e-12f) return {0,0};
    bool overflow=false;
    const float range=std::max(speed,glm::length(s.velocity))*p.controller.config().horizon+
                      p.controller.config().radius+p.controller.config().skin;
    const auto count=world.nearbyTrunks(feet,1.8f,range,p.trunks.data(),p.trunks.size(),overflow);
    if (overflow) { ++p.stats.trunkOverflows;s.velocity={0,0};return {0,0}; }
    for (std::size_t i=0;i<count;++i) {
        const auto& trunk=p.trunks[i].collider;
        p.obstacles[i]={{trunk.base.x,trunk.base.z},trunk.radius};
    }
    steering::Stats sample{};
    auto velocity=p.controller.steer(position,s.velocity,desired,p.obstacles.data(),count,&sample);
    ++p.stats.steeringCalls;p.stats.obstacleTests+=sample.obstacleTests;p.stats.velocitySamples+=sample.velocitySamples;
    if (!p.mesh.segmentClear(point(position),point(position+velocity*dt))) {
        ++p.stats.meshRejectedCandidates;
        velocity=p.mesh.segmentClear(point(position),point(position+desired*dt))?desired:glm::vec2(0);
    }
    return velocity;
}

void ZombieNavigation::recordMovement(std::size_t id,glm::vec3 before,glm::vec3 after,float dt) {
    auto& p=*impl_;
    if (id>=p.states.size() || !finite(before) || !finite(after) || !std::isfinite(dt) || dt<=0) return;
    auto& s=p.states[id];
    s.velocity=(horizontal(after)-horizontal(before))/dt;
    if (!finite(s.velocity)) s.velocity={0,0};
    s.lastPosition=horizontal(after);s.recorded=true;
    s.stuck+=dt;
    if (glm::length(s.lastPosition-s.anchor)>.05f) {s.anchor=s.lastPosition;s.stuck=0;}
}
void ZombieNavigation::reset(std::size_t id) {
    if (id>=impl_->states.size()) return;
    impl_->paths.releaseZombie(id);
    impl_->states[id]={};
}
void ZombieNavigation::resetPopulation(std::size_t population) {
    // Build before publication so a failed allocation leaves the live module
    // valid. Bulk population replacement is already a cold rebuilding operation.
    auto replacement=std::make_unique<Impl>(impl_->mesh,population,impl_->trunks.size());
    replacement->meshHash=impl_->meshHash;
    replacement->stats=impl_->stats;
    impl_=std::move(replacement);
}
bool ZombieNavigation::segmentClear(glm::vec3 a,glm::vec3 b) const {
    return finite(a)&&finite(b)&&impl_->mesh.segmentClear(point(horizontal(a)),point(horizontal(b)));
}
const ZombieNavigationStats& ZombieNavigation::stats() const { return impl_->stats; }
navmesh_path::Status ZombieNavigation::lastStatus() const { return impl_->status; }
const navmesh_path::Mesh& ZombieNavigation::mesh() const { return impl_->mesh; }
std::uint64_t ZombieNavigation::pathFor(std::size_t id) const { return impl_->paths.pathFor(id); }
std::size_t ZombieNavigation::pathArenaBytes() const { return impl_->paths.capacityBytes(); }
std::size_t ZombieNavigation::pathUsedBytes() const { return impl_->paths.usedBytes(); }
std::size_t ZombieNavigation::capacity() const { return impl_->states.size(); }
const std::string& ZombieNavigation::meshSha256() const { return impl_->meshHash; }
const std::string& ZombieNavigation::sourceSha256() const { return impl_->mesh.sourceSha256(); }
}
