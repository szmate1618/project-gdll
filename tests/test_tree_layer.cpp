#include "tree_layer.hpp"

#include <json.hpp>
#include <openssl/evp.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Json = nlohmann::json;

void require(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }
bool near(float a, float b) { return std::abs(a - b) < .01f; }

void append32(std::vector<unsigned char>& bytes, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<unsigned char>((value >> (i * 8)) & 255));
}

void appendFloat(std::vector<unsigned char>& bytes, float value) {
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    append32(bytes, bits);
}

std::string hash(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    const std::string contents((std::istreambuf_iterator<char>(input)), {});
    std::array<unsigned char, EVP_MAX_MD_SIZE> bytes{};
    unsigned int size = 0;
    require(EVP_Digest(contents.data(), contents.size(), bytes.data(), &size, EVP_sha256(), nullptr) == 1 && size == 32,
            "Unable to hash fixture");
    std::string digest;
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned int i = 0; i < size; ++i) { digest += hex[bytes[i] >> 4]; digest += hex[bytes[i] & 15]; }
    return digest;
}

struct Fixtures {
    const std::filesystem::path directory = std::filesystem::current_path() /
        ("tree-layer-fixtures-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const std::filesystem::path map = directory / "relocated-map.glb";
    const std::filesystem::path asset = directory / "assets/tree.glb";
    const std::filesystem::path manifest = directory / "layers/trees.instances.json";

    Fixtures() {
        std::filesystem::create_directories(asset.parent_path());
        std::filesystem::create_directories(manifest.parent_path());
        std::ofstream(map) << "Map bytes only: the tree layer checks its identity, not its geometry.";
        writeAsset();
    }
    ~Fixtures() { std::error_code error; std::filesystem::remove_all(directory, error); }

    void writeAsset(const std::string& mode = "MASK", bool doubleSided = true, bool unlit = true) const {
        std::vector<unsigned char> binary;
        for (const auto point : {glm::vec3(-1, 0, -1), glm::vec3(1, 0, -1), glm::vec3(0, 2, 1)})
            for (int axis = 0; axis < 3; ++axis) appendFloat(binary, point[axis]);
        for (const std::uint32_t index : {0, 1, 2}) append32(binary, index);
        Json material = {{"alphaMode", mode}, {"doubleSided", doubleSided}};
        if (unlit) material["extensions"] = {{"KHR_materials_unlit", Json::object()}};
        Json primitive = {{"attributes", {{"POSITION", 0}}}, {"indices", 1}, {"material", 0}};
        Json document = {
            {"asset", {{"version", "2.0"}}}, {"scene", 0},
            {"scenes", Json::array({{{"nodes", {0}}}})},
            {"nodes", Json::array({{{"mesh", 0}}})},
            {"meshes", Json::array({{{"name", "test-tree"}, {"primitives", Json::array({primitive, primitive, primitive, primitive})}}})},
            {"materials", Json::array({material})},
            {"buffers", Json::array({{{"byteLength", binary.size()}}})},
            {"bufferViews", Json::array({{{"buffer", 0}, {"byteOffset", 0}, {"byteLength", 36}},
                                         {{"buffer", 0}, {"byteOffset", 36}, {"byteLength", 12}}})},
            {"accessors", Json::array({{{"bufferView", 0}, {"componentType", 5126}, {"count", 3}, {"type", "VEC3"}},
                                       {{"bufferView", 1}, {"componentType", 5125}, {"count", 3}, {"type", "SCALAR"}}})}
        };
        if (unlit) document["extensionsUsed"] = {"KHR_materials_unlit"};
        auto encoded = document.dump();
        while (encoded.size() % 4) encoded += ' ';
        std::vector<unsigned char> glb;
        append32(glb, 0x46546c67); append32(glb, 2);
        append32(glb, static_cast<std::uint32_t>(12 + 8 + encoded.size() + 8 + binary.size()));
        append32(glb, static_cast<std::uint32_t>(encoded.size())); append32(glb, 0x4e4f534a);
        glb.insert(glb.end(), encoded.begin(), encoded.end());
        append32(glb, static_cast<std::uint32_t>(binary.size())); append32(glb, 0x004e4942);
        glb.insert(glb.end(), binary.begin(), binary.end());
        std::ofstream output(asset, std::ios::binary);
        output.write(reinterpret_cast<const char*>(glb.data()), static_cast<std::streamsize>(glb.size()));
        require(output.good(), "Failed to write fixture GLB");
    }

    Json document() const {
        // Shear and mirror are deliberate: scale is not the authoritative transform.
        Json instance = {{"id", "one"}, {"asset", "tree"}, {"position", {10, 20, 30}},
            {"scale", {999, 999, 999}}, {"dimensions_m", {4, 6, 4}},
            {"matrix", {2, 0, 0, 0, 0, 3, 0, 0, 1, 0, -2, 0, 10, 20, 30, 1}},
            {"bounds", {{"min", {7, 20, 28}}, {"max", {13, 26, 32}}}}};
        Json second = instance;
        second["id"] = "two";
        return {{"schema", "godollo.tree-instances"}, {"version", 1},
                {"map", {{"path", "some-old-location/map.glb"}, {"map_sha256", hash(map)}}},
                {"assets", {{"tree", {{"path", "../assets/tree.glb"}, {"sha256", hash(asset)}}}}},
                {"instances", Json::array({instance, second})},
                {"counts", {{"input", 5}, {"instances", 2}, {"outside_area", 2}, {"below_min_score", 1}}}};
    }

    void write(const Json& document) const {
        std::ofstream output(manifest);
        output << document.dump(2);
        require(output.good(), "Failed to write fixture manifest");
    }
};

void expectError(const Fixtures& files, const Json& document, const std::string& fragment) {
    files.write(document);
    try { (void)viewer::loadTreeLayer(files.manifest, files.map); }
    catch (const std::runtime_error& error) {
        const std::string message = error.what();
        require(message.find(files.manifest.string()) != std::string::npos, "Error omitted manifest path: " + message);
        require(message.find(fragment) != std::string::npos, "Expected '" + fragment + "' in error: " + message);
        return;
    }
    throw std::runtime_error("Expected invalid manifest to fail with: " + fragment);
}

void testRelativeSharingAndBounds(Fixtures& files) {
    auto document = files.document();
    files.write(document);
    auto layer = viewer::loadTreeLayer(files.manifest, files.map);
    require(layer.assets.size() == 1 && layer.instances.size() == 2, "Instances must share one loaded Model");
    require(layer.assets[0].model.primitives.size() == 4 && layer.assets[0].model.draws.size() == 4,
            "Four tree primitives should not be copied per instance");
    require(layer.assets[0].path == std::filesystem::canonical(files.asset), "Asset path was not resolved relative to manifest");
    const auto& instance = layer.instances[0];
    require(instance.asset == layer.instances[1].asset, "Shared asset index is wrong");
    require(instance.transform[2][0] == 1 && instance.transform[2][2] == -2 && instance.transform[1][1] == 3,
            "Authoritative shear/reflection matrix was reconstructed or transposed");
    require(near(instance.boundsMin.x, 7) && near(instance.boundsMax.y, 26), "Transformed bounds are incorrect");
    for (const auto& primitive : layer.assets[0].model.primitives)
        for (const auto& vertex : primitive.vertices) {
            const glm::vec3 point(instance.transform * glm::vec4(vertex.position, 1));
            for (int axis = 0; axis < 3; ++axis)
                require(point[axis] >= instance.boundsMin[axis] && point[axis] <= instance.boundsMax[axis],
                        "Computed bounds must enclose actual rendered geometry");
        }
    // Aliased asset IDs pointing at one physical file still share the Model.
    document["assets"]["alias"] = document["assets"]["tree"];
    document["instances"][1]["asset"] = "alias";
    files.write(document);
    layer = viewer::loadTreeLayer(files.manifest, files.map);
    require(layer.assets.size() == 1 && layer.instances[0].asset == layer.instances[1].asset,
            "The same asset file should not be loaded twice under aliases");
    document["instances"][0].erase("bounds");
    document["instances"][0].erase("position");
    document.erase("counts");
    files.write(document);
    layer = viewer::loadTreeLayer(files.manifest, files.map);
    require(near(layer.instances[0].boundsMax.z, 32), "Bounds must be derived when omitted from JSON");
}

void testMalformedManifests(Fixtures& files) {
    const auto valid = files.document();
    auto bad = valid; bad["version"] = 2; expectError(files, bad, "version");
    bad = valid; bad["version"] = 1.0; expectError(files, bad, "version");
    bad = valid; bad["schema"] = "other"; expectError(files, bad, "schema");
    bad = valid; bad["assets"].erase("tree"); expectError(files, bad, "unknown asset");
    bad = valid; bad["instances"][0]["asset"] = "missing"; expectError(files, bad, "unknown asset");
    bad = valid; bad["instances"][1]["id"] = "one"; expectError(files, bad, "duplicate instance ID");
    bad = valid; bad["instances"][0]["id"] = ""; expectError(files, bad, "nonempty string");
    bad = valid; bad["counts"]["instances"] = 3; expectError(files, bad, "counts.instances");
    bad = valid; bad["counts"]["outside_area"] = -1; expectError(files, bad, "nonnegative integer");
    bad = valid; bad["counts"]["input"] = 6; expectError(files, bad, "filtered totals");
    bad = valid; bad["map"]["map_sha256"] = std::string(64, '0'); expectError(files, bad, "map SHA-256 mismatch");
    bad = valid; bad["assets"]["tree"]["sha256"] = std::string(64, '0'); expectError(files, bad, "Asset SHA-256 mismatch");
    bad = valid; bad["assets"]["tree"]["sha256"] = "bad"; expectError(files, bad, "64-character");
    bad = valid; bad["assets"]["tree"]["path"] = "missing.glb"; expectError(files, bad, "missing.glb");
    bad = valid; bad["instances"][0]["matrix"][0] = nullptr; expectError(files, bad, "finite number");
    bad = valid; bad["instances"][0]["matrix"][0] = 1e100; expectError(files, bad, "float32 range");
    bad = valid; bad["instances"][0]["matrix"][0] = 0; expectError(files, bad, "singular");
    bad = valid; bad["instances"][0]["matrix"][3] = 1; expectError(files, bad, "affine");
    bad = valid; bad["instances"][0]["matrix"][15] = 0; expectError(files, bad, "affine");
    bad = valid; bad["instances"][0]["matrix"][4] = .1; expectError(files, bad, "upright");
    bad = valid; bad["instances"][0]["matrix"][5] = -1; expectError(files, bad, "upright");
    bad = valid; bad["instances"][0]["dimensions_m"][1] = 0; expectError(files, bad, "positive");
    bad = valid; bad["instances"][0]["position"][0] = 0; expectError(files, bad, "position disagrees");
    bad = valid; bad["instances"][0]["bounds"]["max"][1] = 100; expectError(files, bad, "bounds disagree");
    bad = valid; bad["instances"][0]["matrix"] = {1, 2}; expectError(files, bad, "16 column-major");
    // JSON itself cannot represent NaN/Infinity: malformed numeric tokens fail parsing.
    auto encoded = valid.dump();
    const auto start = encoded.find("\"matrix\":[") + std::string("\"matrix\":[").size();
    encoded.replace(start, 1, "NaN");
    std::ofstream(files.manifest) << encoded;
    bool rejected = false;
    try { (void)viewer::loadTreeLayer(files.manifest, files.map); }
    catch (const std::runtime_error& error) { rejected = std::string(error.what()).find("parse_error") != std::string::npos; }
    require(rejected, "Nonfinite JSON matrix tokens must fail parsing");
}

void testMaterialAndHashFailures(Fixtures& files) {
    files.writeAsset("BLEND"); expectError(files, files.document(), "MASK");
    files.writeAsset("MASK", false); expectError(files, files.document(), "double-sided");
    files.writeAsset("MASK", true, false); expectError(files, files.document(), "unlit");
    files.writeAsset();
    const auto original = files.document();
    std::ofstream(files.map, std::ios::app) << "changed map";
    expectError(files, original, "map SHA-256 mismatch");
    auto valid = files.document();
    std::ofstream(files.asset, std::ios::binary | std::ios::app) << "changed asset";
    expectError(files, valid, "Asset SHA-256 mismatch");
    files.writeAsset();
}

void testEmptyLayer(Fixtures& files) {
    auto document = files.document();
    document["instances"] = Json::array();
    document["counts"] = {{"input", 0}, {"instances", 0}, {"outside_area", 0}, {"below_min_score", 0}};
    files.write(document);
    const auto layer = viewer::loadTreeLayer(files.manifest, files.map);
    require(layer.instances.empty(), "Valid empty layer should load");
}
} // namespace

int main(int argc, char** argv) {
    try {
        Fixtures files;
        testRelativeSharingAndBounds(files);
        testMalformedManifests(files);
        testMaterialAndHashFailures(files);
        testEmptyLayer(files);
        if (argc == 3) {
            const auto layer = viewer::loadTreeLayer(argv[1], argv[2]);
            std::cout << "Loaded real tree layer: " << layer.instances.size() << " instances, " << layer.assets.size() << " assets\n";
        }
        std::cout << "Tree layer loader tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Tree layer test failed: " << error.what() << '\n';
        return 1;
    }
}
