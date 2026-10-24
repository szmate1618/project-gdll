#include "zombie_update_profile.hpp"
#include "zombie_navigation.hpp"
#include "collision_world.hpp"
#include "file_fingerprint.hpp"
#include "zombie_navmesh_paths.hpp"
#include "zombie_path_follow.hpp"
#include "zombie_steering.hpp"

#include <algorithm>
#include <array>
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
// Choose a representable point around a portal corner whose complete incoming
// and outgoing connections are covered. Never widen mesh boundary tolerances.
glm::vec2 roundedConnection(const navmesh_path::Mesh& mesh,navmesh_path::Point anchor,
                            navmesh_path::Point following,glm::vec2 position,float speed,float dt) {
    const glm::vec2 target{float(anchor.x),float(anchor.z)};
    const float precision=4*std::numeric_limits<float>::epsilon()*std::max({1.f,std::abs(position.x),std::abs(position.y)});
    if(glm::length(target-position)>std::max(.15f,speed*dt+precision)) return {0,0};
    std::array<float,5> xs{},zs{};xs[2]=target.x;zs[2]=target.y;
    for(int i=1;i>=0;--i) {xs[i]=std::nextafter(xs[i+1],-std::numeric_limits<float>::infinity());zs[i]=std::nextafter(zs[i+1],-std::numeric_limits<float>::infinity());}
    for(int i=3;i<5;++i) {xs[i]=std::nextafter(xs[i-1],std::numeric_limits<float>::infinity());zs[i]=std::nextafter(zs[i-1],std::numeric_limits<float>::infinity());}
    float best=std::numeric_limits<float>::max();glm::vec2 velocity{0};
    for(float x:xs) for(float z:zs) {
        const glm::vec2 candidate{x,z},offset=candidate-position;const float gap=glm::length(offset);
        if(gap<=1e-5f || gap>=best || !mesh.segmentClear(point(position),point(candidate)) || !mesh.segmentClear(point(candidate),following)) continue;
        const auto approach=offset*(std::min(speed,gap/dt)/gap);
        if(!mesh.segmentClear(point(position),point(position+approach*dt))) continue;
        best=gap;velocity=approach;
    }
    return velocity;
}

