#include "model.hpp"

#include <json.hpp>

#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Json = nlohmann::json;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

bool near(float a, float b) { return std::abs(a - b) < 1e-5f; }

void appendUnsigned(std::vector<unsigned char>& bytes, std::uint32_t value, int size) {
    for (int i = 0; i < size; ++i) bytes.push_back(static_cast<unsigned char>((value >> (8 * i)) & 255));
}

void appendFloat(std::vector<unsigned char>& bytes, float value) {
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    appendUnsigned(bytes, bits, 4);
}

struct Fixtures {
    std::filesystem::path directory = std::filesystem::current_path() /
        ("model-test-fixtures-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Fixtures() { std::filesystem::create_directories(directory); }
    ~Fixtures() { std::error_code error; std::filesystem::remove_all(directory, error); }

    std::filesystem::path write(const std::string& name, Json document,
                                 const std::vector<unsigned char>& bytes) {
        const auto bufferName = name + ".bin";
        document["buffers"] = Json::array({{{"byteLength", bytes.size()}, {"uri", bufferName}}});
        std::ofstream binary(directory / bufferName, std::ios::binary);
        binary.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        binary.close();
        const auto path = directory / (name + ".gltf");
        std::ofstream json(path);
        json << document.dump(2);
        json.close();
        require(binary.good() && json.good(), "Unable to write loader test fixtures");
        return path;
    }
};

Json baseDocument() {
    return {
        {"asset", {{"version", "2.0"}}},
        {"scene", 0},
        {"scenes", Json::array({{{"nodes", {0}}}})},
        {"nodes", Json::array({{{"mesh", 0}}})},
        {"meshes", Json::array({{{"primitives", Json::array({{
            {"attributes", {{"POSITION", 0}}}, {"indices", 1}
        }})}}})}
    };
}

void expectError(const std::filesystem::path& path, const std::string& fragment) {
    try {
        (void)viewer::loadModel(path);
    } catch (const std::runtime_error& error) {
        const std::string message = error.what();
        require(message.find(path.string()) != std::string::npos, "Error must identify the input path: " + message);
        require(message.find(fragment) != std::string::npos, "Missing expected error '" + fragment + "': " + message);
        return;
    }
    throw std::runtime_error("Expected loading to fail for " + path.string());
}

void testIndexWidthsAndHierarchy(Fixtures& files) {
    for (const int width : {1, 2, 4}) {
        std::vector<unsigned char> bytes;
        for (const glm::vec3 position : {glm::vec3(0, 0, 0), glm::vec3(1, 0, 0), glm::vec3(0, 1, 0)}) {
            appendFloat(bytes, position.x);
            appendFloat(bytes, position.y);
            appendFloat(bytes, position.z);
            // Interleaved normalized RGBA8 data verifies accessor byteOffset and byteStride.
            bytes.insert(bytes.end(), {255, 128, 0, 255});
        }
        for (const int index : {0, 1, 2}) appendUnsigned(bytes, index, width);
        auto document = baseDocument();
        document["bufferViews"] = Json::array({
            {{"buffer", 0}, {"byteOffset", 0}, {"byteLength", 48}, {"byteStride", 16}},
            {{"buffer", 0}, {"byteOffset", 48}, {"byteLength", 3 * width}}
        });
        document["accessors"] = Json::array({
            {{"bufferView", 0}, {"componentType", 5126}, {"count", 3}, {"type", "VEC3"}},
            {{"bufferView", 1}, {"componentType", width == 1 ? 5121 : width == 2 ? 5123 : 5125}, {"count", 3}, {"type", "SCALAR"}},
            {{"bufferView", 0}, {"byteOffset", 12}, {"componentType", 5121}, {"normalized", true}, {"count", 3}, {"type", "VEC4"}}
        });
        document["meshes"][0]["primitives"][0]["attributes"]["COLOR_0"] = 2;
        document["scenes"][0]["nodes"] = {0, 2};
        document["nodes"] = Json::array({
            {{"translation", {10, 20, 30}}, {"children", {1}}},
            {{"mesh", 0}, {"scale", {2, 3, 4}}},
            {{"mesh", 0}, {"matrix", {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -5, 0, 0, 1}}}
        });
        const auto model = viewer::loadModel(files.write("indexed" + std::to_string(width), document, bytes));
        require(model.primitives.size() == 1 && model.draws.size() == 2, "Mesh instances should share one primitive");
        require(model.primitives[0].indices == std::vector<std::uint32_t>({0, 1, 2}), "Index width decoded incorrectly");
        require(near(model.boundsMin.x, -5) && near(model.boundsMin.y, 0) && near(model.boundsMin.z, 0), "Incorrect transformed minimum bounds");
        require(near(model.boundsMax.x, 12) && near(model.boundsMax.y, 23) && near(model.boundsMax.z, 30), "Incorrect transformed maximum bounds");
        const auto& vertex = model.primitives[0].vertices[0];
        require(near(vertex.color.r, 1) && near(vertex.color.g, 128.0f / 255.0f) && near(vertex.color.a, 1), "Normalized interleaved color decoded incorrectly");
        require(near(vertex.normal.z, 1), "Missing normals should be generated with triangle winding");

        if (width == 1) {
            auto truncated = document;
            truncated["bufferViews"][0]["byteLength"] = 8;
            expectError(files.write("truncated", truncated, bytes), "beyond its bufferView");
            auto badIndices = bytes;
            badIndices.back() = 7;
            expectError(files.write("bad-index", document, badIndices), "exceeds vertex count");
            auto cycle = document;
            cycle["nodes"][1]["children"] = {0};
            expectError(files.write("cycle", cycle, bytes), "cycle");
            auto required = document;
            required["extensionsRequired"] = {"KHR_draco_mesh_compression"};
            expectError(files.write("draco", required, bytes), "KHR_draco_mesh_compression");
        }
    }
}

void testSparseAndPrimitiveModes(Fixtures& files) {
    std::vector<unsigned char> bytes{0, 1, 2, 0};
    for (const glm::vec3 position : {glm::vec3(0, 0, 0), glm::vec3(1, 0, 0), glm::vec3(0, 1, 0)}) {
        appendFloat(bytes, position.x);
        appendFloat(bytes, position.y);
        appendFloat(bytes, position.z);
    }
    auto document = baseDocument();
    document["bufferViews"] = Json::array({
        {{"buffer", 0}, {"byteOffset", 0}, {"byteLength", 3}},
        {{"buffer", 0}, {"byteOffset", 4}, {"byteLength", 36}}
    });
    document["accessors"] = Json::array({{
        {"componentType", 5126}, {"count", 3}, {"type", "VEC3"},
        {"sparse", {{"count", 3}, {"indices", {{"bufferView", 0}, {"componentType", 5121}}}, {"values", {{"bufferView", 1}}}}}
    }});
    document["meshes"][0]["primitives"][0].erase("indices");
    for (const int mode : {4, 5, 6}) {
        document["meshes"][0]["primitives"][0]["mode"] = mode;
        const auto model = viewer::loadModel(files.write("sparse" + std::to_string(mode), document, bytes));
        require(model.primitives[0].indices == std::vector<std::uint32_t>({0, 1, 2}), "Non-indexed triangle/strip/fan conversion failed");
        require(near(model.boundsMax.x, 1) && near(model.boundsMax.y, 1), "Sparse positions were not applied");
    }
    bytes[2] = 3;
    expectError(files.write("bad-sparse-index", document, bytes), "Sparse indices");
}

void testImportedAsset(const std::filesystem::path& path, bool requireTexture) {
    const auto model = viewer::loadModel(path);
    require(!model.draws.empty() && !model.primitives.empty(), "Test scene has no geometry");
    std::size_t triangles = 0;
    for (const auto& primitive : model.primitives) {
        require(!primitive.vertices.empty() && !primitive.indices.empty(), "Empty primitive loaded");
        triangles += primitive.indices.size() / 3;
        for (const auto& vertex : primitive.vertices) {
            require(std::isfinite(vertex.normal.x) && near(glm::length(vertex.normal), 1), "Normals must be finite unit vectors");
        }
    }
    if (requireTexture) {
        require(!model.images.empty() && !model.textures.empty(), "Test scene should exercise embedded texture decoding");
        require(model.images[0].rgba.size() == static_cast<std::size_t>(model.images[0].width) * model.images[0].height * 4, "Image must be RGBA8");
    }
    std::cout << "Loaded " << path << ": " << model.primitives.size() << " primitives, "
              << model.draws.size() << " instances, " << triangles << " triangles, "
              << model.images.size() << " images\n";
}
}  // namespace

int main(int argc, char** argv) {
    try {
        require(argc >= 2, "Usage: test_model test.glb [scene.glb]");
        Fixtures files;
        testIndexWidthsAndHierarchy(files);
        testSparseAndPrimitiveModes(files);
        expectError(files.directory / "missing.glb", "does not exist");
        testImportedAsset(argv[1], true);
        if (argc >= 3) testImportedAsset(argv[2], false);
        std::cout << "Model loader checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Model loader test failed: " << error.what() << '\n';
        return 1;
    }
}
