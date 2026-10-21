#include "potential_surface.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <random>
#include <stdexcept>

namespace {
using namespace viewer::potential_surface;
std::atomic<std::size_t> allocations{0};
void require(bool condition,const char* message) { if (!condition) throw std::runtime_error(message); }
bool near(double a,double b,double tolerance=1e-8) {
    return std::abs(a-b)<=tolerance*(1+std::max(std::abs(a),std::abs(b)));
}
long double scalarPotential(Point p,const Obstacle& o,const Config& c) {
    const long double norm=std::hypotl(o.axis.x,o.axis.z);
    const long double ux=o.axis.x/norm,uz=o.axis.z/norm;
    long double distance=std::hypotl(p.x-o.center.x,p.z-o.center.z),shapeRadius;
    if (o.kind==0) shapeRadius=std::hypotl(o.halfX,o.halfZ);
    else if (o.kind==1) shapeRadius=std::max(o.halfX,o.halfZ);
    else {
        const bool xLong=o.halfX>=o.halfZ;
        const long double axisX=xLong?ux:-uz,axisZ=xLong?uz:ux;
        const long double along=std::clamp((p.x-o.center.x)*axisX+(p.z-o.center.z)*axisZ,
                                         -static_cast<long double>(xLong?o.halfX:o.halfZ),
                                          static_cast<long double>(xLong?o.halfX:o.halfZ));
        distance=std::hypotl(p.x-o.center.x-along*axisX,p.z-o.center.z-along*axisZ);
        shapeRadius=xLong?o.halfZ:o.halfX;
    }
    const long double clearance=distance-shapeRadius-c.radius-c.skin-(o.kind==0?c.buildingExtra:0);
    if (clearance<0) return 1-clearance/c.lengthScale;
    if (clearance>=c.support) return 0;
    const long double q=1+clearance/c.lengthScale,taper=1-std::pow(clearance/c.support,2);
    return 2*std::pow(taper,3)/(1+q*q);
}
void derivativesAndShapes() {
    Config c;
    const Obstacle shapes[]={{0,{1,-2},{.6,.8},3,4},{1,{-2,1},{1,0},.2,.4},
        {2,{2,3},{.6,.8},7,.15},{2,{2,3},{.6,.8},.15,7}};
    const Point positions[]={{7,1},{-1.5,2},{2,8},{9,8},{.1,-1},{-2.1,4.2}};
    constexpr double step=1e-5;
    for (const auto& o:shapes) for (auto p:positions) {
        const auto actual=obstacleField(p,o,c);
        const auto dx=(scalarPotential({p.x+step,p.z},o,c)-scalarPotential({p.x-step,p.z},o,c))/(2*step);
        const auto dz=(scalarPotential({p.x,p.z+step},o,c)-scalarPotential({p.x,p.z-step},o,c))/(2*step);
        require(near(actual.potential,double(scalarPotential(p,o,c)),1e-10),"Potential value differs from independent scalar reference");
        require(near(actual.gradient.x,double(dx),2e-7) && near(actual.gradient.z,double(dz),2e-7),
                "Analytical field gradient differs from independent central differences");
    }
    const Obstacle building{0,{0,0},{1,0},3,4};
    const double physicalRadius=5+c.radius+c.skin+c.buildingExtra;
    const auto atSurface=obstacleField({physicalRadius,0},building,c);
    require(near(atSurface.potential,1) && near(atSurface.gradient.x,-1/c.lengthScale),
            "Building surface slope must be independent of circumscribed radius");
    require(obstacleField({3,4},building,c).potential>1,"Circumscribed building circle missed an original corner");
    const World world({building},{-20,-20,20,20},c);
    QueryStats insideStats;
    require(world.clear({4.2,0}) && world.field({4.2,0},insideStats).potential>1,
            "A physically free point inside a conservative building field must stay legal");
    const auto outside=obstacleField({physicalRadius+c.support,0},building,c);
    const auto barelyInside=obstacleField({physicalRadius+c.support-1e-5,0},building,c);
    require(outside.potential==0 && dot(outside.gradient,outside.gradient)==0 &&
            std::abs(barelyInside.gradient.x)<1e-8,"Compact support did not fade smoothly");
    const auto inner=obstacleField({physicalRadius-1e-7,0},building,c);
    const auto outer=obstacleField({physicalRadius+1e-7,0},building,c);
    require(near(inner.potential,outer.potential,3e-7) && near(inner.gradient.x,outer.gradient.x,3e-7),
            "Inside/outside clearance profile is not C1 continuous");
    const Obstacle fence{2,{0,0},{1,0},5,.1};
    const auto middle=obstacleField({0,1},fence,c),along=obstacleField({4,1},fence,c);
    require(near(middle.potential,along.potential) && middle.gradient.x==0 && along.gradient.x==0,
            "Long fence field varies along its straight center section");
    const auto end=obstacleField({5.5,.5},fence,c);
    require(end.gradient.x<0 && near(end.gradient.x,end.gradient.z),"Fence end does not have capsule radial gradient");
    const auto left=obstacleField({5-1e-7,1},fence,c),right=obstacleField({5+1e-7,1},fence,c);
    require(near(left.gradient.x,right.gradient.x,2e-7) && near(left.gradient.z,right.gradient.z,2e-7),
            "Capsule straight/end transition is not gradient continuous");
    const auto uphill=filterVelocity({2,0},{1,0},16),downhill=filterVelocity({-2,0},{1,0},16);
    require(near(uphill.x,2.0/17) && uphill.z==0 && downhill.x==-2 && downhill.z==0,
            "One-sided filter changed head-on/downhill semantics");
    const auto velocity=filterVelocity({2,1},{1,0},16);
    require(near(velocity.x,2.0/17) && velocity.z==1,"Filter changed tangential component or normalized speed");
    for (unsigned index=0;index<200;++index) {
        const Point desired{std::cos(index*.21)*2,std::sin(index*.21)*2};
        const Point g{std::cos(index*.37)*17,std::sin(index*.37)*17};
        const auto output=filterVelocity(desired,g,c.weight);
        require(dot(desired,output)>0 && dot(output,output)<=dot(desired,desired)+1e-12,
                "Finite-weight filter reversed goal progress or increased speed");
    }
}
// Independent long-double segment/shape oracle. Bounds are center-inset by the
// same declared physical radius, rather than inferred from the field shape.
bool blocked(Point a,Point b,const Obstacle& o,const Config& c) {
    const long double padding=c.radius+c.skin+(o.kind==0?c.buildingExtra:0);
    const long double dx=static_cast<long double>(b.x)-a.x,dz=static_cast<long double>(b.z)-a.z;
    if (o.kind==1) {
        const long double length2=dx*dx+dz*dz;
        const long double t=length2>0?std::clamp(((o.center.x-static_cast<long double>(a.x))*dx+
            (o.center.z-static_cast<long double>(a.z))*dz)/length2,0.L,1.L):0;
        return std::hypotl(a.x+t*dx-o.center.x,a.z+t*dz-o.center.z)<=std::max(o.halfX,o.halfZ)+padding;
    }
    const long double norm=std::hypotl(o.axis.x,o.axis.z),ux=o.axis.x/norm,uz=o.axis.z/norm;
    const long double localA[]={ (a.x-o.center.x)*ux+(a.z-o.center.z)*uz,
                                -(a.x-o.center.x)*uz+(a.z-o.center.z)*ux };
    const long double localB[]={ (b.x-o.center.x)*ux+(b.z-o.center.z)*uz,
                                -(b.x-o.center.x)*uz+(b.z-o.center.z)*ux };
    const long double extent[]={o.halfX+padding,o.halfZ+padding};
    long double low=0,high=1;
    for (unsigned axis=0;axis<2;++axis) {
        if (localA[axis]==localB[axis]) {
            if (localA[axis]<-extent[axis] || localA[axis]>extent[axis]) return false;
        } else {
            const auto t0=(-extent[axis]-localA[axis])/(localB[axis]-localA[axis]);
            const auto t1=(extent[axis]-localA[axis])/(localB[axis]-localA[axis]);
            low=std::max(low,std::min(t0,t1));high=std::min(high,std::max(t0,t1));
            if (low>high) return false;
        }
    }
    return true;
}
void collisionAndBounds() {
    Config c;c.radius=.5;c.skin=0;c.buildingExtra=0;
    World circle({{1,{0,0},{1,0},.5,.5}},{-10,-10,10,10},c);
    QueryStats stats;
    require(!circle.segmentClear({-2,1},{2,1}),"Closed circle tangent must count as contact");
    require(circle.segmentClear({-2,1.00001},{2,1.00001}),"Near tangent clear segment was blocked");
    const auto hit=circle.move({-4,0},{4,0},stats);
    require(hit.constrained && !hit.invalidStart && hit.position.x<-1 && hit.position.x>-1.001 &&
            circle.segmentClear({-4,0},hit.position),"High speed circle move crossed its first entry");
    require(circle.move({0,0},{4,0},stats).invalidStart,"Invalid physical starting point was not reported");
    const auto tangent=circle.move({-2,1},{2,1},stats);
    require(tangent.constrained && tangent.position.x<0 && circle.segmentClear({-2,1},tangent.position),
            "Tangent clipping did not leave a strictly clear segment");
    World rectangle({{2,{0,0},{1,0},2,.1}},{-10,-10,10,10},c);
    require(!rectangle.segmentClear({-4,.6},{4,.6}) &&
            rectangle.segmentClear({-4,.60001},{4,.60001}),
            "Padded original OBB touching was not treated as closed contact");
    const auto still=circle.move({-4,0},{-4,0},stats);
    require(!still.constrained && still.position.x==-4,"Zero-length clear step was changed");
    World box({{0,{3000,3000},{.6,.8},2,1}},{2990,2990,3010,3010},c);
    const auto moved=box.move({2992,3000},{3008,3000},stats);
    require(moved.constrained && box.segmentClear({2992,3000},moved.position) && box.clear(moved.position),
            "Rotated map-scale OBB entry or strict backoff is wrong");
    World empty({},{-10,-10,10,10},c);
    require(!empty.clear({9.5,0}) && empty.move({9.5,0},{0,0},stats).invalidStart,
            "Closed map boundary contact must be invalid at the body inset");
    const auto boundary=empty.move({0,0},{100,50},stats);
    require(boundary.constrained && empty.clear(boundary.position) && boundary.position.x<9.5 &&
            near(boundary.position.z/boundary.position.x,.5),"Map boundary clip changed direction or missed body inset");
    World two({{1,{2,0},{1,0},.5,.5},{1,{-2,0},{1,0},.5,.5}},{-10,-10,10,10},c);
    const auto earliest=two.move({-8,0},{8,0},stats);
    require(earliest.position.x<-3 && earliest.position.x>-3.001,"Movement did not choose closest collision");
}
void indexesAndAllocations() {
    Config c;
    std::vector<Obstacle> obstacles;
    for (int x=-15;x<=15;++x) for (int z=-15;z<=15;++z) {
        const double angle=.07*(x+2*z);
        obstacles.push_back({unsigned((x+z+60)%3),{x*8.,z*8.},{std::cos(angle),std::sin(angle)},
            .2+.07*((x+15)%5),.12+.04*((z+15)%4)});
    }
    const World world(obstacles,{-140,-140,140,140},c);
    std::mt19937 random(123456);
    auto coordinate=[&] { return -130.+260.*(double(random())/4294967296.); };
    const auto before=allocations.load();
    std::uint64_t totalCandidates=0;
    for (unsigned query=0;query<600;++query) {
        const Point a{coordinate(),coordinate()},b{coordinate(),coordinate()};
        QueryStats stats;
        const auto indexed=world.field(a,stats);
        FieldValue exhaustive;
        bool collision=false;
        for (const auto& o:obstacles) {
            const auto expected=obstacleField(a,o,c);
            exhaustive.potential+=expected.potential;exhaustive.gradient=exhaustive.gradient+expected.gradient;
            collision|=blocked(a,b,o,c);
        }
        require(near(indexed.potential,exhaustive.potential,1e-10) &&
                near(indexed.gradient.x,exhaustive.gradient.x,1e-10) &&
                near(indexed.gradient.z,exhaustive.gradient.z,1e-10),"Field BVH omitted a contributor");
        require(world.segmentClear(a,b)==!collision,"Collision BVH disagrees with exhaustive long-double geometry");
        if (world.clear(a)) {
            const auto movement=world.move(a,b,stats);
            require(!movement.invalidStart && world.segmentClear(a,movement.position),"Constrained move is not physically clear");
            for (const auto& o:obstacles) require(!blocked(a,movement.position,o,c),"Accepted segment failed independent physical oracle");
        }
        totalCandidates+=stats.fieldCandidates;
    }
    require(totalCandidates<600*obstacles.size()/8,"Field index performs an almost full scene scan");
    require(allocations.load()==before,"World field, clear, segmentClear or move allocated after construction");
    require(world.memoryBytes()>obstacles.size()*sizeof(Obstacle),"World memory excludes immutable indexes");
}
}
void* operator new(std::size_t size) {
    ++allocations;
    if (void* memory=std::malloc(size?size:1)) return memory;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory,std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory,std::size_t) noexcept { std::free(memory); }
int main() try {
    derivativesAndShapes();collisionAndBounds();indexesAndAllocations();
    std::cout<<"Potential field derivatives, circle/capsule shapes, independent physical guards, BVH and allocation tests passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr<<error.what()<<'\n';return 1;
}
