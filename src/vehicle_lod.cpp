#include "vehicle_lod.hpp"
#include "file_fingerprint.hpp"
#include <json.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace viewer {
VehicleDetail vehicleCameraDetail(float distance, float projectionScale, std::size_t previous, bool enabled) {
    if (!enabled) return {};
    if (!std::isfinite(distance) || distance < 0 || !std::isfinite(projectionScale) || projectionScale <= 0)
        return {}; // Invalid view data must not drop geometry.
    const float effective = distance * std::sqrt(3.0f) / projectionScale;
    constexpr float thresholds[]{20, 50, 100};
    auto level = std::min(previous, std::size_t(3));
    while (level < 3 && effective >= thresholds[level] * 1.1f) ++level;
    while (level > 0 && effective < thresholds[level - 1] * .9f) --level;
    return {level, effective <= 300};
}
std::vector<Model> loadVehicleLods(const std::filesystem::path& directory, const std::string& id) {
    if (!std::filesystem::exists(directory / "lods.json")) return {};
    try {
        std::ifstream stream(directory / "lods.json");
        const auto manifest = nlohmann::json::parse(stream);
        if (manifest.at("version") != 1) throw std::runtime_error("Unsupported vehicle LoD manifest");
        if (!manifest.at("models").contains(id)) return {};
        const auto& entry = manifest.at("models").at(id);
        const auto root = std::filesystem::weakly_canonical(directory);
        const auto resolve = [&](const nlohmann::json& value) {
            const auto text = value.get<std::string>();
            const std::filesystem::path relative(text);
            if (text.empty() || text.find('\\') != std::string::npos || text.find(':') != std::string::npos || relative.is_absolute())
                throw std::runtime_error("Non-portable vehicle LoD path");
            const auto path = std::filesystem::weakly_canonical(root / relative);
            const auto tail = path.lexically_relative(root);
            if (tail.empty() || *tail.begin() == ".." || path.extension() != ".glb")
                throw std::runtime_error("Vehicle LoD path leaves model directory");
            return path;
        };
        if (fileSha256(resolve(entry.at("source"))) != entry.at("source_sha256"))
            throw std::runtime_error("Stale vehicle LoD source fingerprint");
        const auto& levels = entry.at("levels");
        if (!levels.is_array() || levels.size() != 4) throw std::runtime_error("Expected four vehicle LoDs");
        std::vector<Model> result;
        std::size_t previous = std::numeric_limits<std::size_t>::max();
        for (const auto& level : levels) {
            const auto path = resolve(level.at("path"));
            if (fileSha256(path) != level.at("sha256")) throw std::runtime_error("Vehicle LoD fingerprint mismatch");
            auto model = loadModel(path);
            std::size_t triangles = 0;
            for (const auto& draw : model.draws) triangles += model.primitives.at(draw.primitive).indices.size() / 3;
            if (!triangles || triangles > previous || triangles != level.at("triangles").get<std::size_t>())
                throw std::runtime_error("Invalid vehicle LoD triangle count");
            for (int axis = 0; axis < 3; ++axis)
                if (!std::isfinite(model.boundsMin[axis]) || !std::isfinite(model.boundsMax[axis]) ||
                    model.boundsMin[axis] < -5 || model.boundsMax[axis] > 5)
                    throw std::runtime_error("Vehicle LoD is outside the chassis-local frame");
            previous = triangles;
            result.push_back(std::move(model));
        }
        return result;
    } catch (const std::exception& error) {
        std::cerr << "Vehicle LoD warning: " << id << ": " << error.what()
                  << "; using prototype. Rebuild with tools/prepare_vehicle_lods.py\n";
        return {};
    }
}
}
