#include "bird_flock.hpp"
#include "collision_world.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace viewer {
namespace {
constexpr float birdRadius = .14f;

bool finite(glm::vec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
float squared(glm::vec3 value) { return glm::dot(value, value); }
bool validPlayer(const BirdFlockPlayer& player) {
    return player.targetable && finite(player.feet) && std::isfinite(player.radius) &&
        std::isfinite(player.height) && player.radius > 0 && player.height >= 2 * player.radius;
}
// Closest points of two finite line segments (including degenerate segments).
float segmentDistanceSquared(glm::vec3 p, glm::vec3 q, glm::vec3 a, glm::vec3 b) {
    const auto u = q - p, v = b - a, w = p - a;
    const float uu = squared(u), vv = squared(v), uv = glm::dot(u,v);
    const float uw = glm::dot(u,w), vw = glm::dot(v,w);
    float s = 0, t = 0;
    if (uu <= 1e-12f) t = vv > 1e-12f ? std::clamp(vw / vv, 0.f, 1.f) : 0;
    else if (vv <= 1e-12f) s = std::clamp(-uw / uu, 0.f, 1.f);
    else {
        const float denominator = uu * vv - uv * uv;
        if (denominator > 1e-12f) s = std::clamp((uv * vw - uw * vv) / denominator, 0.f, 1.f);
        t = (uv * s + vw) / vv;
        if (t < 0) { t = 0; s = std::clamp(-uw / uu, 0.f, 1.f); }
        else if (t > 1) { t = 1; s = std::clamp((uv - uw) / uu, 0.f, 1.f); }
    }
    return squared(w + u * s - v * t);
}
}

bool birdHitsPlayer(glm::vec3 from, glm::vec3 to, const BirdFlockPlayer& player, float radius) {
    if (!finite(from) || !finite(to) || !validPlayer(player) || !std::isfinite(radius) || radius < 0) return false;
    const auto a = player.feet + glm::vec3(0, player.radius, 0);
    const auto b = player.feet + glm::vec3(0, player.height - player.radius, 0);
    const float combined = player.radius + radius;
    return segmentDistanceSquared(from, to, a, b) <= combined * combined;
}

std::uint32_t BirdFlocks::randomInteger() {
    random_ ^= random_ << 13; random_ ^= random_ >> 17; random_ ^= random_ << 5;
    return random_;
}
boid::Vec3 BirdFlocks::randomOffset(std::int32_t radius, std::int32_t height) {
    const auto x=static_cast<std::int32_t>(randomInteger()%std::uint32_t(radius*2+1))-radius;
    const auto y=static_cast<std::int32_t>(randomInteger()%std::uint32_t(height*2+1))-height;
    const auto z=static_cast<std::int32_t>(randomInteger()%std::uint32_t(radius*2+1))-radius;
    const auto horizontal=boid::limited({x,0,z},radius);
    return {horizontal.x,y,horizontal.z};
}
namespace {
boid::Vec3 clamp(boid::Vec3 p, boid::Vec3 minimum, boid::Vec3 maximum, std::int32_t margin) {
    return {std::clamp(p.x,minimum.x+margin,maximum.x-margin),
            std::clamp(p.y,minimum.y+margin,maximum.y-margin),
            std::clamp(p.z,minimum.z+margin,maximum.z-margin)};
}
bool same(const boid::Control& a, const boid::Control& b) {
    return a.target==b.target && a.aim==b.aim && a.exit==b.exit && a.attackDirection==b.attackDirection &&
        a.mode==b.mode && a.remainingTicks==b.remainingTicks && a.pass==b.pass;
}
}
bool BirdFlocks::waypoint(boid::Vec3 point, std::int32_t radius) {
    ++stats_.waypointChecks;
    if (point.x<minimum_.x+radius || point.x>maximum_.x-radius ||
        point.y<minimum_.y+radius || point.y>maximum_.y-radius ||
        point.z<minimum_.z+radius || point.z>maximum_.z-radius) return false;
    return space_.clearPoint(boid::toMeters(point),float(radius)/boid::unitsPerMeter);
}
bool BirdFlocks::corridor(boid::Vec3 from, boid::Vec3 to, std::int32_t radius) {
    ++stats_.segmentChecks;
    return space_.clearSegment(boid::toMeters(from),boid::toMeters(to),float(radius)/boid::unitsPerMeter);
}
BirdFlocks::BirdFlocks(const CollisionWorld& world, const TreeLayer* trees,
                       glm::vec3 boundsMin, glm::vec3 boundsMax, glm::vec3 preferredCenter,
                       BirdFlockSettings settings)
    : space_(world,trees), random_(settings.seed?settings.seed:0x62697264u) {
    if (!finite(boundsMin) || !finite(boundsMax) || !finite(preferredCenter) ||
        glm::any(glm::lessThan(boundsMax,boundsMin)))
        throw std::invalid_argument("Bird flock bounds must be finite ordered world metres");
    if (settings.flockCount>boid::maxFlocks || settings.birdsPerFlock>4096 ||
        (settings.flockCount && settings.birdsPerFlock>boid::maxBirds/settings.flockCount))
        throw std::invalid_argument("Bird flocks are limited to 64 flocks and 65536 birds");
    if (!settings.flockCount || !settings.birdsPerFlock) return;
    minimum_=boid::fromMeters(boundsMin); maximum_=boid::fromMeters(boundsMax+glm::vec3(0,45,0));
    const auto extent=maximum_-minimum_;
    if (extent.x<=14000 || extent.y<=14000 || extent.z<=14000) return;
    preferred_=clamp(boid::fromMeters(preferredCenter),minimum_,maximum_,0);
    boid::Checkpoint initial; initial.seed=random_;
    initial.birds.reserve(settings.flockCount*settings.birdsPerFlock);
    for (std::size_t index=0;index<settings.flockCount;++index) {
        boid::Vec3 center{}; bool found=false;
        for (int attempt=0;attempt<32;++attempt) {
            center=preferred_+randomOffset(35000+18000*static_cast<std::int32_t>(index),10000);
            center.y+=18000+static_cast<std::int32_t>(randomInteger()%8001);
            center=clamp(center,minimum_,maximum_,7000);
            if (waypoint(center,7000)) { found=true; break; }
        }
        if (!found) { center.y=maximum_.y-8000; found=waypoint(center,7000); }
        if (!found) continue;
        boid::Flock flock;
        flock.firstBird=static_cast<std::uint32_t>(initial.birds.size());
        flock.birdCount=static_cast<std::uint32_t>(settings.birdsPerFlock);
        flock.center=flock.control.target=center;
        flock.velocity=boid::direction(randomOffset(1000,150),8000);
        initial.flocks.push_back(flock);
        FlockData data; data.nextAttack=90+index*36; flockData_.push_back(data);
        for (std::size_t i=0;i<settings.birdsPerFlock;++i) {
            boid::Bird bird; bird.position=center+boid::birdOffset(initial.seed,static_cast<std::uint32_t>(initial.birds.size()));
            bird.velocity=flock.velocity; initial.birds.push_back(bird);
        }
    }
    simulation_.restore(initial);
    for (std::size_t i=0;i<initial.flocks.size();++i) chooseWander(i,initial.flocks[i].control);
    simulation_.restore(std::move(initial));
    birdData_.resize(simulation_.state().birds.size());
    plans_.resize(flockData_.size()); commands_.reserve(flockData_.size());
    refreshPresentation(); stats_={};
}
bool BirdFlocks::chooseWander(std::size_t index, boid::Control& control) {
    const auto& flock=simulation_.state().flocks[index];
    auto& data=flockData_[index]; const auto tick=simulation_.state().tick+1;
    for (int attempt=0;attempt<4;++attempt) {
        auto point=clamp(flock.center+randomOffset(65000,14000),minimum_,maximum_,7000);
        point.y=std::max(point.y,minimum_.y+12000);
        if (boid::lengthSquared(point-flock.center)<100000000) continue;
        if (waypoint(point,7000) && corridor(flock.center,point,7000)) {
            control.target=point; data.routeActive=true;
            data.nextWaypoint=tick+360+randomInteger()%241; return true;
        }
    }
    control.target=flock.center; data.routeActive=false; data.nextWaypoint=tick+60; return false;
}
void BirdFlocks::recover(std::size_t index, boid::Control& control) {
    const auto tick=simulation_.state().tick+1;
    control.mode=BirdFlockMode::recovering; control.remainingTicks=420; control.target=control.exit;
    auto& data=flockData_[index]; data.nextAttack=tick+600;
    data.routeActive=true; data.nextWaypoint=tick+120;
}
void BirdFlocks::detectPlayer(const BirdFlockPlayer& player, std::vector<boid::Control>& plans) {
    if (birds_.empty() || !validPlayer(player)) return;
    const auto chosen=static_cast<std::size_t>(randomInteger())%birds_.size();
    std::size_t index=0;
    const auto& state=simulation_.state();
    while (index+1<state.flocks.size() && chosen>=state.flocks[index].firstBird+state.flocks[index].birdCount) ++index;
    auto& control=plans[index]; auto& data=flockData_[index];
    if (control.mode!=BirdFlockMode::wandering || state.tick+1<data.nextAttack) return;
    const auto aim=boid::fromMeters(player.feet+glm::vec3(0,std::min(1.45f,player.height-.15f),0));
    const auto distance=boid::lengthSquared(aim-state.birds[chosen].position);
    if (distance>85000ull*85000 || distance<3000ull*3000) return;
    ++stats_.sightChecks;
    if (!space_.lineOfSight(birds_[chosen].position,boid::toMeters(aim))) return;
    const auto toward=aim-state.flocks[index].center;
    const auto horizontal=boid::direction({toward.x,0,toward.z},1000);
    const auto exit=aim+boid::ratio(horizontal,20,1)+boid::Vec3{0,9000,0};
    if (!waypoint(aim,200) || !waypoint(exit,1000) ||
        !corridor(state.flocks[index].center,aim,200) || !corridor(aim,exit,200)) {
        data.nextAttack=state.tick+61; return;
    }
    control.aim=aim; control.exit=exit; control.attackDirection=horizontal;
    ++control.pass; control.mode=BirdFlockMode::gathering;
    control.remainingTicks=51; control.target=exit;
}
void BirdFlocks::refreshPresentation() {
    const auto& state=simulation_.state(); boid::present(state,birds_);
    flocks_.resize(state.flocks.size());
    for (std::size_t i=0;i<flocks_.size();++i) {
        const auto& f=state.flocks[i];
        flocks_[i]={f.firstBird,f.birdCount,boid::toMeters(f.center),boid::toMeters(f.velocity),
            boid::toMeters(f.control.target),f.control.mode,float(f.control.remainingTicks)/boid::ticksPerSecond};
    }
}
std::vector<unsigned> BirdFlocks::step(const std::vector<BirdFlockPlayer>& players) {
    stats_={}; commands_.clear(); std::vector<unsigned> hits(players.size(),0);
    if (birds_.empty()) { simulation_.step(); return hits; }
    const auto& state=simulation_.state(); const auto tick=state.tick+1;
    for (std::size_t i=0;i<plans_.size();++i) plans_[i]=state.flocks[i].control;
    if (!players.empty()) detectPlayer(players[static_cast<std::size_t>(state.tick%players.size())],plans_);
    for (std::size_t i=0;i<plans_.size();++i) {
        auto& control=plans_[i]; auto& data=flockData_[i];
        if (control.mode!=BirdFlockMode::wandering && control.remainingTicks<=1) {
            if (control.mode==BirdFlockMode::gathering) {
                control.mode=BirdFlockMode::swooping;
                const auto distance=boid::integerSqrt(boid::lengthSquared(control.aim-state.flocks[i].center));
                control.remainingTicks=static_cast<std::uint32_t>(std::clamp(distance*60/17000+150,std::uint64_t(180),std::uint64_t(540)));
            } else if (control.mode==BirdFlockMode::swooping) recover(i,control);
            else { control.mode=BirdFlockMode::wandering; data.nextWaypoint=tick; }
        }
        if ((control.mode==BirdFlockMode::wandering || control.mode==BirdFlockMode::recovering) &&
            (tick>=data.nextWaypoint || (data.routeActive && boid::lengthSquared(control.target-state.flocks[i].center)<16000000)))
            chooseWander(i,control);
        if (!same(control,state.flocks[i].control)) commands_.push_back({tick,static_cast<std::uint32_t>(i),control});
    }
    simulation_.step(commands_);
    const auto& after=simulation_.state();
    for (std::size_t f=0;f<after.flocks.size();++f) {
        const auto& flock=after.flocks[f];
        for (std::size_t i=flock.firstBird;i<flock.firstBird+flock.birdCount;++i) {
            auto& data=birdData_[i];
            if (data.hitPass==flock.control.pass || tick<data.nextHit) continue;
            const auto previous=birds_[i].position, current=boid::toMeters(after.birds[i].position);
            for (std::size_t p=0;p<players.size();++p) {
                if (!birdHitsPlayer(previous,current,players[p],birdRadius)) continue;
                const auto torso=players[p].feet+glm::vec3(0,players[p].height*.5f,0);
                if (space_.clearPoint(torso,.01f) && space_.lineOfSight(previous,torso)) {
                    data.hitPass=flock.control.pass; data.nextHit=tick+120;
                    ++hits[p]; ++stats_.hits; break;
                }
            }
        }
    }
    refreshPresentation(); return hits;
}
unsigned BirdFlocks::update(float dt, const BirdFlockPlayer& player) {
    stats_={};
    if (!std::isfinite(dt) || dt<=0 || birds_.empty()) return 0;
    pendingSeconds_+=std::min(double(dt),.1);
    unsigned hits=0; BirdFlockStats total;
    while (pendingSeconds_>=1./boid::ticksPerSecond) {
        hits+=step({player})[0]; pendingSeconds_-=1./boid::ticksPerSecond;
        total.sightChecks+=stats_.sightChecks; total.waypointChecks+=stats_.waypointChecks;
        total.segmentChecks+=stats_.segmentChecks; total.hits+=stats_.hits;
    }
    stats_=total; return hits;
}
} // namespace viewer
