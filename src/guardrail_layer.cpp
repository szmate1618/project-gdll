#include "guardrail_layer.hpp"
#include "file_fingerprint.hpp"

#include <json.hpp>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <stdexcept>

namespace viewer {
namespace {
using Json = nlohmann::json;

Json readDocument(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    std::array<unsigned char, 20> header{};
    if (!stream.read(reinterpret_cast<char*>(header.data()), header.size()))
        throw std::runtime_error("Truncated guardrail GLB header");
    const auto word = [&](int offset) {
        return std::uint32_t(header[offset]) | (std::uint32_t(header[offset + 1]) << 8) |
               (std::uint32_t(header[offset + 2]) << 16) | (std::uint32_t(header[offset + 3]) << 24);
    };
    const auto length = word(12);
    if (word(0) != 0x46546c67 || word(4) != 2 || word(16) != 0x4e4f534a ||
        word(8) != std::filesystem::file_size(path) || length % 4 ||
        length > 16 * 1024 * 1024 || std::uint64_t(length) + 20 > word(8))
        throw std::runtime_error("Invalid guardrail GLB JSON chunk");
    std::string json(length, '\0');
    if (!stream.read(json.data(), length)) throw std::runtime_error("Truncated guardrail GLB JSON");
    return Json::parse(json);
}

std::size_t count(const Json& value) {
    if (!value.is_number_integer() || value.get<double>() < 0 || value.get<double>() > 10000000)
        throw std::runtime_error("Invalid guardrail count");
    return value.get<std::size_t>();
}
} // namespace

GuardrailLayer loadGuardrailLayer(const std::filesystem::path& path, const std::filesystem::path& map) {
    try {
        const auto document = readDocument(path);
        const auto& metadata = document.at("scenes").at(document.value("scene", 0)).at("extras");
        if (metadata.at("schema") != "godollo.guardrails" ||
            !metadata.at("version").is_number_integer() || metadata.at("version") != 1)
            throw std::runtime_error("Unsupported guardrail schema/version");
        if (metadata.at("map_sha256") != fileSha256(map))
            throw std::runtime_error("Guardrail map fingerprint mismatch; regenerate the layer");
        if (!document.value("skins", Json::array()).empty() || !document.value("animations", Json::array()).empty())
            throw std::runtime_error("Guardrail layer must be static");
        for (const auto* key : {"images", "buffers"})
            for (const auto& item : document.value(key, Json::array()))
                if (item.contains("uri")) throw std::runtime_error("Guardrail layer must be self-contained");
        const auto& stats = metadata.at("guardrails");
        GuardrailLayer layer;
        layer.ways = count(stats.at("ways"));
        if (!stats.at("length_m").is_number()) throw std::runtime_error("Invalid guardrail length");
        layer.lengthMeters = stats.at("length_m").get<double>();
        if (!std::isfinite(layer.lengthMeters) || layer.lengthMeters < 0)
            throw std::runtime_error("Invalid guardrail length");
        const auto batches = count(stats.at("batches")), expectedTriangles = count(stats.at("triangles"));
        if (batches == 0) {
            if (layer.ways || expectedTriangles || layer.lengthMeters != 0 ||
                !document.value("meshes", Json::array()).empty())
                throw std::runtime_error("Empty guardrail layer has inconsistent counts");
            return layer;
        }
        layer.model = loadModel(path);
        if (layer.model.draws.size() != batches) throw std::runtime_error("Guardrail batch count mismatch");
        std::size_t triangles = 0;
        for (const auto& draw : layer.model.draws) {
            if (draw.name.rfind("guardrails_", 0) != 0) throw std::runtime_error("Unexpected guardrail draw");
            const auto& mesh = layer.model.primitives.at(draw.primitive);
            const auto& material = layer.model.materials.at(static_cast<std::size_t>(mesh.material));
            if (material.alphaMode != "MASK" || !material.doubleSided || material.baseColorTexture < 0)
                throw std::runtime_error("Guardrails require double-sided textured alpha masks");
            triangles += mesh.indices.size() / 3;
        }
        if (triangles != expectedTriangles) throw std::runtime_error("Guardrail triangle count mismatch");
        return layer;
    } catch (const std::exception& error) {
        throw std::runtime_error(path.string() + ": " + error.what());
    }
}
} // namespace viewer
