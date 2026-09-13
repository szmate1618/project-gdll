#include "tree_layer.hpp"

#include <json.hpp>
#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace viewer {
namespace {
using Json = nlohmann::json;

[[noreturn]] void fail(const std::string& message) { throw std::runtime_error(message); }

std::string stringValue(const Json& value, const std::string& field) {
    if (!value.is_string() || value.get_ref<const std::string&>().empty())
        fail(field + " must be a nonempty string");
    return value.get<std::string>();
}

std::uint64_t countValue(const Json& value, const std::string& field) {
    if (!value.is_number_integer() || (!value.is_number_unsigned() && value.get<std::int64_t>() < 0))
        fail(field + " must be a nonnegative integer");
    return value.get<std::uint64_t>();
}

double numberValue(const Json& value, const std::string& field) {
    if (!value.is_number()) fail(field + " must be a finite number");
    const double result = value.get<double>();
    if (!std::isfinite(result) || std::abs(result) > std::numeric_limits<float>::max())
        fail(field + " must be finite and within float32 range");
    return result;
}

glm::dvec3 vectorValue(const Json& value, const std::string& field) {
    if (!value.is_array() || value.size() != 3) fail(field + " must contain three numbers");
    glm::dvec3 result;
    for (int axis = 0; axis < 3; ++axis) result[axis] = numberValue(value[axis], field);
    return result;
}

std::string expectedHash(const Json& value, const std::string& field) {
    auto hash = stringValue(value, field);
    if (hash.size() != 64 || !std::all_of(hash.begin(), hash.end(), [](unsigned char c) { return std::isxdigit(c); }))
        fail(field + " must be a 64-character SHA-256 hex digest");
    std::transform(hash.begin(), hash.end(), hash.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return hash;
}

std::string fileHash(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) fail("Cannot open fingerprinted file '" + path.string() + "'");
    using Context = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;
    Context context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!context || EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1)
        fail("Cannot initialize SHA-256 for '" + path.string() + "'");
    std::array<char, 65536> buffer{};
    while (stream) {
        stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        if (stream.gcount() > 0 && EVP_DigestUpdate(context.get(), buffer.data(), static_cast<std::size_t>(stream.gcount())) != 1)
            fail("Cannot hash file '" + path.string() + "'");
    }
    if (!stream.eof()) fail("Cannot finish reading fingerprinted file '" + path.string() + "'");
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int size = 0;
    if (EVP_DigestFinal_ex(context.get(), digest.data(), &size) != 1 || size != 32)
        fail("Cannot finalize SHA-256 for '" + path.string() + "'");
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (unsigned int i = 0; i < size; ++i) {
        result.push_back(hex[digest[i] >> 4]);
        result.push_back(hex[digest[i] & 15]);
    }
    return result;
}

bool near(double first, double second, double absolute = 1e-5) {
    return std::abs(first - second) <= absolute +
        8 * std::numeric_limits<float>::epsilon() * std::max(std::abs(first), std::abs(second));
}

glm::mat4 matrixValue(const Json& value) {
    if (!value.is_array() || value.size() != 16) fail("matrix must contain 16 column-major numbers");
    glm::mat4 matrix;
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            matrix[column][row] = static_cast<float>(numberValue(value[column * 4 + row], "matrix"));
    for (int column = 0; column < 3; ++column)
        if (std::abs(matrix[column][3]) > 1e-7f) fail("matrix must be affine, without perspective");
    if (std::abs(matrix[3][3] - 1.0f) > 1e-7f) fail("matrix must have affine homogeneous coordinate 1");
    // The vertical axis must remain +Y. Horizontal raster axes may rotate,
    // reflect, and shear, so an ordinary position/scale reconstruction is wrong.
    if (matrix[1][1] <= 0 || std::abs(matrix[1][0]) > 1e-7f || std::abs(matrix[1][2]) > 1e-7f ||
        std::abs(matrix[0][1]) > 1e-7f || std::abs(matrix[2][1]) > 1e-7f)
        fail("matrix must keep the trunk upright along positive Y");
    const glm::dmat3 linear(matrix);
    const double product = glm::length(linear[0]) * glm::length(linear[1]) * glm::length(linear[2]);
    if (!(product > 0) || std::abs(glm::determinant(linear)) <= product * 1e-8)
        fail("matrix is singular or has a collapsed footprint");
    return matrix;
}

