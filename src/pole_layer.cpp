#include "pole_layer.hpp"
#include "model_merge.hpp"
#include "file_fingerprint.hpp"

#include <json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <stdexcept>
#include <unordered_set>

namespace viewer {
namespace {
using Json = nlohmann::json;

double number(const Json& value) {
    if (!value.is_number()) throw std::runtime_error("Pole coordinate/dimension must be numeric");
    const double result = value.get<double>();
    if (!std::isfinite(result) || std::abs(result) > 1e7)
        throw std::runtime_error("Pole coordinate/dimension outside finite meter range");
    return result;
}

void validateColumn(const PoleColumn& column) {
    for (int axis = 0; axis < 3; ++axis)
        if (!std::isfinite(column.position[axis]) || std::abs(column.position[axis]) > 1e7f)
            throw std::runtime_error("Invalid pole position");
    if (!std::isfinite(column.height) || column.height < .5f || column.height > 100 ||
        !std::isfinite(column.width) || column.width < .01f || column.width > 10)
        throw std::runtime_error("Invalid pole dimensions");
}

void addColumn(Primitive& batch, const PoleColumn& column, Model& model) {
    const auto p = column.position;
    const float half = column.width * .5f;
    // Bury the foot slightly to hide narrow gaps on sloped ground. The manifest
    // base and above-ground height still refer to the rendered terrain center.
    const glm::vec3 lo(p.x - half, p.y - std::max(.1f, half), p.z - half);
    const glm::vec3 hi(p.x + half, p.y + column.height, p.z + half);
    if (glm::any(glm::lessThanEqual(hi, lo)))
        throw std::runtime_error("Pole dimensions collapse at float32 world coordinates");
    model.boundsMin = glm::min(model.boundsMin, lo);
    model.boundsMax = glm::max(model.boundsMax, hi);
    const std::array<glm::vec3, 8> corners{{
        {lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {lo.x, hi.y, lo.z},
        {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z}}};
    constexpr int faces[6][4] = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 4, 7, 3},
                                  {1, 2, 6, 5}, {0, 1, 5, 4}, {3, 7, 6, 2}};
    const std::array<glm::vec3, 6> normals{{{0, 0, -1}, {0, 0, 1}, {-1, 0, 0},
                                           {1, 0, 0}, {0, -1, 0}, {0, 1, 0}}};
    if (batch.vertices.size() > std::numeric_limits<std::uint32_t>::max() - 24 ||
        batch.indices.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) - 36)
        throw std::length_error("Pole batch exceeds OpenGL index limits");
    for (int face = 0; face < 6; ++face) {
        const auto base = static_cast<std::uint32_t>(batch.vertices.size());
        for (const auto corner : faces[face]) {
            Vertex vertex;
            vertex.position = corners[corner];
            vertex.normal = normals[face];
            batch.vertices.push_back(vertex);
        }
        batch.indices.insert(batch.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
}
} // namespace

Model buildPoleBatches(const std::vector<PoleColumn>& columns) {
    Model model;
    if (columns.empty()) return model;
    model.boundsMin = glm::vec3(std::numeric_limits<float>::max());
    model.boundsMax = glm::vec3(std::numeric_limits<float>::lowest());
    Material grey;
    grey.baseColor = glm::vec4(.55f, .55f, .55f, 1);
    model.materials.push_back(grey);
    std::map<std::pair<int, int>, std::size_t> cells;
    for (const auto& column : columns) {
        validateColumn(column);
        const auto key = std::make_pair(static_cast<int>(std::floor(column.position.x / 128)),
                                        static_cast<int>(std::floor(column.position.z / 128)));
        const auto inserted = cells.emplace(key, model.primitives.size());
        if (inserted.second) {
            model.primitives.emplace_back();
            model.primitives.back().material = 0;
            DrawInstance draw;
            draw.primitive = inserted.first->second;
            draw.name = "poles_" + std::to_string(key.first) + "_" + std::to_string(key.second);
            model.draws.push_back(std::move(draw));
        }
        addColumn(model.primitives[inserted.first->second], column, model);
    }
    return model;
}

PoleLayer loadPoleLayer(const std::filesystem::path& manifest, const std::filesystem::path& map) {
    try {
        std::ifstream stream(manifest);
        if (!stream) throw std::runtime_error("Cannot open pole manifest");
        const auto document = Json::parse(stream);
        if (document.at("schema") != "godollo.pole-instances" ||
            !document.at("version").is_number_integer() || document.at("version") != 1)
            throw std::runtime_error("Unsupported pole schema/version");
        if (document.at("map").at("map_sha256") != fileSha256(map))
            throw std::runtime_error("Pole layer map fingerprint mismatch; regenerate pole placements");
        const auto& columns = document.at("columns");
        if (!columns.is_array()) throw std::runtime_error("columns must be an array");
        PoleLayer layer;
        std::unordered_set<std::string> ids;
        for (const auto& item : columns) {
            PoleColumn column;
            column.id = item.at("id").get<std::string>();
            if (column.id.empty() || !ids.insert(column.id).second)
                throw std::runtime_error("Empty or duplicate pole ID");
            const auto kind = item.at("kind").get<std::string>();
            if (kind != "street_lamp" && kind != "power_pole" && kind != "utility_pole" && kind != "power_tower")
                throw std::runtime_error("Unknown pole kind");
            const auto& position = item.at("position");
            if (!position.is_array() || position.size() != 3)
                throw std::runtime_error("Pole position must contain three numbers");
            for (int axis = 0; axis < 3; ++axis) column.position[axis] = static_cast<float>(number(position[axis]));
            column.height = static_cast<float>(number(item.at("height_m")));
            column.width = static_cast<float>(number(item.at("width_m")));
            validateColumn(column);
            layer.columns.push_back(std::move(column));
        }
        layer.batches = buildPoleBatches(layer.columns);
        return layer;
    } catch (const std::exception& error) {
        throw std::runtime_error(manifest.string() + ": " + error.what());
    }
}

void appendPoleLayer(Model& town, PoleLayer&& layer) {
    appendStaticModel(town, std::move(layer.batches));
}
} // namespace viewer
