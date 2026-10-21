#pragma once

#include "zombie_navmesh.hpp"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <vector>

namespace viewer::navmesh_benchmark {

struct Input { navmesh_path::Point start, goal; };
struct RandomInputs {
    std::vector<Input> inputs;
    std::uint64_t startAttempts = 0, goalAttempts = 0;
};
inline constexpr std::uint32_t maximumEndpointAttempts = 1'000'000;

// Use explicitly specified MT19937 output bits, rather than implementation-
// dependent uniform_real_distribution mappings. Each coordinate consumes two
// draws and samples one of 2^53 equally spaced values in [0,1).
inline double randomUnit(std::mt19937& random) {
    const auto high = std::uint64_t(random()) << 21;
    const auto low = std::uint64_t(random()) >> 11;
    return static_cast<double>(high | low) * 0x1p-53;
}

// Rejection sampling makes each endpoint independent and uniform over the
// walkable map area before float32 rounding. The game/replay endpoint precision
// is float32; test that rounded point itself rather than an unrounded precursor.
// No nearest-point snapping or connected-component filtering is performed.
inline RandomInputs randomInputs(const navmesh_path::Mesh& mesh, std::size_t count, std::uint32_t seed) {
    const double lowX=mesh.minimumX(), lowZ=mesh.minimumZ();
    const double highX=mesh.maximumX(), highZ=mesh.maximumZ();
    const double width=highX-lowX, height=highZ-lowZ;
    if (!std::isfinite(width) || !std::isfinite(height) || width<=0 || height<=0 || mesh.polygons().empty())
        throw std::invalid_argument("random endpoint sampling requires a nonempty bounded navmesh");
    std::mt19937 random(seed);
    RandomInputs result;
    result.inputs.reserve(count);
    auto endpoint = [&](std::uint64_t& attempts) {
        for (std::uint32_t attempt=0; attempt<maximumEndpointAttempts; ++attempt) {
            ++attempts;
            const double x=lowX+width*randomUnit(random);
            const double z=lowZ+height*randomUnit(random);
            const navmesh_path::Point point{static_cast<float>(x),static_cast<float>(z)};
            if (point.x>=lowX && point.x<=highX && point.z>=lowZ && point.z<=highZ &&
                mesh.locate(point)!=navmesh_path::noPolygon)
                return point;
        }
        throw std::runtime_error("random endpoint sampling exceeded one million attempts for an endpoint");
    };
    for (std::size_t index=0; index<count; ++index) {
        const auto start=endpoint(result.startAttempts);
        const auto goal=endpoint(result.goalAttempts);
        result.inputs.push_back({start,goal});
    }
    return result;
}

} // namespace viewer::navmesh_benchmark
