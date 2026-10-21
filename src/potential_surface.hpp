#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

// Standalone planar experiment. All positions are runtime X east / Z south
// meters. This library has no mesh, waypoint, search or Detour dependency.
namespace viewer::potential_surface {

struct Point { double x=0, z=0; };
inline Point operator+(Point a,Point b) { return {a.x+b.x,a.z+b.z}; }
inline Point operator-(Point a,Point b) { return {a.x-b.x,a.z-b.z}; }
inline Point operator*(Point a,double value) { return {a.x*value,a.z*value}; }
inline Point operator/(Point a,double value) { return {a.x/value,a.z/value}; }
inline double dot(Point a,Point b) { return a.x*b.x+a.z*b.z; }

// Original replay OBB. Kinds: 0 building, 1 circular trunk, 2 fence panel.
struct Obstacle {
    unsigned kind=0;
    Point center,axis{1,0};
    double halfX=0,halfZ=0;
};
struct Bounds { double minX=0,minZ=0,maxX=0,maxZ=0; };
struct Config {
    double radius=.3,skin=.002,buildingExtra=.05;
    double support=3,lengthScale=1,weight=16;
    bool exactBuildings=false; // Opt in to signed distance from the padded OBB.
};
struct QueryStats {
    std::uint64_t fieldNodes=0,fieldCandidates=0,fieldContributors=0;
    std::uint64_t collisionNodes=0,collisionCandidates=0;
};
struct FieldValue { double potential=0;Point gradient; };
struct MoveResult { Point position;bool constrained=false,invalidStart=false; };

// Building fields default to circumscribed circles; exactBuildings selects the
// signed distance of their padded OBBs. Fences use capsules along their long
// axis, and trees use circles. The clearance profile has inverse-quadratic
// decay and a compact taper outside the obstacle; it is linear inside a field
// approximation so large buildings retain a useful, finite surface gradient.
FieldValue obstacleField(Point position,const Obstacle&,const Config&);
// Preserve the earlier one-sided filter exactly; never normalize its output.
Point filterVelocity(Point desired,Point gradient,double weight);

class World {
public:
    World(std::vector<Obstacle>,Bounds,Config={});
    ~World();
    World(const World&)=delete;
    World& operator=(const World&)=delete;
    FieldValue field(Point,QueryStats&) const;
    bool clear(Point) const;
    // Strict segment check, with tangency to padded OBBs treated as contact.
    bool segmentClear(Point start,Point end) const;
    bool segmentClear(Point start,Point end,QueryStats&) const;
    // Clip at earliest collision with original inflated OBBs/tree circles or
    // map boundary. No sliding, tangent force, route search or recovery.
    MoveResult move(Point start,Point proposed,QueryStats&) const;
    const std::vector<Obstacle>& obstacles() const;
    Bounds bounds() const;
    const Config& config() const;
    std::size_t memoryBytes() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace viewer::potential_surface
