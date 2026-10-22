#pragma once

#include "potential_surface_guidance.hpp"
#include <filesystem>
#include <memory>
#include <string>

namespace viewer::potential_surface_benchmark {
struct LoadedGuidance {
    std::unique_ptr<potential_surface::Guidance> field;
    std::string sha256;
    double loadMilliseconds=0;
};

const char* guidanceName(potential_surface::GuidanceMode);
LoadedGuidance loadGuidance(const std::filesystem::path&,const std::string& sceneSha256,
                           const potential_surface::Config&,potential_surface::GuidanceMode);
}
