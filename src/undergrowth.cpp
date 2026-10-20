#include "undergrowth.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace viewer {
namespace {
bool finite(glm::vec3 p) { return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z); }
// Interval where the swept player center overlaps the expanded local ellipse
// and vertical slab. Capsule extents are conservatively mapped to local axes.
bool overlap(glm::vec3 a, glm::vec3 b, glm::vec3 half, float& first, float& last) {
    first=0; last=1;
    const glm::vec2 p(a.x/half.x,a.z/half.z), d((b.x-a.x)/half.x,(b.z-a.z)/half.z);
    const float aa=glm::dot(d,d), bb=glm::dot(p,d), cc=glm::dot(p,p)-1;
    if (aa<1e-12f) { if (cc>0) return false; }
    else {
        const float discriminant=bb*bb-aa*cc;
        if (discriminant<0) return false;
        const float root=std::sqrt(discriminant);
        first=std::max(first,(-bb-root)/aa); last=std::min(last,(-bb+root)/aa);
    }
    const float dy=b.y-a.y;
    if (std::abs(dy)<1e-8f) { if (std::abs(a.y)>half.y) return false; }
    else {
        const float u=(-half.y-a.y)/dy, v=(half.y-a.y)/dy;
        first=std::max(first,std::min(u,v)); last=std::min(last,std::max(u,v));
    }
    return first<=last;
}
} // namespace

UndergrowthIndex::UndergrowthIndex(const TreeLayer& trees) {
    std::vector<VisibilityBounds> bounds;
    for (const auto& instance:trees.instances) {
        if (instance.undergrowth==UndergrowthKind::none) continue;
        const auto& model=trees.assets.at(instance.asset).model;
        const auto center=(model.boundsMin+model.boundsMax)*.5f;
        const auto half=(model.boundsMax-model.boundsMin)*glm::vec3(.425f,.5f,.425f);
        const float determinant=glm::determinant(glm::mat3(instance.transform));
        if (!finite(center)||!finite(half)||glm::any(glm::lessThanEqual(half,glm::vec3(0)))||
            !std::isfinite(determinant)||std::abs(determinant)<1e-12f)
            throw std::runtime_error("Undergrowth requires finite, nondegenerate contact geometry");
        const auto inverse=glm::inverse(instance.transform);
        const auto worldCenter=glm::vec3(instance.transform*glm::vec4(center,1));
        glm::vec3 extent(0);
        for (int axis=0;axis<3;++axis) extent+=glm::abs(glm::vec3(instance.transform[axis]))*half[axis];
        if (!finite(worldCenter)||!finite(extent)) throw std::runtime_error("Invalid undergrowth transform");
        shrubs_.push_back({inverse,center,half,instance.undergrowth==UndergrowthKind::dry});
        bounds.push_back({worldCenter-extent,worldCenter+extent});
    }
    index_=VisibilityIndex(bounds);
}

UndergrowthContact UndergrowthIndex::contact(const UndergrowthMotion& before,
    const UndergrowthMotion& after,std::vector<std::size_t>& scratch) const {
    UndergrowthContact result;
    const float height=std::max(before.height,after.height), radius=std::max(before.radius,after.radius);
    if (!finite(before.feet)||!finite(after.feet)||!std::isfinite(height)||!std::isfinite(radius)||height<=0||radius<0)
        return result;
    const glm::vec3 margin(radius,0,radius);
    result.stats=index_.query({glm::min(before.feet,after.feet)-margin,
        glm::max(before.feet,after.feet)+margin+glm::vec3(0,height,0)},scratch);
    const glm::vec3 playerHalf(radius,height*.5f,radius), offset(0,height*.5f,0);
    const float distance=glm::length(glm::vec2(after.feet.x-before.feet.x,after.feet.z-before.feet.z));
    for (const auto id:scratch) {
        const auto& shrub=shrubs_[id];
        glm::vec3 expansion(0);
        for (int axis=0;axis<3;++axis) expansion+=glm::abs(glm::vec3(shrub.inverse[axis]))*playerHalf[axis];
        const auto a=glm::vec3(shrub.inverse*glm::vec4(before.feet+offset,1))-shrub.center;
        const auto b=glm::vec3(shrub.inverse*glm::vec4(after.feet+offset,1))-shrub.center;
        float first=0,last=0;
        if (!overlap(a,b,shrub.half+expansion,first,last)) continue;
        if (!result.touched || (shrub.dry&&!result.dry))
            result.position=glm::mix(before.feet,after.feet,(first+last)*.5f)+glm::vec3(0,std::min(height*.5f,.8f),0);
        result.touched=true; result.dry|=shrub.dry;
        // Overlapping shrubs cannot multiply cadence. Longest covered interval
        // is a conservative distance estimate when several shrubs are crossed.
        result.distance=std::max(result.distance,distance*std::max(0.f,last-first));
    }
    return result;
}

