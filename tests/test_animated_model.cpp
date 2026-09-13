#include "animated_model.hpp"

#include <json.hpp>

#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using Json = nlohmann::json;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

bool near(float a, float b) { return std::abs(a - b) < 2e-4f; }

struct Fixture {
    Json document{
        {"asset", {{"version", "2.0"}}}, {"scene", 0},
        {"scenes", Json::array({{{"nodes", {0}}}})},
        {"nodes", Json::array({{{"mesh", 0}}})},
        {"bufferViews", Json::array()}, {"accessors", Json::array()},
        {"animations", Json::array()}
    };
    std::vector<unsigned char> bytes;

    int accessor(const std::vector<float>& values, int width, const std::string& type,
                   int componentType = 5126, bool normalized = false) {
        const auto offset = bytes.size();
        for (float value : values) {
            if (componentType == 5121) bytes.push_back(static_cast<unsigned char>(value));
            else {
                std::uint32_t bits;
                std::memcpy(&bits, &value, sizeof(bits));
                for (int byte = 0; byte < 4; ++byte) bytes.push_back(static_cast<unsigned char>((bits >> (8 * byte)) & 255));
            }
        }
        const auto view = document["bufferViews"].size();
        document["bufferViews"].push_back({{"buffer", 0}, {"byteOffset", offset}, {"byteLength", bytes.size() - offset}});
        const int index = static_cast<int>(document["accessors"].size());
        document["accessors"].push_back({{"bufferView", view}, {"componentType", componentType},
            {"count", values.size() / width}, {"type", type}, {"normalized", normalized}});
        while (bytes.size() % 4) bytes.push_back(0);
        return index;
    }

    Fixture() {
        const int positions = accessor({0, 0, 0, 1, 0, 0, 0, 1, 0}, 3, "VEC3");
        document["meshes"] = Json::array({{{"primitives", Json::array({{{"attributes", {{"POSITION", positions}}}}})}}});
    }

    void track(const std::string& name, int node, const std::string& path,
                 const std::vector<float>& times, const std::vector<float>& values,
                 const std::string& interpolation = "LINEAR") {
        if (document["animations"].empty()) {
            document["animations"].push_back({{"name", name}, {"channels", Json::array()}, {"samplers", Json::array()}});
        }
        auto& animation = document["animations"][0];
        const int input = accessor(times, 1, "SCALAR");
        const int output = accessor(values, path == "rotation" ? 4 : 3, path == "rotation" ? "VEC4" : "VEC3");
        const auto sampler = animation["samplers"].size();
        animation["samplers"].push_back({{"input", input}, {"output", output}, {"interpolation", interpolation}});
        animation["channels"].push_back({{"sampler", sampler}, {"target", {{"node", node}, {"path", path}}}});
    }