void validateModel(const Model& model) {
    if (model.draws.empty()) fail("tree asset has no active geometry");
    for (const auto& draw : model.draws) {
        const auto& primitive = model.primitives.at(draw.primitive);
        if (primitive.material < 0 || static_cast<std::size_t>(primitive.material) >= model.materials.size())
            fail("tree asset requires explicit MASK, double-sided, unlit materials");
        const auto& material = model.materials[static_cast<std::size_t>(primitive.material)];
        if (material.alphaMode != "MASK" || !material.doubleSided || !material.unlit)
            fail("tree asset materials must be MASK, double-sided and unlit for mirrored instance batches");
    }
}

void computeBounds(TreeInstance& instance, const Model& model, const Json& declaration) {
    const glm::dvec3 lo(model.boundsMin), hi(model.boundsMax);
    const glm::dmat3 linear(instance.transform);
    const glm::dvec3 center = linear * ((lo + hi) * .5) + glm::dvec3(instance.transform[3]);
    glm::dvec3 radius(0.0);
    const auto extent = (hi - lo) * .5;
    for (int column = 0; column < 3; ++column)
        radius += glm::abs(linear[column]) * extent[column];
    const glm::dvec3 minimum = center - radius, maximum = center + radius;
    if (!declaration.is_null()) {
        if (!declaration.is_object()) fail("bounds must contain min/max vectors");
        const auto statedMin = vectorValue(declaration.at("min"), "bounds.min");
        const auto statedMax = vectorValue(declaration.at("max"), "bounds.max");
        for (int axis = 0; axis < 3; ++axis)
            if (statedMin[axis] > statedMax[axis] || !near(statedMin[axis], minimum[axis], .01) ||
                !near(statedMax[axis], maximum[axis], .01))
                fail("bounds disagree with the transformed tree asset geometry");
    }
    for (int axis = 0; axis < 3; ++axis) {
        // Pad for float32 model-matrix multiplication and round outward. The
        // bounds used for culling never depend on the JSON's declared bounds.
        const double margin = 1e-4 + 8 * std::numeric_limits<float>::epsilon() *
            (std::abs(center[axis]) + radius[axis]);
        const double lower = minimum[axis] - margin, upper = maximum[axis] + margin;
        if (!std::isfinite(lower) || !std::isfinite(upper) ||
            std::max(std::abs(lower), std::abs(upper)) >= std::numeric_limits<float>::max())
            fail("transformed tree bounds exceed float32 range");
        instance.boundsMin[axis] = std::nextafter(static_cast<float>(lower), -std::numeric_limits<float>::infinity());
        instance.boundsMax[axis] = std::nextafter(static_cast<float>(upper), std::numeric_limits<float>::infinity());
    }
}

void validateCounts(const Json& document) {
    if (!document.contains("counts")) return;
    const auto& counts = document.at("counts");
    if (!counts.is_object()) fail("counts must be an object");
    for (auto item = counts.begin(); item != counts.end(); ++item)
        countValue(item.value(), "counts." + item.key());
    const auto actual = static_cast<std::uint64_t>(document.at("instances").size());
    if (counts.contains("instances") && countValue(counts.at("instances"), "counts.instances") != actual)
        fail("counts.instances does not match the instance array");
    if (counts.contains("input")) {
        auto remaining = countValue(counts.at("input"), "counts.input");
        if (remaining < actual) fail("counts.input is smaller than the instance array");
        remaining -= actual;
        for (const auto* field : {"outside_area", "below_min_score"}) {
            if (counts.contains(field)) {
                const auto value = countValue(counts.at(field), std::string("counts.") + field);
                if (value > remaining) fail("counts filtered totals exceed counts.input");
                remaining -= value;
            }
        }
        if (counts.contains("outside_area") && counts.contains("below_min_score") && remaining != 0)
            fail("counts.input does not equal retained and filtered totals");
    }
}
} // namespace

