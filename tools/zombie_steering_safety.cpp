#include "zombie_steering_safety.hpp"
#include <algorithm>
#include <cmath>

namespace viewer::steering_benchmark {
namespace {
bool penetrating(glm::vec2 start,glm::vec2 end,const steering::Obstacle* obstacles,
                 std::size_t count,double bodyRadius) {
    const double dx=double(end.x)-start.x,dz=double(end.y)-start.y;
    const double squared=dx*dx+dz*dz;
    for (std::size_t id=0;id<count;++id) {
        const auto& obstacle=obstacles[id];
        const double x=double(start.x)-obstacle.center.x,z=double(start.y)-obstacle.center.y;
        const double radius=bodyRadius+obstacle.radius;
        const double t=squared>0?std::clamp(-(x*dx+z*dz)/squared,0.0,1.0):0;
        const double nearestX=x+t*dx,nearestZ=z+t*dz;
        if (nearestX*nearestX+nearestZ*nearestZ<radius*radius) return true;
    }
    return false;
}
}
SweepResult constrainTrunkSweep(glm::vec2 start,glm::vec2 proposed,
                               const steering::Obstacle* obstacles,std::size_t count,float bodyRadius) {
    SweepResult result{proposed};
    if (!std::isfinite(start.x) || !std::isfinite(start.y) || !std::isfinite(proposed.x) ||
        !std::isfinite(proposed.y) || !std::isfinite(bodyRadius) || bodyRadius<0 || (count && !obstacles))
        return {start,true,true};
    const double radiusBody=bodyRadius;
    const double dx=double(proposed.x)-start.x,dz=double(proposed.y)-start.y;
    const double squared=dx*dx+dz*dz;
    double earliest=1;
    bool blocked=false;
    for (std::size_t id=0;id<count;++id) {
        const auto& obstacle=obstacles[id];
        if (!std::isfinite(obstacle.center.x) || !std::isfinite(obstacle.center.y) ||
            !std::isfinite(obstacle.radius) || obstacle.radius<0) return {start,true,true};
        const double x=double(start.x)-obstacle.center.x,z=double(start.y)-obstacle.center.y;
        const double radius=radiusBody+obstacle.radius;
        const double c=x*x+z*z-radius*radius;
        if (c<0) return {start,true,true};
        if (squared==0) continue;
        const double b=x*dx+z*dz;
        const double closest=std::clamp(-b/squared,0.0,1.0);
        const double nearestX=x+closest*dx,nearestZ=z+closest*dz;
        if (nearestX*nearestX+nearestZ*nearestZ>=radius*radius) continue;
        // c/(-b+sqrt(discriminant)) avoids cancellation in the entry root when
        // starting near the boundary. A tangent that never enters is allowed.
        const double root=std::sqrt(std::max(0.0,b*b-squared*c));
        const double denominator=-b+root;
        const double entry=denominator>0?c/denominator:0;
        earliest=std::min(earliest,std::clamp(entry,0.0,1.0)); blocked=true;
    }
    if (!blocked) return result;
    result.constrained=true;
    constexpr double spatialBackoff=.0001;
    double t=std::max(0.0,earliest-spatialBackoff/std::sqrt(squared));
    for (unsigned attempt=0;attempt<32;++attempt) {
        result.position={float(double(start.x)+dx*t),float(double(start.y)+dz*t)};
        if (!penetrating(start,result.position,obstacles,count,radiusBody)) return result;
        t*=.5;
    }
    result.position=start;
    return result;
}
}
