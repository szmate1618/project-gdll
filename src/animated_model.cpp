#include "animated_model.hpp"

#define TINYGLTF_NO_STB_IMAGE_WRITE
#include <tiny_gltf.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

namespace viewer {
namespace {

[[noreturn]] void fail(const std::string& message) { throw std::runtime_error(message); }

template <class T>
const T& at(const std::vector<T>& values, int index, const char* description) {
    if (index < 0 || static_cast<std::size_t>(index) >= values.size()) {
        fail(std::string("Invalid ") + description + " index " + std::to_string(index));
    }
    return values[static_cast<std::size_t>(index)];
}

std::size_t product(std::size_t a, std::size_t b) {
    if (b && a > std::numeric_limits<std::size_t>::max() / b) fail("Animation buffer size overflow");
    return a * b;
}

float checkedFloat(double value) {
    const auto result = static_cast<float>(value);
    if (!std::isfinite(result)) fail("Non-finite animation data");
    return result;
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

int componentBytes(int type) {
    switch (type) {
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: return 1;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: return 2;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
        case TINYGLTF_COMPONENT_TYPE_FLOAT: return 4;
        default: fail("Unsupported animation accessor component type");
    }
}

std::uint32_t readUnsigned(const unsigned char* data, int bytes) {
    std::uint32_t result = 0;
    for (int i = 0; i < bytes; ++i) result |= static_cast<std::uint32_t>(data[i]) << (8 * i);
    return result;
}

double component(const unsigned char* data, int type, bool normalized) {
    const auto bits = readUnsigned(data, componentBytes(type));
    if (type == TINYGLTF_COMPONENT_TYPE_FLOAT) {
        float value;
        std::memcpy(&value, &bits, sizeof(value));
        return checkedFloat(value);
    }
    if (!normalized) return bits;
    if (type == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) return bits / 255.0;
    if (type == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) return bits / 65535.0;
    return bits / 4294967295.0;
}

const unsigned char* range(const tinygltf::Model& source, int viewIndex,
                           std::size_t offset, std::size_t count,
                           std::size_t stride, std::size_t packed) {
    const auto& view = at(source.bufferViews, viewIndex, "animation bufferView");
    const auto& bytes = at(source.buffers, view.buffer, "animation buffer").data;
    if (view.byteOffset > bytes.size() || view.byteLength > bytes.size() - view.byteOffset) {
        fail("Animation bufferView extends beyond its buffer");
    }
    if (stride < packed || offset > view.byteLength) fail("Invalid animation accessor offset or stride");
    if (count) {
        const auto tail = product(count - 1, stride);
        const auto remaining = view.byteLength - offset;
        if (tail > remaining || packed > remaining - tail) fail("Animation accessor extends beyond its bufferView");
    }
    return bytes.empty() ? nullptr : bytes.data() + view.byteOffset + offset;
}

struct Accessor {
    std::size_t count = 0;
    int width = 0;
    std::vector<double> values;
    double operator()(std::size_t i, int c) const { return values[i * width + c]; }
    glm::vec4 vector(std::size_t i) const {
        glm::vec4 result(0.0f);
        for (int c = 0; c < std::min(width, 4); ++c) result[c] = checkedFloat((*this)(i, c));
        return result;
    }
};

Accessor readAccessor(const tinygltf::Model& source, int index, int expectedType) {
    const auto& input = at(source.accessors, index, "animation accessor");
    if (input.type != expectedType) fail("Incorrect animation accessor shape");
    const int width = expectedType == TINYGLTF_TYPE_MAT4 ? 16 :
        expectedType == TINYGLTF_TYPE_VEC4 ? 4 : expectedType == TINYGLTF_TYPE_VEC3 ? 3 : 1;
    const int bytes = componentBytes(input.componentType);
    const auto packed = static_cast<std::size_t>(width * bytes);
    Accessor output{input.count, width, std::vector<double>(product(input.count, width), 0.0)};
    const auto fill = [&](std::size_t destination, const unsigned char* data) {
        for (int c = 0; c < width; ++c) {
            output.values[destination * width + c] = component(data + c * bytes, input.componentType, input.normalized);
        }
    };
    if (input.bufferView >= 0) {
        const auto& view = at(source.bufferViews, input.bufferView, "animation bufferView");
        const auto stride = view.byteStride ? view.byteStride : packed;
        if (stride % bytes) fail("Animation accessor stride is misaligned");
        const auto* data = range(source, input.bufferView, input.byteOffset, input.count, stride, packed);
        for (std::size_t i = 0; i < input.count; ++i) fill(i, data + i * stride);
    } else if (!input.sparse.isSparse) {
        fail("Animation accessor has neither a bufferView nor sparse data");
    }
    if (input.sparse.isSparse) {
        const auto& sparse = input.sparse;
        if (sparse.count < 0 || static_cast<std::size_t>(sparse.count) > input.count) fail("Invalid sparse animation count");
        const int type = sparse.indices.componentType;
        if (type != TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE && type != TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT &&
            type != TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) fail("Sparse animation indices must be unsigned integers");
        const auto indexBytes = static_cast<std::size_t>(componentBytes(type));
        if (at(source.bufferViews, sparse.indices.bufferView, "sparse indices").byteStride ||
            at(source.bufferViews, sparse.values.bufferView, "sparse values").byteStride) {
            fail("Sparse animation bufferViews cannot have strides");
        }
        const auto* indices = range(source, sparse.indices.bufferView, sparse.indices.byteOffset,
                                     sparse.count, indexBytes, indexBytes);
        const auto* values = range(source, sparse.values.bufferView, sparse.values.byteOffset,
                                    sparse.count, packed, packed);
        std::uint32_t previous = 0;
        for (int i = 0; i < sparse.count; ++i) {
            const auto destination = readUnsigned(indices + i * indexBytes, static_cast<int>(indexBytes));
            if (destination >= input.count || (i && destination <= previous)) fail("Invalid sparse animation index");
            previous = destination;
            fill(destination, values + i * packed);
        }
    }
    return output;
}

void requireFloat(const tinygltf::Model& source, int index) {
    const auto& accessor = at(source.accessors, index, "animation accessor");
    if (accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT || accessor.normalized) {
        fail("Animation keys and inverse bind matrices must use non-normalized FLOAT accessors");
    }
}

struct Pose {
    glm::vec3 translation{0.0f};
    glm::vec3 scale{1.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::mat4 matrix{1.0f};
    bool hasMatrix = false;
    glm::mat4 transform() const {
        return hasMatrix ? matrix : glm::translate(glm::mat4(1.0f), translation) *
            glm::mat4_cast(rotation) * glm::scale(glm::mat4(1.0f), scale);
    }
};

glm::quat quaternion(const glm::vec4& values) {
    const glm::quat result(values.w, values.x, values.y, values.z);
    if (!std::isfinite(glm::length(result)) || glm::length(result) < 1e-12f) fail("Animation quaternion has zero or invalid length");
    return glm::normalize(result);
}

Pose nodePose(const tinygltf::Node& node) {
    Pose pose;
    const auto vector = [](const std::vector<double>& values, int count, const char* name) {
        if (values.size() != static_cast<std::size_t>(count)) fail(std::string("Invalid node ") + name);
        glm::vec4 result(0.0f);
        for (int c = 0; c < count; ++c) result[c] = checkedFloat(values[c]);
        return result;
    };
    if (!node.matrix.empty()) {
        if (node.matrix.size() != 16 || !node.translation.empty() || !node.rotation.empty() || !node.scale.empty()) {
            fail("Node matrix must have 16 values and cannot coexist with TRS");
        }
        pose.hasMatrix = true;
        for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) pose.matrix[c][r] = checkedFloat(node.matrix[c * 4 + r]);
        if (pose.matrix[0][3] || pose.matrix[1][3] || pose.matrix[2][3] || pose.matrix[3][3] != 1) fail("Node matrix must be affine");
    }
    if (!node.translation.empty()) pose.translation = vector(node.translation, 3, "translation");
    if (!node.scale.empty()) pose.scale = vector(node.scale, 3, "scale");
    if (!node.rotation.empty()) pose.rotation = quaternion(vector(node.rotation, 4, "rotation"));
    return pose;
}

struct Track {
    int node = -1;
    std::string path;
    std::string interpolation;
    Accessor times;
    Accessor values;

    glm::vec4 sample(float time) const {
        const bool cubic = interpolation == "CUBICSPLINE";
        const auto value = [&](std::size_t key) { return values.vector(cubic ? key * 3 + 1 : key); };
        if (time <= times(0, 0)) return value(0);
        if (time >= times(times.count - 1, 0)) return value(times.count - 1);
        const auto upper = std::upper_bound(times.values.begin(), times.values.end(), time);
        const auto next = static_cast<std::size_t>(upper - times.values.begin());
        const auto previous = next - 1;
        if (interpolation == "STEP") return value(previous);
        const float delta = checkedFloat(times(next, 0) - times(previous, 0));
        const float u = (time - checkedFloat(times(previous, 0))) / delta;
        const auto a = value(previous), b = value(next);
        if (cubic) {
            const float u2 = u * u, u3 = u2 * u;
            return (2 * u3 - 3 * u2 + 1) * a + (u3 - 2 * u2 + u) * delta * values.vector(previous * 3 + 2) +
                (-2 * u3 + 3 * u2) * b + (u3 - u2) * delta * values.vector(next * 3);
        }
        if (path == "rotation") {
            const auto q = glm::slerp(quaternion(a), quaternion(b), u);
            return {q.x, q.y, q.z, q.w};
        }
        return glm::mix(a, b, u);
    }
};

std::vector<Track> readTracks(const tinygltf::Model& source, const tinygltf::Animation& clip,
                              float& start, float& duration) {
    std::vector<Track> tracks;
    std::set<std::pair<int, std::string>> targets;
    start = std::numeric_limits<float>::max();
    float end = std::numeric_limits<float>::lowest();
    for (const auto& channel : clip.channels) {
        const auto& node = at(source.nodes, channel.target_node, "animation target node");
        if (channel.target_path != "translation" && channel.target_path != "rotation" && channel.target_path != "scale") {
            fail("Unsupported animation channel: " + channel.target_path + " (morph animation is not supported)");
        }
        if (!node.matrix.empty()) fail("Animation targets a node with a matrix instead of TRS");
        if (!targets.insert({channel.target_node, channel.target_path}).second) fail("Duplicate animation target channel");
        const auto& sampler = at(clip.samplers, channel.sampler, "animation sampler");
        requireFloat(source, sampler.input);
        requireFloat(source, sampler.output);
        Track track{channel.target_node, channel.target_path, sampler.interpolation.empty() ? "LINEAR" : sampler.interpolation,
            readAccessor(source, sampler.input, TINYGLTF_TYPE_SCALAR),
            readAccessor(source, sampler.output, channel.target_path == "rotation" ? TINYGLTF_TYPE_VEC4 : TINYGLTF_TYPE_VEC3)};
        if (track.interpolation != "LINEAR" && track.interpolation != "STEP" && track.interpolation != "CUBICSPLINE") {
            fail("Unsupported animation interpolation: " + track.interpolation);
        }
        if (!track.times.count || track.values.count != product(track.times.count, track.interpolation == "CUBICSPLINE" ? 3 : 1)) {
            fail("Animation key/value counts do not match");
        }
        for (std::size_t i = 0; i < track.times.count; ++i) {
            if (track.times(i, 0) < 0 || (i && track.times(i, 0) <= track.times(i - 1, 0))) {
                fail("Animation times must be nonnegative and strictly increasing");
            }
            if (track.path == "rotation") {
                (void)quaternion(track.values.vector(track.interpolation == "CUBICSPLINE" ? i * 3 + 1 : i));
            }
        }
        start = std::min(start, checkedFloat(track.times(0, 0)));
        end = std::max(end, checkedFloat(track.times(track.times.count - 1, 0)));
        tracks.push_back(std::move(track));
    }
    duration = end - start;
    if (tracks.empty() || !std::isfinite(duration) || duration <= 0) fail("Idle animation must have channels and positive duration");
    return tracks;
}

struct Skin {
    std::vector<int> joints;
    std::vector<glm::mat4> inverseBind;
};

Skin readSkin(const tinygltf::Model& source, int index) {
    const auto& input = at(source.skins, index, "skin");
    if (input.joints.empty()) fail("Skin has no joints");
    Skin result{input.joints, std::vector<glm::mat4>(input.joints.size(), glm::mat4(1.0f))};
    std::set<int> unique;
    for (const int joint : input.joints) {
        at(source.nodes, joint, "skin joint");
        if (!unique.insert(joint).second) fail("Skin has duplicate joints");
    }
    if (input.inverseBindMatrices >= 0) {
        requireFloat(source, input.inverseBindMatrices);
        const auto data = readAccessor(source, input.inverseBindMatrices, TINYGLTF_TYPE_MAT4);
        if (data.count != input.joints.size()) fail("Inverse bind matrix count does not match skin joints");
        for (std::size_t i = 0; i < data.count; ++i) {
            for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) result.inverseBind[i][c][r] = checkedFloat(data(i, c * 4 + r));
            const auto& matrix = result.inverseBind[i];
            if (matrix[0][3] || matrix[1][3] || matrix[2][3] || matrix[3][3] != 1) fail("Inverse bind matrix must be affine");
        }
    }
    return result;
}

struct Weights {
    std::array<std::uint32_t, 8> joints{};
    std::array<float, 8> weights{};
};

struct Binding {
    int node = -1;
    int skin = -1;
    std::vector<Weights> vertices;
};

std::vector<std::string> nameTokens(const std::string& name) {
    std::vector<std::string> result;
    std::string token;
    for (const unsigned char character : name) {
        if (std::isalnum(character)) token.push_back(static_cast<char>(std::tolower(character)));
        else if (!token.empty()) {
            result.push_back(std::move(token));
            token.clear();
        }
    }
    if (!token.empty()) result.push_back(std::move(token));
    return result;
}

bool hasToken(const std::vector<std::string>& tokens, const char* value) {
    return std::find(tokens.begin(), tokens.end(), value) != tokens.end();
}

int ragdollPartForName(const std::string& name) {
    const auto tokens = nameTokens(name);
    const bool left = hasToken(tokens, "left") || hasToken(tokens, "l");
    const bool right = hasToken(tokens, "right") || hasToken(tokens, "r");
    const bool arm = hasToken(tokens, "arm") || hasToken(tokens, "upperarm") || hasToken(tokens, "forearm") ||
                     hasToken(tokens, "lowerarm");
    const bool leg = hasToken(tokens, "leg") || hasToken(tokens, "upperleg") || hasToken(tokens, "lowerleg") ||
                     hasToken(tokens, "thigh") || hasToken(tokens, "calf") || hasToken(tokens, "shin");
    const auto side = [left, right](int leftPart, int rightPart) { return left ? leftPart : right ? rightPart : -1; };
    if (hasToken(tokens, "pelvis")) return static_cast<int>(RagdollPart::pelvis);
    if (hasToken(tokens, "spine") || hasToken(tokens, "chest") || hasToken(tokens, "torso"))
        return static_cast<int>(RagdollPart::torso);
    if (hasToken(tokens, "head")) return static_cast<int>(RagdollPart::head);
    if (arm && (hasToken(tokens, "upperarm") || (hasToken(tokens, "upper") && hasToken(tokens, "arm"))))
        return side(static_cast<int>(RagdollPart::upperArmLeft), static_cast<int>(RagdollPart::upperArmRight));
    if (arm && (hasToken(tokens, "forearm") || hasToken(tokens, "lowerarm") ||
                (hasToken(tokens, "lower") && hasToken(tokens, "arm"))))
        return side(static_cast<int>(RagdollPart::lowerArmLeft), static_cast<int>(RagdollPart::lowerArmRight));
    if (leg && (hasToken(tokens, "thigh") || hasToken(tokens, "upperleg") ||
                (hasToken(tokens, "upper") && hasToken(tokens, "leg"))))
        return side(static_cast<int>(RagdollPart::upperLegLeft), static_cast<int>(RagdollPart::upperLegRight));
    if (leg && (hasToken(tokens, "calf") || hasToken(tokens, "shin") || hasToken(tokens, "lowerleg") ||
                (hasToken(tokens, "lower") && hasToken(tokens, "leg"))))
        return side(static_cast<int>(RagdollPart::lowerLegLeft), static_cast<int>(RagdollPart::lowerLegRight));
    return -1;
}

std::vector<glm::mat4> worldTransforms(const tinygltf::Model& source, const std::vector<Pose>& poses,
                                       const std::vector<int>& parents) {
    std::vector<glm::mat4> world(source.nodes.size(), glm::mat4(1.0f));
    std::vector<int> state(source.nodes.size(), 0);
    std::function<void(int, int)> compute = [&](int node, int depth) {
        if (state[node] == 2) return;
        if (state[node] == 1 || depth > 512) fail("Invalid animation hierarchy cycle or depth");
        state[node] = 1;
        const int parent = parents[node];
        if (parent >= 0) compute(parent, depth + 1);
        world[node] = (parent >= 0 ? world[parent] : glm::mat4(1.0f)) * poses[node].transform();
        for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) (void)checkedFloat(world[node][c][r]);
        state[node] = 2;
    };
    for (std::size_t node = 0; node < source.nodes.size(); ++node) compute(static_cast<int>(node), 0);
    return world;
}

std::vector<Weights> readWeights(const tinygltf::Model& source, const tinygltf::Primitive& primitive,
                                 std::size_t vertexCount, std::size_t jointCount) {
    std::vector<Weights> result(vertexCount);
    if (!primitive.attributes.count("JOINTS_0") || !primitive.attributes.count("WEIGHTS_0")) {
        fail("Skinned primitive must contain JOINTS_0 and WEIGHTS_0");
    }
    for (const auto& attribute : primitive.attributes) {
        if ((attribute.first.find("JOINTS_") == 0 || attribute.first.find("WEIGHTS_") == 0) &&
            attribute.first != "JOINTS_0" && attribute.first != "WEIGHTS_0" &&
            attribute.first != "JOINTS_1" && attribute.first != "WEIGHTS_1") fail("More than eight skin influences are not supported");
    }
    for (int set = 0; set < 2; ++set) {
        const auto jointIt = primitive.attributes.find("JOINTS_" + std::to_string(set));
        const auto weightIt = primitive.attributes.find("WEIGHTS_" + std::to_string(set));
        if (jointIt == primitive.attributes.end() && weightIt == primitive.attributes.end()) continue;
        if (jointIt == primitive.attributes.end() || weightIt == primitive.attributes.end()) fail("Unpaired skin joint/weight attribute");
        const auto& jointAccessor = at(source.accessors, jointIt->second, "joint accessor");
        const auto& weightAccessor = at(source.accessors, weightIt->second, "weight accessor");
        if (jointAccessor.normalized || (jointAccessor.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE &&
            jointAccessor.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)) fail("JOINTS must be non-normalized unsigned bytes or shorts");
        if ((weightAccessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT || weightAccessor.normalized) &&
            !((weightAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE ||
               weightAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) && weightAccessor.normalized)) {
            fail("WEIGHTS must be floats or normalized unsigned bytes/shorts");
        }
        const auto joints = readAccessor(source, jointIt->second, TINYGLTF_TYPE_VEC4);
        const auto weights = readAccessor(source, weightIt->second, TINYGLTF_TYPE_VEC4);
        if (joints.count != vertexCount || weights.count != vertexCount) fail("Skin attribute count does not match vertices");
        for (std::size_t i = 0; i < vertexCount; ++i) for (int c = 0; c < 4; ++c) {
            const auto joint = static_cast<std::uint32_t>(joints(i, c));
            const float weight = checkedFloat(weights(i, c));
            if (joint >= jointCount) fail("Skin joint index is out of range");
            if (weight < 0) fail("Skin weight must be nonnegative");
            result[i].joints[set * 4 + c] = joint;
            result[i].weights[set * 4 + c] = weight;
        }
    }
    for (auto& vertex : result) {
        double sum = 0;
        for (const auto weight : vertex.weights) sum += weight;
        if (!std::isfinite(sum) || sum <= 1e-12) fail("Skin weights must have a positive finite sum");
        for (auto& weight : vertex.weights) weight = static_cast<float>(weight / sum);
    }
    return result;
}

// Match loadModel's conversion order, including discarded degenerate strips/fans.
bool hasTriangles(const tinygltf::Model& source, const tinygltf::Primitive& primitive) {
    const int mode = primitive.mode < 0 ? TINYGLTF_MODE_TRIANGLES : primitive.mode;
    if (mode != TINYGLTF_MODE_TRIANGLES && mode != TINYGLTF_MODE_TRIANGLE_STRIP && mode != TINYGLTF_MODE_TRIANGLE_FAN) return false;
    const auto position = primitive.attributes.find("POSITION");
    if (position == primitive.attributes.end()) fail("Primitive is missing POSITION");
    if (primitive.indices < 0) return at(source.accessors, position->second, "POSITION accessor").count >= 3;
    const auto indices = readAccessor(source, primitive.indices, TINYGLTF_TYPE_SCALAR);
    if (mode == TINYGLTF_MODE_TRIANGLES) return indices.count > 0;
    for (std::size_t i = 2; i < indices.count; ++i) {
        const auto a = indices(mode == TINYGLTF_MODE_TRIANGLE_FAN ? 0 : i - 2, 0);
        const auto b = indices(i - 1, 0), c = indices(i, 0);
        if (a != b && b != c && a != c) return true;
    }
    return false;
}

AnimatedModel bake(const tinygltf::Model& source, Model base, const std::string& requestedClip) {
    const tinygltf::Animation* selected = nullptr;
    int bestMatch = 0;
    bool ambiguous = false;
    for (const auto& animation : source.animations) {
        const auto name = lower(animation.name);
        int match = 0;
        if (!requestedClip.empty()) match = name == lower(requestedClip) ? 1 : 0;
        else if (name == "idle") match = 3;
        else if (name.size() > 4 && name.compare(name.size() - 4, 4, "idle") == 0 &&
                 !std::isalnum(static_cast<unsigned char>(name[name.size() - 5]))) match = 2;
        else if (name.find("idle") != std::string::npos) match = 1;
        if (!match || match < bestMatch) continue;
        if (match == bestMatch) ambiguous = true;
        else {
            bestMatch = match;
            ambiguous = false;
            selected = &animation;
        }
    }
    if (ambiguous) fail("Multiple idle animations match; specify an exact clip name");
    if (!selected) fail(requestedClip.empty() ? "No animation containing 'idle' was found" : "Animation clip not found: " + requestedClip);
    AnimatedModel output;
    output.clipName = selected->name;
    float start = 0;
    const auto tracks = readTracks(source, *selected, start, output.duration);
    if (output.duration > 120) fail("Idle clips longer than 120 seconds exceed the animation baking limit");
    output.frameCount = std::max<std::size_t>(2, static_cast<std::size_t>(std::ceil(output.duration * 30.0f)));
    output.model.images = std::move(base.images);
    output.model.textures = std::move(base.textures);
    output.model.materials = std::move(base.materials);
    output.model.warnings = std::move(base.warnings);
    auto& warnings = output.model.warnings;
    warnings.erase(std::remove_if(warnings.begin(), warnings.end(), [](const std::string& warning) {
        return warning.find("Animations are not played;") == 0 || warning.find("Skinning is not supported;") == 0;
    }), warnings.end());

    std::vector<Pose> initial;
    std::vector<int> parents(source.nodes.size(), -1);
    for (std::size_t i = 0; i < source.nodes.size(); ++i) {
        initial.push_back(nodePose(source.nodes[i]));
        for (int child : source.nodes[i].children) {
            at(source.nodes, child, "child node");
            if (parents[child] >= 0) fail("Animation node hierarchy has multiple parents");
            parents[child] = static_cast<int>(i);
        }
    }
    std::vector<Skin> skins;
    for (std::size_t i = 0; i < source.skins.size(); ++i) skins.push_back(readSkin(source, static_cast<int>(i)));
    output.ragdollBones.fill(-1);
    output.skeleton.resize(source.nodes.size());
    const auto referenceWorld = worldTransforms(source, initial, parents);
    for (std::size_t node = 0; node < source.nodes.size(); ++node) {
        output.skeleton[node].name = source.nodes[node].name;
        output.skeleton[node].parent = parents[node];
        output.skeleton[node].referenceWorld = referenceWorld[node];
        const int part = ragdollPartForName(source.nodes[node].name);
        if (part >= 0 && output.ragdollBones[static_cast<std::size_t>(part)] < 0)
            output.ragdollBones[static_cast<std::size_t>(part)] = static_cast<int>(node);
    }
    for (const auto& skin : skins) {
        for (std::size_t joint = 0; joint < skin.joints.size(); ++joint)
            output.skeleton[static_cast<std::size_t>(skin.joints[joint])].inverseBind = skin.inverseBind[joint];
    }
    std::vector<std::vector<std::pair<std::size_t, const tinygltf::Primitive*>>> primitiveMap(source.meshes.size());
    std::size_t primitiveIndex = 0;
    for (std::size_t mesh = 0; mesh < source.meshes.size(); ++mesh) {
        for (const auto& primitive : source.meshes[mesh].primitives) {
            if (hasTriangles(source, primitive)) primitiveMap[mesh].push_back({primitiveIndex++, &primitive});
        }
    }
    if (primitiveIndex != base.primitives.size()) fail("Animation and static topology conversion disagree");
    std::vector<int> roots;
    if (source.scenes.empty()) {
        for (std::size_t i = 0; i < parents.size(); ++i) if (parents[i] < 0) roots.push_back(static_cast<int>(i));
    } else roots = at(source.scenes, source.defaultScene < 0 ? 0 : source.defaultScene, "scene").nodes;
    std::vector<Binding> bindings;
    std::vector<bool> visited(source.nodes.size(), false);
    std::size_t bakedBytes = 0;
    std::function<void(int)> visit = [&](int index) {
        const auto& node = at(source.nodes, index, "scene node");
        if (visited[index]) fail("Scene hierarchy contains a cycle or repeated node");
        visited[index] = true;
        if (node.mesh >= 0) {
            at(source.meshes, node.mesh, "mesh");
            for (const auto& entry : primitiveMap[node.mesh]) {
                if (!entry.second->targets.empty()) fail("Morph target geometry is not supported for animated models");
                auto primitive = base.primitives[entry.first];
                Binding binding{index, node.skin, {}};
                if (node.skin >= 0) {
                    binding.vertices = readWeights(source, *entry.second, primitive.vertices.size(), at(skins, node.skin, "skin").joints.size());
                }
                const auto vertices = product(output.frameCount, primitive.vertices.size());
                const auto bytes = product(vertices, sizeof(AnimationVertex));
                constexpr std::size_t maximumBytes = 512 * 1024 * 1024;
                if (bytes > maximumBytes - bakedBytes) fail("Baked animation exceeds the 512 MiB memory limit");
                bakedBytes += bytes;
                output.model.draws.push_back({output.model.primitives.size(), glm::mat4(1.0f), node.name});
                AnimatedPrimitive animated;
                animated.frames.resize(vertices);
                if (binding.skin >= 0) {
                    const auto& skin = at(skins, binding.skin, "skin");
                    animated.skinning.resize(primitive.vertices.size());
                    for (std::size_t vertex = 0; vertex < primitive.vertices.size(); ++vertex) {
                        for (std::size_t influence = 0; influence < 8; ++influence) {
                            animated.skinning[vertex].joints[influence] =
                                static_cast<std::uint32_t>(skin.joints[binding.vertices[vertex].joints[influence]]);
                            animated.skinning[vertex].weights[influence] = binding.vertices[vertex].weights[influence];
                        }
                    }
                }
                output.model.primitives.push_back(std::move(primitive));
                output.animation.push_back(std::move(animated));
                bindings.push_back(std::move(binding));
            }
        }
        for (int child : node.children) visit(child);
    };
    for (int root : roots) visit(root);
    if (bindings.empty()) fail("Animated scene contains no triangle geometry");

    output.model.boundsMin = glm::vec3(std::numeric_limits<float>::max());
    output.model.boundsMax = glm::vec3(std::numeric_limits<float>::lowest());
    std::vector<int> orientation(bindings.size(), 0);
    for (std::size_t frame = 0; frame < output.frameCount; ++frame) {
        auto poses = initial;
        const float time = start + output.duration * static_cast<float>(frame) / static_cast<float>(output.frameCount);
        for (const auto& track : tracks) {
            const auto value = track.sample(time);
            if (track.path == "translation") poses[track.node].translation = value;
            else if (track.path == "scale") poses[track.node].scale = value;
            else poses[track.node].rotation = quaternion(value);
        }
        const auto world = worldTransforms(source, poses, parents);
        std::vector<std::vector<glm::mat4>> skinMatrices;
        for (const auto& skin : skins) {
            std::vector<glm::mat4> matrices;
            for (std::size_t joint = 0; joint < skin.joints.size(); ++joint) matrices.push_back(world[skin.joints[joint]] * skin.inverseBind[joint]);
            skinMatrices.push_back(std::move(matrices));
        }
        for (std::size_t p = 0; p < bindings.size(); ++p) {
            const auto& binding = bindings[p];
            const auto& primitive = output.model.primitives[p];
            for (std::size_t v = 0; v < primitive.vertices.size(); ++v) {
                glm::mat4 transform = world[binding.node];
                if (binding.skin >= 0) {
                    // The mesh node transform cancels from world-space skinning:
                    // meshWorld * inverse(meshWorld) * jointWorld * inverseBind.
                    transform = glm::mat4(0.0f);
                    for (std::size_t influence = 0; influence < 8; ++influence) {
                        const auto& weights = binding.vertices[v];
                        if (weights.weights[influence] > 0) transform += skinMatrices[binding.skin][weights.joints[influence]] * weights.weights[influence];
                    }
                }
                const auto& vertex = primitive.vertices[v];
                const glm::vec4 position = transform * glm::vec4(vertex.position, 1.0f);
                for (int c = 0; c < 4; ++c) (void)checkedFloat(position[c]);
                const glm::mat3 basis(transform);
                const float determinant = glm::determinant(basis);
                if (!std::isfinite(determinant) || std::abs(determinant) < 1e-20f) fail("Animation produces a singular normal transform");
                const int sign = determinant < 0 ? -1 : 1;
                if (orientation[p] && orientation[p] != sign) fail("Animation changes orientation within a primitive");
                orientation[p] = sign;
                const auto normal = glm::transpose(glm::inverse(basis)) * vertex.normal;
                const float length = glm::length(normal);
                if (!std::isfinite(length) || length < 1e-12f) fail("Animation produces an invalid normal");
                output.animation[p].frames[frame * primitive.vertices.size() + v] = {position, glm::vec4(normal / length, 0.0f)};
                output.model.boundsMin = glm::min(output.model.boundsMin, glm::vec3(position));
                output.model.boundsMax = glm::max(output.model.boundsMax, glm::vec3(position));
            }
        }
    }
    for (std::size_t p = 0; p < output.model.primitives.size(); ++p) {
        if (orientation[p] < 0) {
            // Scene transforms are now identity; retain outward front faces after
            // baking a reflection into positions instead of the draw transform.
            auto& indices = output.model.primitives[p].indices;
            for (std::size_t i = 0; i < indices.size(); i += 3) std::swap(indices[i + 1], indices[i + 2]);
        }
        auto& vertices = output.model.primitives[p].vertices;
        if (bindings[p].skin < 0) {
            const glm::mat4 transform = output.skeleton[static_cast<std::size_t>(bindings[p].node)].referenceWorld;
            const glm::mat3 basis(transform);
            const float determinant = glm::determinant(basis);
            if (!std::isfinite(determinant) || std::abs(determinant) < 1e-20f)
                fail("Animated primitive has a singular reference transform");
            for (auto& vertex : vertices) {
                vertex.position = glm::vec3(transform * glm::vec4(vertex.position, 1.0f));
                vertex.normal = glm::normalize(glm::transpose(glm::inverse(basis)) * vertex.normal);
            }
        }
    }
    return output;
}

bool skipImage(tinygltf::Image*, int, std::string*, std::string*, int, int,
                const unsigned char*, int, void*) { return true; }

}  // namespace

AnimatedModel loadIdleModel(const std::filesystem::path& path, const std::string& clipName) {
    try {
        auto model = loadModel(path);
        tinygltf::TinyGLTF loader;
        loader.SetImageLoader(skipImage, nullptr);  // Images were decoded by loadModel.
        tinygltf::Model source;
        std::string error, warning;
        const bool binary = lower(path.extension().string()) == ".glb";
        const bool loaded = binary ? loader.LoadBinaryFromFile(&source, &error, &warning, path.string()) :
            loader.LoadASCIIFromFile(&source, &error, &warning, path.string());
        if (!loaded) fail(error.empty() ? "Unable to parse animation data" : error);
        return bake(source, std::move(model), clipName);
    } catch (const std::exception& error) {
        throw std::runtime_error("Failed to load idle model '" + path.string() + "': " + error.what());
    }
}

}  // namespace viewer
