#include "bird_assets.hpp"
#include <json.hpp>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace viewer {
BirdAssets loadBirdAssets(const std::filesystem::path& path) {
    try {
        std::ifstream stream(path);
        const auto data = nlohmann::json::parse(stream);
        if (data.at("schema") != "godollo.pigeon-flock" || data.at("version") != 1)
            throw std::runtime_error("Unsupported pigeon asset version");
        BirdAssets result;
        result.duration = data.at("duration").get<float>();
        result.frameCount = data.at("frame_count").get<std::size_t>();
        if (!std::isfinite(result.duration) || result.duration <= 0 || result.duration > 10 ||
            result.frameCount < 2 || result.frameCount > 300)
            throw std::runtime_error("Invalid pigeon animation dimensions");
        result.atlas.width = result.atlas.height = 16;
        result.atlas.rgba = data.at("atlas_rgba").get<std::vector<std::uint8_t>>();
        if (result.atlas.rgba.size() != 16 * 16 * 4 || data.at("levels").size() != 3)
            throw std::runtime_error("Expected a 16px atlas and three pigeon detail levels");
        std::size_t previousTriangles = 100000;
        for (std::size_t level = 0; level < 3; ++level) {
            auto& mesh = result.meshes[level];
            const auto& spec = data.at("levels").at(level);
            const auto uv = spec.at("uv").get<std::vector<std::array<float, 2>>>();
            if (uv.empty() || uv.size() > 10000) throw std::runtime_error("Invalid pigeon vertex count");
            for (const auto& p : uv) {
                if (!std::isfinite(p[0]) || !std::isfinite(p[1])) throw std::runtime_error("Invalid pigeon UV");
                mesh.uv.emplace_back(p[0], p[1]);
            }
            mesh.indices = spec.at("indices").get<std::vector<std::uint32_t>>();
            if (mesh.indices.empty() || mesh.indices.size() % 3 || mesh.indices.size() / 3 >= previousTriangles)
                throw std::runtime_error("Pigeon LoDs must have decreasing triangle counts");
            previousTriangles = mesh.indices.size() / 3;
            for (auto index : mesh.indices)
                if (index >= uv.size()) throw std::runtime_error("Pigeon index outside vertex array");
            const auto& samples = spec.at("samples");
            if (samples.size() != result.frameCount * uv.size()) throw std::runtime_error("Truncated pigeon animation");
            mesh.samples.reserve(samples.size());
            for (const auto& value : samples) {
                const auto p = value.get<std::array<float, 6>>();
                for (float component : p)
                    if (!std::isfinite(component)) throw std::runtime_error("Nonfinite pigeon sample");
                const glm::vec3 position(p[0], p[1], p[2]), normal(p[3], p[4], p[5]);
                if (glm::length(position) > 2 || glm::length(normal) < .01f)
                    throw std::runtime_error("Invalid pigeon sample bounds or normal");
                result.radius = std::max(result.radius, glm::length(position));
                mesh.samples.push_back({glm::vec4(position, 1), glm::vec4(normal, 0)});
            }
        }
        if (result.radius <= 0) throw std::runtime_error("Empty pigeon bounds");
        // Enclose quantized samples and any linear interpolation between them.
        result.radius += .001f;
        return result;
    } catch (const std::exception& error) {
        throw std::runtime_error(path.string() + ": " + error.what());
    }
}

std::size_t birdDetailLevel(float distance, float radius, float projectionY, int viewportHeight) {
    const float pixels = radius * projectionY * viewportHeight / std::max(distance, .001f);
    return pixels >= 48 ? 0 : pixels >= 16 ? 1 : 2;
}
}