void UndergrowthEmitter::clearPath() { pathSize_=0; querySeconds_=0; }
void UndergrowthEmitter::reset() {
    clearPath(); touching_=false; contactDistance_=0; hasLast_=false; sinceEvent_=1; idleSeconds_=0;
}

std::optional<UndergrowthEvent> UndergrowthEmitter::update(float seconds,const UndergrowthMotion& before,
    const UndergrowthMotion& after,const UndergrowthIndex& index) {
    if (!std::isfinite(seconds)||seconds<=0) return std::nullopt;
    sinceEvent_+=seconds;
    const auto delta=after.feet-before.feet;
    const float distance=glm::length(glm::vec2(delta.x,delta.z));
    const bool discontinuity=hasLast_&&glm::distance(before.feet,lastFeet_)>.1f;
    lastFeet_=after.feet; hasLast_=true;
    if (index.size()==0||!before.grounded||!after.grounded||!finite(delta)||
        seconds>.2f||glm::length(delta)>std::max(2.f,seconds*15.f)||discontinuity) {
        clearPath(); touching_=false; contactDistance_=0;
        return std::nullopt;
    }
    // Render frames can contain no 120 Hz physics step. Brief zero-displacement
    // samples must not erase the swept path or silence high-refresh clients.
    idleSeconds_=distance<.002f?idleSeconds_+seconds:0;
    if (idleSeconds_>.08f) { clearPath(); touching_=false; contactDistance_=0; return std::nullopt; }
    if (distance>=.002f) {
        bool merge=false;
        if (pathSize_ && path_[pathSize_-1].after.height==after.height && path_[pathSize_-1].after.radius==after.radius) {
            const auto old=path_[pathSize_-1].after.feet-path_[pathSize_-1].before.feet;
            merge=glm::dot(old,delta)>0 && glm::length(glm::cross(old,delta))<=1e-4f*glm::length(old)*glm::length(delta);
        }
        if (merge) path_[pathSize_-1].after=after;
        else path_[pathSize_++]={before,after};
    }
    querySeconds_+=seconds;
    if (querySeconds_+1e-6<.05 && pathSize_<path_.size()) return std::nullopt;
    UndergrowthContact combined;
    for (std::size_t i=0;i<pathSize_;++i) {
        const auto contact=index.contact(path_[i].before,path_[i].after,scratch_);
        if (!contact.touched) continue;
        if (!combined.touched||(contact.dry&&!combined.dry)) combined.position=contact.position;
        combined.touched=true; combined.dry|=contact.dry; combined.distance+=contact.distance;
    }
    clearPath();
    if (!combined.touched) { touching_=false; contactDistance_=0; return std::nullopt; }
    contactDistance_+=combined.distance;
    const bool starting=!touching_;
    if (contactDistance_<.05f || (!starting&&contactDistance_<.65f) || sinceEvent_+1e-6<.25) return std::nullopt;
    touching_=true;
    sinceEvent_=0; contactDistance_=0;
    const bool crack=combined.dry&&after.running&&((events_+seed_)%3==0);
    const auto kind=crack?NoiseEvent::Kind::twigSnap:combined.dry?NoiseEvent::Kind::dryRustle:NoiseEvent::Kind::rustle;
    float decibels=crack?58.f:combined.dry?(after.running?55.f:47.f):(after.running?52.f:45.f);
    if (after.height<.8f) decibels-=10; else if (after.height<1.4f) decibels-=6;
    const float gainScale=after.height<.8f?.32f:after.height<1.4f?.5f:1.f;
    return UndergrowthEvent{{combined.position,decibels,kind},after.running,events_++,gainScale};
}
} // namespace viewer
