// Offline preparation only: bake the approved GLB once; runtime never skins birds on CPU.
#include "animated_model.hpp"
#include "boid/bird_assets.hpp"
#include "file_fingerprint.hpp"
#include <json.hpp>
#include <meshoptimizer.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

using Json = nlohmann::json;
int main(int argc, char** argv) {
    try {
        if (argc != 3) throw std::runtime_error("Usage: prepare_pigeon_flock SOURCE.glb OUTPUT.json");
        auto asset = viewer::loadIdleModel(argv[1], "Flapping");
        if (asset.model.primitives.size() != 1 || asset.model.images.size() != 1 || asset.frameCount < 2)
            throw std::runtime_error("Expected the approved single-mesh animated pigeon");
        const auto& mesh = asset.model.primitives.front();
        const auto& frames = asset.animation.front().frames;
        const auto& image = asset.model.images.front();
        if (image.width != 16 || image.height != 16 || mesh.indices.size() != 710 * 3)
            throw std::runtime_error("Expected the 710-triangle pigeon with a 16px atlas");
        glm::vec3 low(std::numeric_limits<float>::max()), high(-low);
        for (const auto& sample : frames) {
            low = glm::min(low, glm::vec3(sample.position));
            high = glm::max(high, glm::vec3(sample.position));
        }
        const auto center = (low + high) * .5f;
        const float scale = .65f / (high.x - low.x); // 65cm animated wingspan, meters.
        Json output = {{"schema", "godollo.pigeon-flock"}, {"version", 1},
            {"source_sha256", viewer::fileSha256(argv[1])}, {"clip", "Flapping"},
            {"duration", asset.duration}, {"frame_count", asset.frameCount},
            {"wingspan_m", .65}, {"atlas_rgba", image.rgba}, {"levels", Json::array()}};
        // Pose attributes retain the wing tips through the flap, as well as UV borders.
        std::vector<std::array<float, 11>> attributes(mesh.vertices.size());
        for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
            auto& a = attributes[i];
            a[0] = mesh.vertices[i].uv.x; a[1] = mesh.vertices[i].uv.y;
            for (std::size_t pose = 0; pose < 3; ++pose) {
                const auto p = frames[(pose * asset.frameCount / 3) * mesh.vertices.size() + i].position;
                for (std::size_t axis = 0; axis < 3; ++axis) a[2 + pose * 3 + axis] = p[axis] * scale;
            }
        }
        const float weights[11] = {1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2};
        // Six decimal places keep sub-micrometer positions without writing
        // promoted float32 values as long double-precision decimal strings.
        const auto number = [](float value) { return std::round(double(value) * 1e6) / 1e6; };
        for (const auto target : {710u, 240u, 64u}) {
            auto indices = mesh.indices;
            if (target < 710) {
                const auto count = meshopt_simplifyWithAttributes(indices.data(), mesh.indices.data(), mesh.indices.size(),
                    &mesh.vertices[0].position.x, mesh.vertices.size(), sizeof(viewer::Vertex),
                    attributes[0].data(), sizeof(attributes[0]), weights, 11, nullptr, target * 3, .8f,
                    meshopt_SimplifyPermissive | meshopt_SimplifyRegularize, nullptr);
                indices.resize(count);
            }
            std::vector<std::size_t> used;
            std::vector<std::uint32_t> remap(mesh.vertices.size(), std::numeric_limits<std::uint32_t>::max());
            for (auto& index : indices) {
                if (remap[index] == std::numeric_limits<std::uint32_t>::max()) {
                    remap[index] = static_cast<std::uint32_t>(used.size());
                    used.push_back(index);
                }
                index = remap[index];
            }
            Json level = {{"indices", indices}, {"uv", Json::array()}, {"samples", Json::array()}};
            for (auto index : used) level["uv"].push_back({number(mesh.vertices[index].uv.x), number(mesh.vertices[index].uv.y)});
            for (std::size_t frame = 0; frame < asset.frameCount; ++frame)
                for (auto index : used) {
                    const auto& sample = frames[frame * mesh.vertices.size() + index];
                    const auto position = (glm::vec3(sample.position) - center) * scale;
                    const auto normal = glm::normalize(glm::vec3(sample.normal));
                    level["samples"].push_back({number(position.x), number(position.y), number(position.z),
                        number(normal.x), number(normal.y), number(normal.z)});
                }
            output["levels"].push_back(std::move(level));
            std::cout << "LoD " << output["levels"].size() - 1 << ": " << indices.size() / 3
                      << " triangles, " << used.size() << " vertices\n";
        }
        const std::filesystem::path path(argv[2]), temporary = path.string() + ".tmp";
        { std::ofstream stream(temporary); stream << output.dump() << '\n';
          if (!stream) throw std::runtime_error("Cannot write pigeon descriptor"); }
        const auto validated = viewer::loadBirdAssets(temporary);
        std::filesystem::rename(temporary, path);
        std::cout << "Published " << validated.frameCount << " shared animation frames, radius " << validated.radius << "m\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