    void skin() {
        auto& primitive = document["meshes"][0]["primitives"][0];
        primitive["attributes"]["JOINTS_0"] = accessor({0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 4, "VEC4", 5121);
        primitive["attributes"]["WEIGHTS_0"] = accessor({255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0}, 4, "VEC4", 5121, true);
        const int inverseBind = accessor({1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -2, 0, 0, 1}, 16, "MAT4");
        document["skins"] = Json::array({{{"joints", {2}}, {"inverseBindMatrices", inverseBind}}});
        document["nodes"] = Json::array({
            {{"translation", {10, 0, 0}}, {"children", {1, 2, 3}}},
            {{"mesh", 0}, {"skin", 0}, {"translation", {7, 0, 0}}},
            {{"translation", {2, 0, 0}}},
            {{"mesh", 0}, {"translation", {0, 0, 5}}}
        });
    }
};

struct Files {
    std::filesystem::path directory = std::filesystem::current_path() /
        ("animated-model-fixtures-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Files() { std::filesystem::create_directories(directory); }
    ~Files() { std::error_code error; std::filesystem::remove_all(directory, error); }
    std::filesystem::path write(const std::string& name, Fixture fixture) const {
        const std::string buffer = name + ".bin";
        fixture.document["buffers"] = Json::array({{{"uri", buffer}, {"byteLength", fixture.bytes.size()}}});
        std::ofstream binary(directory / buffer, std::ios::binary);
        binary.write(reinterpret_cast<const char*>(fixture.bytes.data()), static_cast<std::streamsize>(fixture.bytes.size()));
        binary.close();
        const auto path = directory / (name + ".gltf");
        std::ofstream json(path);
        json << fixture.document.dump(2);
        json.close();
        require(binary.good() && json.good(), "Could not write fixture");
        return path;
    }
};

void expectError(const std::filesystem::path& path, const std::string& fragment,
                   const std::string& clip = "") {
    try { (void)viewer::loadIdleModel(path, clip); }
    catch (const std::runtime_error& error) {
        const std::string message = error.what();
        require(message.find(path.string()) != std::string::npos, "Error must include source path");
        require(message.find(fragment) != std::string::npos, "Expected '" + fragment + "' in: " + message);
        return;
    }
    throw std::runtime_error("Expected animated model load failure: " + fragment);
}

const viewer::AnimationVertex& vertex(const viewer::AnimatedModel& model, std::size_t frame,
                                       std::size_t index = 0, std::size_t primitive = 0) {
    return model.animation[primitive].frames[frame * model.model.primitives[primitive].vertices.size() + index];
}

void testSkinAndHierarchy(Files& files) {
    Fixture fixture;
    fixture.skin();
    fixture.track("Armature|IDLE", 0, "translation", {2, 4}, {10, 0, 0, 10, 2, 0});
    fixture.track("", 2, "translation", {2.5f, 3.5f}, {2, 0, 0, 4, 0, 0});
    const auto model = viewer::loadIdleModel(files.write("skinned", fixture));
    require(model.frameCount == 60 && near(model.duration, 2), "Sample count and clip time range");
    require(model.clipName == "Armature|IDLE", "Idle selection should ignore case");
    require(model.model.primitives.size() == 2 && model.model.draws.size() == 2, "Shared meshes must flatten per draw");
    require(model.animation[0].frames.size() == 180 && model.animation[1].frames.size() == 180, "Frame-major buffer layout");
    require(near(vertex(model, 0).position.x, 10), "Skin root and inverse bind must each apply once; mesh transform must cancel");
    require(near(vertex(model, 30).position.x, 11) && near(vertex(model, 30).position.y, 1), "Parent and joint animation must compose");
    require(near(vertex(model, 59).position.x, 12) && near(vertex(model, 59).position.y, 59.0f / 30.0f), "Shorter channels clamp and duplicate endpoint is excluded");
    require(near(vertex(model, 30, 0, 1).position.x, 10) && near(vertex(model, 30, 0, 1).position.y, 1) &&
        near(vertex(model, 30, 0, 1).position.z, 5), "Unskinned geometry must still follow animated ancestors");
    require(near(model.model.primitives[0].vertices[0].position.x, 10), "Static vertex data must hold frame zero");
    for (const auto& draw : model.model.draws) require(draw.transform == glm::mat4(1), "Baked scene draws must be identity");
    for (const auto& primitive : model.animation) for (const auto& v : primitive.frames) {
        require(near(glm::length(glm::vec3(v.normal)), 1), "Baked normals must be unit length");
        for (int c = 0; c < 3; ++c) require(v.position[c] >= model.model.boundsMin[c] && v.position[c] <= model.model.boundsMax[c], "Animation bounds must enclose every frame");
    }
    for (const auto& warning : model.model.warnings) require(warning.find("not supported") == std::string::npos &&
        warning.find("not played") == std::string::npos, "Static loader warnings must be removed after baking");

    auto bad = fixture;
    const int joints = bad.document["meshes"][0]["primitives"][0]["attributes"]["JOINTS_0"];
    const int view = bad.document["accessors"][joints]["bufferView"];
    const auto offset = bad.document["bufferViews"][view]["byteOffset"].get<std::size_t>();
    bad.bytes[offset] = 1;
    expectError(files.write("bad-joint", bad), "joint index is out of range");
    bad = fixture;
    bad.document["accessors"][joints]["count"] = 2;
    expectError(files.write("bad-count", bad), "count does not match vertices");
    bad = fixture;
    const int weights = bad.document["meshes"][0]["primitives"][0]["attributes"]["WEIGHTS_0"];
    const int weightView = bad.document["accessors"][weights]["bufferView"];
    bad.bytes[bad.document["bufferViews"][weightView]["byteOffset"].get<std::size_t>()] = 0;
    expectError(files.write("zero-weights", bad), "positive finite sum");

    Fixture rotation;
    rotation.skin();
    rotation.track("idle", 2, "rotation", {0, 1}, {0, 0, 0, 1, 0, 0, 1, 0});
    const auto rotated = viewer::loadIdleModel(files.write("skinned-rotation", rotation));
    require(near(vertex(rotated, 15, 1).position.x, 12) && near(vertex(rotated, 15, 1).position.y, -1),
        "Skin rotation must rotate about the inverse-bind joint, then apply the scene root");

    Fixture blended;
    blended.skin();
    blended.document["nodes"][0]["children"].push_back(4);
    blended.document["nodes"].push_back({{"translation", {4, 0, 0}}});
    const int inverseBind = blended.accessor({
        1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -2, 0, 0, 1,
        1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -4, 0, 0, 1
    }, 16, "MAT4");
    blended.document["skins"][0] = {{"joints", {2, 4}}, {"inverseBindMatrices", inverseBind}};
    auto& attributes = blended.document["meshes"][0]["primitives"][0]["attributes"];
    attributes["JOINTS_0"] = blended.accessor({0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0}, 4, "VEC4", 5121);
    attributes["WEIGHTS_0"] = blended.accessor({128, 128, 0, 0, 128, 128, 0, 0, 128, 128, 0, 0}, 4, "VEC4", 5121, true);
    blended.track("idle", 4, "translation", {0, 1}, {4, 0, 0, 6, 0, 0});
    const auto mixed = viewer::loadIdleModel(files.write("blended-skin", blended));
    require(near(vertex(mixed, 0).position.x, 10) && near(vertex(mixed, 15).position.x, 10.5f),
        "Skinning must blend multiple joints and renormalize quantized weights");
}

void testInterpolation(Files& files) {
    Fixture rotation;
    rotation.track("idle", 0, "rotation", {0, 1}, {0, 0, 0, 1, 0, 0, 1, 0});
    auto model = viewer::loadIdleModel(files.write("rotation", rotation));
    require(near(vertex(model, 15, 1).position.x, 0) && near(vertex(model, 15, 1).position.y, 1), "LINEAR rotations must use quaternion slerp");
    require(near(vertex(model, 15, 1).normal.z, 1), "Rotated normal");

    Fixture step;
    step.track("idle", 0, "translation", {0, 0.5f, 1}, {0, 0, 0, 5, 0, 0, 0, 0, 0}, "STEP");
    model = viewer::loadIdleModel(files.write("step", step));
    require(near(vertex(model, 14).position.x, 0) && near(vertex(model, 15).position.x, 5) &&
        near(vertex(model, 29).position.x, 5), "STEP must change at exact keys without blending");

    Fixture cubic;
    // Two equal endpoint values with asymmetric tangents produce x=0.5 at t=1;
    // the two-second interval must scale the outgoing tangent in Hermite interpolation.
    cubic.track("idle", 0, "translation", {0, 2}, {
        0, 0, 0,  0, 0, 0,  2, 0, 0,
        0, 0, 0,  0, 0, 0,  0, 0, 0
    }, "CUBICSPLINE");
    model = viewer::loadIdleModel(files.write("cubic", cubic));
    require(near(vertex(model, 30).position.x, 0.5f), "CUBICSPLINE tangents must be scaled by the key interval");

    Fixture cubicRotation;
    cubicRotation.track("idle", 0, "rotation", {0, 1}, {
        0, 0, 0, 0,  0, 0, 0, 1,  0, 0, 0, 0,
        0, 0, 0, 0,  0, 0, 1, 0,  0, 0, 0, 0
    }, "CUBICSPLINE");
    model = viewer::loadIdleModel(files.write("cubic-rotation", cubicRotation));
    require(near(vertex(model, 15, 1).position.x, 0) && near(vertex(model, 15, 1).position.y, 1),
        "CUBICSPLINE quaternion values must be normalized after interpolation");

    Fixture normal;
    normal.document["nodes"][0]["scale"] = {2, 1, 3};
    const int normals = normal.accessor({1, 1, 0, 1, 1, 0, 1, 1, 0}, 3, "VEC3");
    normal.document["meshes"][0]["primitives"][0]["attributes"]["NORMAL"] = normals;
    normal.track("idle", 0, "translation", {0, 1}, {0, 0, 0, 0, 1, 0});
    model = viewer::loadIdleModel(files.write("normal", normal));
    require(near(vertex(model, 0).normal.x, 1 / std::sqrt(5.0f)) && near(vertex(model, 0).normal.y, 2 / std::sqrt(5.0f)), "Normals need inverse transpose under nonuniform scale");

    Fixture reflection;
    reflection.document["nodes"][0]["scale"] = {-1, 1, 1};
    reflection.track("idle", 0, "translation", {0, 1}, {0, 0, 0, 0, 1, 0});
    model = viewer::loadIdleModel(files.write("reflection", reflection));
    const auto& primitive = model.model.primitives[0];
    const auto a = primitive.vertices[primitive.indices[0]].position;
    const auto b = primitive.vertices[primitive.indices[1]].position;
    const auto c = primitive.vertices[primitive.indices[2]].position;
    require(glm::dot(glm::cross(b - a, c - a), primitive.vertices[0].normal) > 0,
        "Baked reflected geometry must retain front-face winding consistent with normals");
}

void testValidation(Files& files) {
    Fixture missing;
    expectError(files.write("missing", missing), "No animation containing 'idle'");
    Fixture fixture;
    fixture.track("Idle_A", 0, "translation", {0, 1}, {0, 0, 0, 0, 1, 0});
    fixture.document["animations"].push_back(fixture.document["animations"][0]);
    fixture.document["animations"][1]["name"] = "Idle_B";
    const auto path = files.write("ambiguous", fixture);
    expectError(path, "Multiple idle animations");
    require(viewer::loadIdleModel(path, "idle_b").clipName == "Idle_B", "Exact clip override must disambiguate");
    expectError(path, "clip not found", "walk");
    fixture.document["animations"][0]["name"] = "idle2_alert";
    fixture.document["animations"][1]["name"] = "Idle";
    require(viewer::loadIdleModel(files.write("preferred-idle", fixture)).clipName == "Idle",
        "Exact idle must be preferred over named idle variants");
    fixture.document["animations"][1]["name"] = "Armature|Idle";
    require(viewer::loadIdleModel(files.write("prefixed-idle", fixture)).clipName == "Armature|Idle",
        "An exporter-prefixed idle token must be preferred over variants");

    Fixture malformed;
    malformed.track("idle", 0, "translation", {0, 1}, {0, 0, 0, 0, 1, 0});
    auto bad = malformed;
    const int input = bad.document["animations"][0]["samplers"][0]["input"];
    const int inputView = bad.document["accessors"][input]["bufferView"];
    bad.document["bufferViews"][inputView]["byteLength"] = 4;
    expectError(files.write("truncated-keys", bad), "beyond its bufferView");
    bad = malformed;
    bad.document["animations"][0]["channels"][0]["target"]["path"] = "weights";
    expectError(files.write("morph", bad), "Unsupported animation channel");
    bad = malformed;
    bad.document["animations"][0]["channels"].push_back(bad.document["animations"][0]["channels"][0]);
    expectError(files.write("duplicate-channel", bad), "Duplicate animation target");
    bad = malformed;
    bad.document["animations"][0]["samplers"][0]["interpolation"] = "BEZIER";
    expectError(files.write("bad-interpolation", bad), "Unsupported animation interpolation");
    Fixture times;
    times.track("idle", 0, "translation", {1, 1}, {0, 0, 0, 0, 1, 0});
    expectError(files.write("duplicate-time", times), "strictly increasing");
}
}  // namespace

int main(int argc, char** argv) {
    try {
        Files files;
        testSkinAndHierarchy(files);
        testInterpolation(files);
        testValidation(files);
        if (argc > 1) {
            const auto model = viewer::loadIdleModel(argv[1], argc > 2 ? argv[2] : "");
            std::size_t vertices = 0;
            for (const auto& primitive : model.model.primitives) vertices += primitive.vertices.size();
            float maximumDisplacement = 0;
            for (std::size_t p = 0; p < model.animation.size(); ++p) {
                const auto count = model.model.primitives[p].vertices.size();
                for (std::size_t frame = 1; frame < model.frameCount; ++frame) {
                    for (std::size_t v = 0; v < count; ++v) maximumDisplacement = std::max(maximumDisplacement,
                        glm::length(glm::vec3(vertex(model, frame, v, p).position - vertex(model, 0, v, p).position)));
                }
            }
            std::cout << "Loaded " << model.clipName << ": " << model.frameCount << " frames, " << model.duration
                      << " s, " << vertices << " vertices, height " << model.model.boundsMax.y - model.model.boundsMin.y
                      << " m, maximum vertex displacement " << maximumDisplacement << " m\n";
        }
        std::cout << "Animated model tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