glm::vec2 recoverPathVelocity(const navmesh_path::Mesh& mesh,navmesh_path::PathView path,
                              std::size_t segment,glm::vec2 position,float speed,float dt) {
    const auto corner=segment+1;
    if(corner>=path.size) return {0,0};
    if(corner+1<path.size) {
        const auto v=roundedConnection(mesh,path[corner],path[corner+1],position,speed,dt);
        if(glm::dot(v,v)>0) return v;
    }
    // Avoidance or float interpolation may leave the previously visible path
    // segment. Reconnect locally to that same stored segment, without a search.
    // Near a corner, returning to the incoming line can alternate between
    // adjacent floats forever. Corner recovery owns that neighborhood.
    if(std::hypot(path[corner].x-position.x,path[corner].z-position.y)<=.15) return {0,0};
    const auto a=path[segment],b=path[corner];
    const double dx=b.x-a.x,dz=b.z-a.z,length=dx*dx+dz*dz;
    if(length<=0) return {0,0};
    const double t=std::clamp(((position.x-a.x)*dx+(position.y-a.z)*dz)/length,0.,1.);
    const double px=a.x+dx*t,pz=a.z+dz*t;
    const double lateralSquared=(px-position.x)*(px-position.x)+(pz-position.y)*(pz-position.y);
    const double step=double(speed)*dt;
    // Reconnect ahead of the projection when possible; choosing the nearest
    // neighboring float alone can walk backward along a boundary indefinitely.
    const double ahead=std::sqrt(std::max(0.,step*step-lateralSquared));
    const double forward=std::min(1.,t+ahead/std::sqrt(length));
    return roundedConnection(mesh,{a.x+dx*forward,a.z+dz*forward},b,position,speed,dt);
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
        navmesh_path::PathId path=navmesh_path::invalidPath;
        TrunkCandidates trunkCandidates;
        glm::vec2 velocity{0}, plannedGoal{0}, lastPosition{0};
        std::uint32_t goalPolygon=navmesh_path::noPolygon, currentPolygon=navmesh_path::noPolygon;
        glm::vec2 checkedStart{0}, checkedEnd{0};
        struct CoverageRegion {
            navmesh_path::Point minimum{}, maximum{};
            std::uint32_t polygon=navmesh_path::noPolygon;
            bool valid=false;
            bool contains(navmesh_path::Point p) const {
                return valid && p.x>=minimum.x && p.x<=maximum.x && p.z>=minimum.z && p.z<=maximum.z;
            }
        } coverage;
        bool hasPlan=false, recorded=false, checkedMovement=false;
    };
    struct Recovery {
        glm::vec2 position{0},velocity{0};
        float speed=0,seconds=0;
        std::size_t segment=0;
        bool valid=false;
    };
    navmesh_path::Mesh mesh;
    navmesh_path::PathManager paths;
    steering::Controller controller;
    std::vector<State> states;
    std::vector<Recovery> recovery;
    std::vector<NearbyTrunk> trunks;
    std::vector<steering::Obstacle> obstacles;
    ZombieNavigationStats stats;
    navmesh_path::Status status=navmesh_path::Status::noPath;
    std::string meshHash;

    Impl(navmesh_path::Mesh source,std::size_t population,std::size_t trunkCapacity)
        :mesh(std::move(source)),paths(mesh,slots(population),50'000,8.0),
         controller({},trunkCapacity),states(population),recovery(population),trunks(trunkCapacity),obstacles(trunkCapacity) {
        validateMesh(mesh);
        if (!trunkCapacity) throw std::invalid_argument("Zombie navigation needs a positive trunk capacity");
    }
    bool covered(State& state,navmesh_path::Point from,navmesh_path::Point to) {
        auto& region=state.coverage;
        // A rectangle is convex. Its four certified corners prove coverage of
        // every segment inside it, including small steering/crowd deviations.
        if (region.contains(from) && region.contains(to)) {
            ++stats.coverageRegionReuses;
            return true;
        }
        if (!mesh.segmentClear(from,to,nullptr,&state.currentPolygon)) return false;
        const double dx=to.x-from.x,dz=to.z-from.z,length=std::hypot(dx,dz);
        if (length<=0) return true;
        // Prefer 60 cm ahead; near a polygon edge certify only the short step.
        // This predicts no movement and changes neither speed nor steering.
        for (const double reach:{std::max(.6,length),length}) {
            const navmesh_path::Point ahead{from.x+dx*(reach/length),from.z+dz*(reach/length)};
            const navmesh_path::Point minimum{std::min(from.x,ahead.x)-.02,std::min(from.z,ahead.z)-.02};
            const navmesh_path::Point maximum{std::max(from.x,ahead.x)+.02,std::max(from.z,ahead.z)+.02};
            const auto polygon=state.currentPolygon;
            if (mesh.containsPoint(polygon,minimum) && mesh.containsPoint(polygon,maximum) &&
                mesh.containsPoint(polygon,{minimum.x,maximum.z}) &&
                mesh.containsPoint(polygon,{maximum.x,minimum.z})) {
                region={minimum,maximum,polygon,true};
                ++stats.coverageRegionBuilds;
                break;
            }
        }
        return true;
    }
    void release(std::size_t id) {
        paths.releaseZombie(id);
        states[id].hasPlan=false;
        states[id].path=navmesh_path::invalidPath;
        states[id].goalPolygon=navmesh_path::noPolygon;
        states[id].checkedMovement=false;
        states[id].coverage.valid=false;
        states[id].follow={};
        recovery[id].valid=false;
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

bool ZombieNavigation::plan(std::size_t id,glm::vec3 feet,glm::vec3 target) {
    auto& p=*impl_;
    if (id>=p.states.size()) throw std::out_of_range("Invalid zombie navigation ID");
    auto& s=p.states[id];
    s.follow={};
    p.recovery[id].valid=false;
    s.checkedMovement=false;
    s.coverage.valid=false;
    if (!finite(feet) || !finite(target)) {
        p.release(id);
        p.status=navmesh_path::Status::blockedEndpoint;
        return false;
    }
    const auto position=horizontal(feet),goal=horizontal(target);
    s.plannedGoal=goal;s.lastPosition=position;s.recorded=true;
    const auto startPolygon=p.mesh.locate(point(position)),goalPolygon=p.mesh.locate(point(goal));
    if (startPolygon==navmesh_path::noPolygon || goalPolygon==navmesh_path::noPolygon) {
        p.release(id);
        p.status=navmesh_path::Status::blockedEndpoint;
        return false;
    }
    if (p.mesh.component(startPolygon)!=p.mesh.component(goalPolygon)) {
        p.release(id);
        p.status=navmesh_path::Status::noPath;
        return false;
    }
    s.goalPolygon=goalPolygon;s.currentPolygon=startPolygon;
    ++p.stats.requests;
    s.path=p.paths.request(id,point(position),point(goal));
    s.hasPlan=s.path!=navmesh_path::invalidPath;
    p.status=p.paths.lastStatus();
    if (s.hasPlan) ++p.stats.foundPaths;
    else { ++p.stats.failedPaths;p.release(id); }
    return s.hasPlan;
}

glm::vec2 ZombieNavigation::follow(std::size_t id,glm::vec3 feet,
                                    float speed,float dt,const CollisionWorld& world) {
    zombie_profile::Timer timer(zombie_profile::Stage::navigationControl);
    auto& p=*impl_;
    if (id>=p.states.size()) return {0,0};
    auto& s=p.states[id];
    s.checkedMovement=false;
    if (!finite(feet) || !std::isfinite(speed) || speed<=0 || !std::isfinite(dt) || dt<=0) return {0,0};
    if (!s.hasPlan) return {0,0};
    const auto position=horizontal(feet),goal=s.plannedGoal;
    // External placement/teleport invalidates both route progress and sampled
    // velocity. Ordinary accepted movement is recorded after the physical guard.
    if (s.recorded && glm::length(position-s.lastPosition)>.5f) {
        reset(id);
        return {0,0};
    }
    const auto startPolygon=s.coverage.contains(point(position))?s.coverage.polygon:
        p.mesh.containsPoint(s.currentPolygon,point(position))?s.currentPolygon:p.mesh.locate(point(position));
    const auto goalPolygon=s.goalPolygon;
    if (startPolygon==navmesh_path::noPolygon || goalPolygon==navmesh_path::noPolygon ||
        p.mesh.component(startPolygon)!=p.mesh.component(goalPolygon)) {
        p.release(id);
        p.status=startPolygon==navmesh_path::noPolygon || goalPolygon==navmesh_path::noPolygon
            ?navmesh_path::Status::blockedEndpoint:navmesh_path::Status::noPath;
        ++p.stats.blockedUpdates;
        return {0,0};
    }
    s.currentPolygon=startPolygon;
    const auto path=p.paths.view(s.path);
    const auto reconnect=[&] {
        auto& cache=p.recovery[id];
        if(!cache.valid || cache.position!=position || cache.segment!=s.follow.segment || cache.speed!=speed || cache.seconds!=dt) {
            cache={position,recoverPathVelocity(p.mesh,path,s.follow.segment,position,speed,dt),speed,dt,s.follow.segment,true};
        }
        return cache.velocity;
    };
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
    // The shared follower uses a0.15m waypoint arrival radius. Scenario travel
    // finishes at the fixed target using a validated final connection.
    if (s.follow.arrived) {
        const float remaining=glm::length(goal-position);
        if (remaining<=1e-4f) return {0,0};
        if (!p.mesh.segmentClear(point(position),point(goal))) {
            p.release(id);return {0,0};
        }
        desired=(goal-position)*(std::min(speed,remaining/dt)/remaining);
    }
    if(position+desired*dt==position) desired=reconnect();
    if (!finite(desired) || glm::dot(desired,desired)<1e-12f) return {0,0};
    bool overflow=false;
    const float range=std::max(speed,glm::length(s.velocity))*p.controller.config().horizon+
                      p.controller.config().radius+p.controller.config().skin;
    const auto count=world.nearbyTrunks(feet,1.8f,range,p.trunks.data(),p.trunks.size(),overflow,&s.trunkCandidates);
    if (overflow) { ++p.stats.trunkOverflows;s.velocity={0,0};return {0,0}; }
    for (std::size_t i=0;i<count;++i) {
        const auto& trunk=p.trunks[i].collider;
        p.obstacles[i]={{trunk.base.x,trunk.base.z},trunk.radius};
    }
    steering::Stats sample{};
    auto velocity=p.controller.steer(position,s.velocity,desired,p.obstacles.data(),count,&sample);
    ++p.stats.steeringCalls;p.stats.obstacleTests+=sample.obstacleTests;p.stats.velocitySamples+=sample.velocitySamples;
    if (!p.covered(s,point(position),point(position+velocity*dt))) {
        ++p.stats.meshRejectedCandidates;
        if (!p.covered(s,point(position),point(position+desired*dt))) {
            desired=reconnect();
            if(glm::dot(desired,desired)==0 || !p.covered(s,point(position),point(position+desired*dt))) return {0,0};
        }
        velocity=desired;
    }
    s.checkedStart=position;
    s.checkedEnd=position+velocity*dt;
    s.checkedMovement=true;
    return velocity;
}

void ZombieNavigation::recordMovement(std::size_t id,glm::vec3 before,glm::vec3 after,float dt) {
    auto& p=*impl_;
    if (id>=p.states.size() || !finite(before) || !finite(after) || !std::isfinite(dt) || dt<=0) return;
    auto& s=p.states[id];
    s.checkedMovement=false;
    s.velocity=(horizontal(after)-horizontal(before))/dt;
    if (!finite(s.velocity)) s.velocity={0,0};
    s.lastPosition=horizontal(after);s.recorded=true;
}
void ZombieNavigation::reset(std::size_t id) {
    if (id>=impl_->states.size()) return;
    impl_->paths.releaseZombie(id);
    impl_->states[id]={};
    impl_->recovery[id]={};
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
bool ZombieNavigation::movementClear(std::size_t id,glm::vec3 a,glm::vec3 b) const {
    if (!finite(a) || !finite(b)) return false;
    if (id<impl_->states.size()) {
        const auto& s=impl_->states[id];
        // Coverage is planar. Terrain may change Y without changing this segment.
        if (s.checkedMovement && horizontal(a)==s.checkedStart && horizontal(b)==s.checkedEnd) {
            ++impl_->stats.movementValidationReuses;
            return true;
        }
    }
    if (id<impl_->states.size()) return impl_->covered(impl_->states[id],point(horizontal(a)),point(horizontal(b)));
    return segmentClear(a,b);
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
