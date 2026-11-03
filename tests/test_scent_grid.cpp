#include "scent_grid.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using viewer::ScentGrid;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void clockAndEmission() {
    ScentGrid grid;
    const std::vector<viewer::ScentEmitter> emitters{{{10,0,10},true,true},{{30,0,10},true,false},{{50,0,10},false,true}};
    grid.update(0,emitters); grid.update(.99,emitters);
    require(grid.activeCells() == 0,"Scent must not emit before a whole second");
    grid.update(1,emitters);
    require(grid.at(ScentGrid::Cell{0,0}) == 1 && grid.activeCells() == 1,
            "Only bleeding outdoor players may deposit scent");
    grid.update(1.99,emitters);
    require(grid.at(ScentGrid::Cell{0,0}) == 1,"Repeated frame updates must not duplicate deposits");
    grid.update(2,{});
    require(std::abs(grid.at(ScentGrid::Cell{0,0})-.784f) < 1e-6f &&
            std::abs(grid.at(ScentGrid::Cell{1,0})-.0392f) < 1e-6f &&
            std::abs(grid.at(ScentGrid::Cell{1,1})-.0098f) < 1e-6f,
            "Diffusion must use the simultaneous 80/4/1 stencil and two percent decay");
    double total = 0;
    for (int x = -1; x <= 1; ++x) for (int z = -1; z <= 1; ++z) total += grid.at(ScentGrid::Cell{x,z});
    require(std::abs(total-.98) < 1e-6,"Only decay may remove appreciable scent during spreading");
    require(ScentGrid::cellAt({-.01f,0,-20.01f}) == std::optional<ScentGrid::Cell>({-1,-2}),
            "Negative world coordinates must use floor rather than truncation");
    require(!ScentGrid::cellAt({std::numeric_limits<float>::infinity(),0,0}),"Nonfinite positions must not produce cells");
    ScentGrid multiple;
    multiple.update(0,{}); multiple.update(1,{{{10,0,10},true,true},{{11,0,11},true,true}});
    require(multiple.at(ScentGrid::Cell{0,0}) == 2,"Multiple players must add independent scent doses");
}
void gradientsAndHistory() {
    ScentGrid grid;
    grid.update(0,{});
    grid.update(300,{{{10,0,10},true,true}});
    const auto source = ScentGrid::Cell{0,0};
    for (const auto p : {glm::vec3(110,0,10),glm::vec3(-90,0,-90),glm::vec3(10,0,150)}) {
        const auto path = grid.ascent(p);
        require(!path.empty() && path.back() == source,"A stationary source must be found by strict gradient ascent");
        for (std::size_t i = 1; i < path.size(); ++i)
            require(grid.at(path[i]) > grid.at(path[i-1]),"An ascent must not cycle or follow equal-valued cells");
    }
    const auto a = grid.at(ScentGrid::Cell{5,0}), b = grid.at(ScentGrid::Cell{3,4});
    require(std::abs(a-b)/a < .01f,"Equal-distance scent must be approximately independent of grid direction");
    grid.update(301,{{{210,0,10},true,true}});
    require(grid.at(source) > grid.at(ScentGrid::Cell{10,0}),"A long stationary stay must leave stronger scent than a fresh distant dose");
    grid.update(2000,{});
    require(grid.activeCells() == 0,"Abandoned scent must eventually decay below the sparse cutoff");
}
}
int main() {
    try { clockAndEmission(); gradientsAndHistory(); std::cout << "Scent grid diffusion and gradients passed\n"; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