TreeLayer loadTreeLayer(const std::filesystem::path& manifest, const std::filesystem::path& map) {
    try {
        TreeLayer layer;
        layer.source = std::filesystem::absolute(manifest).lexically_normal();
        std::ifstream stream(layer.source);
        if (!stream) fail("Cannot open instance manifest");
        const auto document = Json::parse(stream);
        if (!document.is_object() || document.value("schema", std::string{}) != "godollo.tree-instances")
            fail("Unsupported tree instance schema");
        if (!document.contains("version") || !document.at("version").is_number_integer() || document.at("version") != 1)
            fail("Unsupported tree instance version; expected 1");
        const auto& instances = document.at("instances");
        if (!instances.is_array()) fail("instances must be an array");
        validateCounts(document);
        const auto expectedMap = expectedHash(document.at("map").at("map_sha256"), "map.map_sha256");
        if (fileHash(map) != expectedMap)
            fail("Selected map SHA-256 mismatch for '" + map.string() + "'; regenerate tree instances for this map");
        // The manifest's recorded map path is informational: matching bytes
        // are sufficient when a map and its placement file have been relocated.
        const auto& assets = document.at("assets");
        if (!assets.is_object()) fail("assets must be an object keyed by asset ID");
        std::unordered_map<std::string, std::size_t> assetIndices;
        std::unordered_map<std::string, std::pair<std::string, std::size_t>> loadedPaths;
        for (auto item = assets.begin(); item != assets.end(); ++item) {
            const auto& id = item.key();
            try {
                if (id.empty()) fail("asset ID must not be empty");
                const auto& record = item.value();
                auto path = std::filesystem::path(stringValue(record.at("path"), "asset path"));
                if (path.is_relative()) path = layer.source.parent_path() / path;
                path = std::filesystem::weakly_canonical(std::filesystem::absolute(path));
                const auto expected = expectedHash(record.at("sha256"), "asset sha256");
                auto loaded = loadedPaths.find(path.string());
                if (loaded == loadedPaths.end()) {
                    const auto actual = fileHash(path);
                    if (actual != expected) fail("Asset SHA-256 mismatch for '" + path.string() + "'; regenerate instances or restore the matching asset");
                    auto model = loadModel(path);
                    validateModel(model);
                    const auto index = layer.assets.size();
                    layer.assets.push_back({id, path, std::move(model)});
                    loaded = loadedPaths.emplace(path.string(), std::make_pair(actual, index)).first;
                }
                if (loaded->second.first != expected) fail("Asset SHA-256 mismatch for reused file '" + path.string() + "'");
                assetIndices.emplace(id, loaded->second.second);
            } catch (const std::exception& error) {
                fail("Asset '" + id + "': " + error.what());
            }
        }
        layer.instances.reserve(instances.size());
        std::unordered_set<std::string> identifiers;
        identifiers.reserve(instances.size());
        for (std::size_t index = 0; index < instances.size(); ++index) {
            try {
                const auto& record = instances[index];
                TreeInstance instance;
                instance.id = stringValue(record.at("id"), "instance id");
                if (!identifiers.insert(instance.id).second) fail("duplicate instance ID '" + instance.id + "'");
                const auto assetId = stringValue(record.at("asset"), "instance asset");
                const auto asset = assetIndices.find(assetId);
                if (asset == assetIndices.end()) fail("unknown asset '" + assetId + "'");
                instance.asset = asset->second;
                instance.transform = matrixValue(record.at("matrix"));
                instance.dimensions = glm::vec3(vectorValue(record.at("dimensions_m"), "dimensions_m"));
                if (instance.dimensions.x <= 0 || instance.dimensions.y <= 0 || instance.dimensions.z <= 0)
                    fail("dimensions_m must be strictly positive in float32");
                if (record.contains("position")) {
                    const auto position = vectorValue(record.at("position"), "position");
                    for (int axis = 0; axis < 3; ++axis)
                        if (!near(position[axis], instance.transform[3][axis]))
                            fail("position disagrees with the authoritative matrix translation");
                }
                computeBounds(instance, layer.assets[instance.asset].model, record.value("bounds", Json{}));
                layer.instances.push_back(std::move(instance));
            } catch (const std::exception& error) {
                fail("Instance " + std::to_string(index) + ": " + error.what());
            }
        }
        return layer;
    } catch (const std::exception& error) {
        throw std::runtime_error("Failed to load tree layer '" + manifest.string() + "': " + error.what());
    }
}
} // namespace viewer
