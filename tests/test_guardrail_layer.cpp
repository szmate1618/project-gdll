#include "guardrail_layer.hpp"
#include "model_merge.hpp"
#include "file_fingerprint.hpp"
#include "collision_world.hpp"

#include <json.hpp>
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using Json = nlohmann::json;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
std::uint32_t word(const std::string& data, std::size_t offset) {
    std::uint32_t result = 0;
    for (int i = 0; i < 4; ++i) result |= std::uint32_t(static_cast<unsigned char>(data.at(offset + i))) << (i * 8);
    return result;
}
void appendWord(std::string& data, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) data += static_cast<char>((value >> (i * 8)) & 255);
}
void write(const std::filesystem::path& path, const Json& document, std::string binary) {
    auto json = document.dump();
    while (json.size() % 4) json += ' ';
    while (binary.size() % 4) binary += '\0';
    std::string data;
    appendWord(data, 0x46546c67); appendWord(data, 2);
    appendWord(data, static_cast<std::uint32_t>(28 + json.size() + binary.size()));
    appendWord(data, static_cast<std::uint32_t>(json.size())); appendWord(data, 0x4e4f534a);
    data += json;
    appendWord(data, static_cast<std::uint32_t>(binary.size())); appendWord(data, 0x004e4942);
    data += binary;
    std::ofstream(path, std::ios::binary).write(data.data(), static_cast<std::streamsize>(data.size()));
}

void test(const std::filesystem::path& root) {
    const auto map = std::filesystem::path(VIEWER_TEST_ASSET), path = root / "guardrails.glb";
    std::ifstream input(map, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(input)), {});
    const auto jsonLength = word(bytes, 12);
    auto document = Json::parse(bytes.substr(20, jsonLength));
    const auto binary = bytes.substr(28 + jsonLength, document["buffers"][0]["byteLength"].get<std::size_t>());
    auto original = viewer::loadModel(map);
    std::size_t triangles = 0;
    for (const auto& draw : original.draws) triangles += original.primitives.at(draw.primitive).indices.size() / 3;
    for (auto& mesh : document["meshes"]) mesh["name"] = "guardrails_fixture";
    for (auto& node : document["nodes"])
        if (node.contains("mesh")) node["name"] = "guardrails_fixture";
    for (auto& material : document["materials"]) {
        material["name"] = "guardrail_metal";
        material["alphaMode"] = "MASK";
        material["doubleSided"] = true;
        material["pbrMetallicRoughness"]["baseColorTexture"] = {{"index", 0}};
    }
    auto& metadata = document["scenes"][document.value("scene", 0)]["extras"];
    metadata = {{"schema", "godollo.guardrails"}, {"version", 1}, {"map_sha256", viewer::fileSha256(map)},
                {"guardrails", {{"ways", 1}, {"length_m", 10}, {"batches", original.draws.size()}, {"triangles", triangles}}}};
    write(path, document, binary);
    auto layer = viewer::loadGuardrailLayer(path, map);
    require(layer.ways == 1 && layer.lengthMeters == 10, "Guardrail metadata did not load");
    require(layer.model.materials[0].minifiedAlphaCutoff == .3f, "Thin guardrails need distant alpha tuning");
    const auto oldDraws = original.draws.size(), oldPrimitives = original.primitives.size();
    const auto oldImages = original.images.size(), oldTextures = original.textures.size(), oldMaterials = original.materials.size();
    const auto sourceImage = layer.model.images[0].rgba;
    const auto sourceTransform = layer.model.draws[0].transform;
    viewer::appendStaticModel(original, std::move(layer.model));
    require(original.draws.size() == oldDraws * 2 && original.draws[oldDraws].primitive == oldPrimitives,
            "Merged layer must offset primitive references while retaining original draws");
    require(original.images[oldImages].rgba == sourceImage, "Merge changed source texture pixels");
    require(original.textures[oldTextures].image == static_cast<int>(oldImages), "Texture image reference not remapped");
    require(original.materials[oldMaterials].baseColorTexture == static_cast<int>(oldTextures), "Material texture reference not remapped");
    require(original.primitives[oldPrimitives].material == static_cast<int>(oldMaterials), "Primitive material reference not remapped");
    require(original.draws[oldDraws].transform == sourceTransform, "Merge applied a scene transform again");
    const auto rejects = [&](Json bad) {
        write(path, bad, binary);
        try { (void)viewer::loadGuardrailLayer(path, map); }
        catch (const std::runtime_error& error) {
            require(std::string(error.what()).find(path.string()) != std::string::npos, "Error omitted layer path");
            return;
        }
        throw std::runtime_error("Invalid guardrail layer accepted");
    };
    auto bad = document; bad["scenes"][0]["extras"]["map_sha256"] = std::string(64, '0'); rejects(bad);
    bad = document; bad["scenes"][0]["extras"]["version"] = 2; rejects(bad);
    bad = document; bad["scenes"][0]["extras"]["guardrails"]["triangles"] = 0; rejects(bad);
    bad = document; bad["materials"][0]["doubleSided"] = false; rejects(bad);
    bad = document; bad["images"][0]["uri"] = "outside.png"; rejects(bad);
    // Empty mapped coverage is valid and must leave existing geometry alone.
    auto empty = document;
    empty["nodes"] = Json::array(); empty["meshes"] = Json::array(); empty["images"] = Json::array();
    empty["scenes"][0]["nodes"] = Json::array();
    empty["scenes"][0]["extras"]["guardrails"] = {{"ways", 0}, {"length_m", 0}, {"batches", 0}, {"triangles", 0}};
    write(path, empty, binary);
    auto emptyLayer = viewer::loadGuardrailLayer(path, map);
    const auto drawCount = original.draws.size();
    viewer::appendStaticModel(original, std::move(emptyLayer.model));
    require(original.draws.size() == drawCount, "Empty layer changed original draws");
}
} // namespace

int main() {
    const auto root = std::filesystem::current_path() /
        ("guardrail-fixtures-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    int result = 0;
    try { test(root); std::cout << "Guardrail loading, fingerprints, texture remapping and empty layers passed\n"; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    std::filesystem::remove_all(root);
    return result;
}
