#include "model.hpp"

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include <tiny_gltf.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <numeric>
#include <set>
#include <stdexcept>
#include <utility>

namespace viewer {
namespace {

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

std::size_t checkedProduct(std::size_t a, std::size_t b) {
    if (b != 0 && a > std::numeric_limits<std::size_t>::max() / b) {
        fail("Accessor/image dimensions overflow addressable memory");
    }
    return a * b;
}

template <class T>
const T& at(const std::vector<T>& values, int index, const char* description) {
    if (index < 0 || static_cast<std::size_t>(index) >= values.size()) {
        fail(std::string("Invalid ") + description + " index " + std::to_string(index));
    }
    return values[static_cast<std::size_t>(index)];
}

int componentSize(int type) {
    switch (type) {
        case TINYGLTF_COMPONENT_TYPE_BYTE:
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: return 1;
        case TINYGLTF_COMPONENT_TYPE_SHORT:
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: return 2;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
        case TINYGLTF_COMPONENT_TYPE_FLOAT: return 4;
        default: fail("Unsupported accessor component type " + std::to_string(type));
    }
}

int componentCount(int type) {
    switch (type) {
        case TINYGLTF_TYPE_SCALAR: return 1;
        case TINYGLTF_TYPE_VEC2: return 2;
        case TINYGLTF_TYPE_VEC3: return 3;
        case TINYGLTF_TYPE_VEC4: return 4;
        default: fail("Expected a scalar or vector accessor");
    }
}

std::uint32_t readUnsigned(const unsigned char* data, int size) {
    std::uint32_t value = 0;
    for (int byte = 0; byte < size; ++byte) {
        value |= static_cast<std::uint32_t>(data[byte]) << (8 * byte);
    }
    return value;
}

double readComponent(const unsigned char* data, int type, bool normalized) {
    const auto bits = readUnsigned(data, componentSize(type));
    switch (type) {
        case TINYGLTF_COMPONENT_TYPE_BYTE: {
            const int value = bits >= 128 ? static_cast<int>(bits) - 256 : static_cast<int>(bits);
            return normalized ? std::max(-1.0, value / 127.0) : value;
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE: return normalized ? bits / 255.0 : bits;
        case TINYGLTF_COMPONENT_TYPE_SHORT: {
            const int value = bits >= 32768 ? static_cast<int>(bits) - 65536 : static_cast<int>(bits);
            return normalized ? std::max(-1.0, value / 32767.0) : value;
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: return normalized ? bits / 65535.0 : bits;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: return normalized ? bits / 4294967295.0 : bits;
        case TINYGLTF_COMPONENT_TYPE_FLOAT: {
            float value;
            std::memcpy(&value, &bits, sizeof(value));
            if (!std::isfinite(value)) fail("Accessor contains a non-finite floating-point value");
            return value;
        }
        default: fail("Unsupported accessor component type");
    }
}

// Every byte range is checked against both its bufferView and backing buffer.
const unsigned char* checkedRange(const tinygltf::Model& source, int viewIndex,
                                  std::size_t offset, std::size_t count,
                                  std::size_t stride, std::size_t packedSize) {
    const auto& view = at(source.bufferViews, viewIndex, "bufferView");
    const auto& buffer = at(source.buffers, view.buffer, "buffer").data;
    if (view.byteOffset > buffer.size() || view.byteLength > buffer.size() - view.byteOffset) {
        fail("BufferView extends beyond its buffer");
    }
    if (offset > view.byteLength || stride < packedSize) fail("Invalid accessor offset or stride");
    if (count > 0) {
        const auto trailing = checkedProduct(count - 1, stride);
        const auto remaining = view.byteLength - offset;
        if (trailing > remaining || packedSize > remaining - trailing) {
            fail("Accessor extends beyond its bufferView");
        }
    }
    return buffer.empty() ? nullptr : buffer.data() + view.byteOffset + offset;
}

struct AccessorData {
    std::size_t count;
    int components;
    std::vector<double> values;

    double operator()(std::size_t element, int component) const {
        return values[element * static_cast<std::size_t>(components) + component];
    }
};

AccessorData readAccessor(const tinygltf::Model& source, int index) {
    const auto& accessor = at(source.accessors, index, "accessor");
    const int components = componentCount(accessor.type);
    const int bytes = componentSize(accessor.componentType);
    const auto packedSize = static_cast<std::size_t>(components * bytes);
    AccessorData result{accessor.count, components, {}};
    result.values.resize(checkedProduct(accessor.count, static_cast<std::size_t>(components)), 0.0);
    if (accessor.bufferView >= 0) {
        const auto& view = at(source.bufferViews, accessor.bufferView, "bufferView");
        const auto stride = view.byteStride == 0 ? packedSize : view.byteStride;
        if (stride % static_cast<std::size_t>(bytes) != 0) fail("Accessor stride is misaligned");
        const auto* data = checkedRange(source, accessor.bufferView, accessor.byteOffset,
                                        accessor.count, stride, packedSize);
        for (std::size_t vertex = 0; vertex < accessor.count; ++vertex) {
            for (int c = 0; c < components; ++c) {
                result.values[vertex * components + c] =
                    readComponent(data + vertex * stride + c * bytes,
                                  accessor.componentType, accessor.normalized);
            }
        }
    } else if (!accessor.sparse.isSparse) {
        fail("Accessor has neither a bufferView nor sparse data");
    }
    if (accessor.sparse.isSparse) {
        const auto& sparse = accessor.sparse;
        if (sparse.count < 0 || static_cast<std::size_t>(sparse.count) > accessor.count) {
            fail("Invalid sparse accessor count");
        }
        const int indexType = sparse.indices.componentType;
        if (indexType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE &&
            indexType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT &&
            indexType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
            fail("Sparse accessor indices must be unsigned integers");
        }
        if (at(source.bufferViews, sparse.indices.bufferView, "sparse index bufferView").byteStride != 0 ||
            at(source.bufferViews, sparse.values.bufferView, "sparse value bufferView").byteStride != 0) {
            fail("Sparse accessor bufferViews cannot have a byteStride");
        }
        const auto indexBytes = static_cast<std::size_t>(componentSize(indexType));
        const auto count = static_cast<std::size_t>(sparse.count);
        const auto* indices = checkedRange(source, sparse.indices.bufferView,
            sparse.indices.byteOffset, count, indexBytes, indexBytes);
        const auto* values = checkedRange(source, sparse.values.bufferView,
            sparse.values.byteOffset, count, packedSize, packedSize);
        std::uint32_t previous = 0;
        for (std::size_t entry = 0; entry < count; ++entry) {
            const auto vertex = readUnsigned(indices + entry * indexBytes, static_cast<int>(indexBytes));
            if (vertex >= accessor.count || (entry > 0 && vertex <= previous)) {
                fail("Sparse indices must be increasing and within the accessor");
            }
            previous = vertex;
            for (int c = 0; c < components; ++c) {
                result.values[static_cast<std::size_t>(vertex) * components + c] =
                    readComponent(values + entry * packedSize + c * bytes,
                                  accessor.componentType, accessor.normalized);
            }
        }
    }
    return result;
}

float asFloat(double value, const char* description) {
    const auto result = static_cast<float>(value);
    if (!std::isfinite(result)) fail(std::string("Non-finite ") + description);
    return result;
}

glm::mat4 nodeTransform(const tinygltf::Node& node) {
    if (!node.matrix.empty()) {
        if (node.matrix.size() != 16) fail("Node matrix must contain 16 values");
        glm::mat4 matrix(1.0f);
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) matrix[c][r] = asFloat(node.matrix[c * 4 + r], "node matrix");
        }
        if (matrix[0][3] != 0 || matrix[1][3] != 0 || matrix[2][3] != 0 || matrix[3][3] != 1) {
            fail("Node transform must be an affine matrix");
        }
        return matrix;
    }
    glm::vec3 translation(0.0f), scale(1.0f);
    glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
    if (!node.translation.empty()) {
        if (node.translation.size() != 3) fail("Node translation must contain 3 values");
        for (int c = 0; c < 3; ++c) translation[c] = asFloat(node.translation[c], "node translation");
    }
    if (!node.scale.empty()) {
        if (node.scale.size() != 3) fail("Node scale must contain 3 values");
        for (int c = 0; c < 3; ++c) scale[c] = asFloat(node.scale[c], "node scale");
    }
    if (!node.rotation.empty()) {
        if (node.rotation.size() != 4) fail("Node rotation must contain 4 values");
        rotation = glm::quat(asFloat(node.rotation[3], "node rotation"),
                             asFloat(node.rotation[0], "node rotation"),
                             asFloat(node.rotation[1], "node rotation"),
                             asFloat(node.rotation[2], "node rotation"));
        if (glm::length(rotation) < 1e-12f) fail("Node rotation quaternion has zero length");
        rotation = glm::normalize(rotation);
    }
    return glm::translate(glm::mat4(1.0f), translation) * glm::mat4_cast(rotation) *
           glm::scale(glm::mat4(1.0f), scale);
}

void generateNormals(Primitive& primitive) {
    for (std::size_t face = 0; face < primitive.indices.size(); face += 3) {
        auto& a = primitive.vertices[primitive.indices[face]];
        auto& b = primitive.vertices[primitive.indices[face + 1]];
        auto& c = primitive.vertices[primitive.indices[face + 2]];
        const glm::dvec3 normal = glm::cross(glm::dvec3(b.position) - glm::dvec3(a.position),
                                           glm::dvec3(c.position) - glm::dvec3(a.position));
        // Normalize unusually large triangles before conversion to float.
        const double length = glm::length(normal);
        const auto weighted = glm::vec3(length > 1e30 ? normal / length : normal);
        a.normal += weighted;
        b.normal += weighted;
        c.normal += weighted;
    }
}

Primitive readPrimitive(const tinygltf::Model& source, const tinygltf::Primitive& input) {
    Primitive result;
    const auto positionIt = input.attributes.find("POSITION");
    if (positionIt == input.attributes.end()) fail("Triangle primitive has no POSITION attribute");
    const auto positions = readAccessor(source, positionIt->second);
    if (positions.components != 3) fail("POSITION must be a VEC3 accessor");
    if (positions.count > std::numeric_limits<std::uint32_t>::max()) fail("Too many vertices for 32-bit indices");
    result.vertices.resize(positions.count);
    for (std::size_t i = 0; i < positions.count; ++i) {
        for (int c = 0; c < 3; ++c) result.vertices[i].position[c] = asFloat(positions(i, c), "vertex position");
    }
    bool hasNormals = false;
    for (const std::string semantic : {"NORMAL", "TEXCOORD_0", "COLOR_0"}) {
        const auto attribute = input.attributes.find(semantic);
        if (attribute == input.attributes.end()) continue;
        const auto data = readAccessor(source, attribute->second);
        if (data.count != positions.count) fail(semantic + " count does not match POSITION");
        if ((semantic == "NORMAL" && data.components != 3) ||
            (semantic == "TEXCOORD_0" && data.components != 2) ||
            (semantic == "COLOR_0" && data.components != 3 && data.components != 4)) {
            fail("Invalid accessor shape for " + semantic);
        }
        for (std::size_t i = 0; i < data.count; ++i) {
            for (int c = 0; c < data.components; ++c) {
                const float value = asFloat(data(i, c), "vertex attribute");
                if (semantic == "NORMAL") result.vertices[i].normal[c] = value;
                else if (semantic == "TEXCOORD_0") result.vertices[i].uv[c] = value;
                else result.vertices[i].color[c] = value;
            }
        }
        if (semantic == "NORMAL") hasNormals = true;
    }
    std::vector<std::uint32_t> indices;
    if (input.indices >= 0) {
        const auto& accessor = at(source.accessors, input.indices, "index accessor");
        if (accessor.type != TINYGLTF_TYPE_SCALAR || accessor.normalized ||
            (accessor.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE &&
             accessor.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT &&
             accessor.componentType != TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)) {
            fail("Triangle indices must be non-normalized unsigned integer scalars");
        }
        const auto data = readAccessor(source, input.indices);
        indices.reserve(data.count);
        for (std::size_t i = 0; i < data.count; ++i) {
            const auto index = static_cast<std::uint32_t>(data(i, 0));
            if (index >= result.vertices.size()) fail("Triangle index exceeds vertex count");
            indices.push_back(index);
        }
    } else {
        indices.resize(result.vertices.size());
        std::iota(indices.begin(), indices.end(), 0u);
    }
    const int mode = input.mode < 0 ? TINYGLTF_MODE_TRIANGLES : input.mode;
    if (mode == TINYGLTF_MODE_TRIANGLES) {
        if (indices.size() % 3 != 0) fail("Triangle index count must be a multiple of three");
        result.indices = std::move(indices);
    } else {
        for (std::size_t i = 2; i < indices.size(); ++i) {
            const auto a = mode == TINYGLTF_MODE_TRIANGLE_FAN ? indices[0] : indices[i - 2];
            const auto b = indices[i - 1];
            const auto c = indices[i];
            if (a == b || b == c || a == c) continue;
            if (mode == TINYGLTF_MODE_TRIANGLE_STRIP && (i % 2) != 0) {
                result.indices.insert(result.indices.end(), {b, a, c});
            } else {
                result.indices.insert(result.indices.end(), {a, b, c});
            }
        }
    }
    if (!hasNormals) generateNormals(result);
    for (auto& vertex : result.vertices) {
        const auto normal = glm::dvec3(vertex.normal);
        const double length = glm::length(normal);
        vertex.normal = length > 1e-12 && std::isfinite(length) ? glm::vec3(normal / length) : glm::vec3(0, 1, 0);
    }
    if (input.material >= 0) {
        at(source.materials, input.material, "material");
        result.material = input.material;
    }
    return result;
}

bool decodeImage(tinygltf::Image* image, int index, std::string*, std::string* warning,
                 int requestedWidth, int requestedHeight, const unsigned char* bytes,
                 int size, void*) {
    std::string error;
    if (tinygltf::LoadImageData(image, index, &error, warning, requestedWidth,
                              requestedHeight, bytes, size, nullptr)) {
        return true;
    }
    if (warning) *warning += "Image " + std::to_string(index) + " could not be decoded; using white fallback. " + error + "\n";
    image->width = image->height = 1;
    image->component = 4;
    image->bits = 8;
    image->pixel_type = TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE;
    image->image = {255, 255, 255, 255};
    return true;
}

void readMaterialsAndImages(const tinygltf::Model& source, Model& output) {
    output.images.reserve(source.images.size());
    for (const auto& image : source.images) {
        ImageData result;
        if (image.width <= 0 || image.height <= 0 || image.component < 1 || image.component > 4 ||
            (image.bits != 8 && image.bits != 16)) {
            output.warnings.push_back("Image '" + image.uri + "' is unavailable or unsupported; using white fallback.");
            result.width = result.height = 1;
            result.rgba = {255, 255, 255, 255};
        } else {
            result.width = image.width;
            result.height = image.height;
            const auto pixels = checkedProduct(static_cast<std::size_t>(image.width), static_cast<std::size_t>(image.height));
            const auto sampleBytes = static_cast<std::size_t>(image.bits / 8);
            if (image.image.size() < checkedProduct(checkedProduct(pixels, image.component), sampleBytes)) {
                fail("Decoded image pixel buffer is too short");
            }
            result.rgba.resize(checkedProduct(pixels, 4));
            const auto channel = [&](std::size_t pixel, int c) -> std::uint8_t {
                const auto offset = (pixel * image.component + c) * sampleBytes;
                if (sampleBytes == 1) return image.image[offset];
                std::uint16_t value;
                std::memcpy(&value, image.image.data() + offset, sizeof(value));
                return static_cast<std::uint8_t>(value >> 8);
            };
            for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
                const bool grayscale = image.component <= 2;
                result.rgba[pixel * 4] = channel(pixel, 0);
                result.rgba[pixel * 4 + 1] = channel(pixel, grayscale ? 0 : 1);
                result.rgba[pixel * 4 + 2] = channel(pixel, grayscale ? 0 : 2);
                result.rgba[pixel * 4 + 3] = image.component == 2 ? channel(pixel, 1) :
                                            image.component == 4 ? channel(pixel, 3) : 255;
            }
        }
        output.images.push_back(std::move(result));
    }
    for (const auto& texture : source.textures) {
        Texture result;
        if (texture.source >= 0) {
            at(source.images, texture.source, "texture image");
            result.image = texture.source;
        } else {
            output.warnings.push_back("Texture has no supported source image; using base color.");
        }
        if (texture.sampler >= 0) {
            const auto& sampler = at(source.samplers, texture.sampler, "texture sampler");
            if (sampler.minFilter > 0) result.minFilter = sampler.minFilter;
            if (sampler.magFilter > 0) result.magFilter = sampler.magFilter;
            result.wrapS = sampler.wrapS;
            result.wrapT = sampler.wrapT;
        }
        const std::set<int> minFilters{9728, 9729, 9984, 9985, 9986, 9987};
        const std::set<int> wraps{33071, 33648, 10497};
        if (!minFilters.count(result.minFilter) || (result.magFilter != 9728 && result.magFilter != 9729) ||
            !wraps.count(result.wrapS) || !wraps.count(result.wrapT)) fail("Invalid texture sampler enum");
        output.textures.push_back(result);
    }
    for (const auto& material : source.materials) {
        Material result;
        const auto& pbr = material.pbrMetallicRoughness;
        if (pbr.baseColorFactor.size() != 4) fail("Material baseColorFactor must contain four values");
        for (int c = 0; c < 4; ++c) result.baseColor[c] = asFloat(pbr.baseColorFactor[c], "material base color");
        if (pbr.baseColorTexture.index >= 0) {
            at(source.textures, pbr.baseColorTexture.index, "base color texture");
            if (pbr.baseColorTexture.texCoord == 0) result.baseColorTexture = pbr.baseColorTexture.index;
            else output.warnings.push_back("Material requests TEXCOORD_" + std::to_string(pbr.baseColorTexture.texCoord) + "; using base color.");
        }
        result.doubleSided = material.doubleSided;
        result.alphaMode = material.alphaMode;
        if (result.alphaMode != "OPAQUE" && result.alphaMode != "MASK" && result.alphaMode != "BLEND") {
            fail("Invalid material alphaMode '" + result.alphaMode + "'");
        }
        result.alphaCutoff = asFloat(material.alphaCutoff, "material alpha cutoff");
        result.unlit = material.extensions.count("KHR_materials_unlit") != 0;
        output.materials.push_back(result);
    }
}

Model convert(const tinygltf::Model& source) {
    Model output;
    const std::set<std::string> supportedExtensions{"KHR_materials_unlit", "KHR_mesh_quantization"};
    for (const auto& extension : source.extensionsRequired) {
        if (!supportedExtensions.count(extension)) fail("Unsupported required glTF extension: " + extension);
    }
    for (const auto& extension : source.extensionsUsed) {
        if (!supportedExtensions.count(extension)) output.warnings.push_back("Ignoring optional glTF extension: " + extension);
    }
    if (!source.animations.empty()) output.warnings.push_back("Animations are not played; displaying the static node transforms.");
    if (!source.skins.empty()) output.warnings.push_back("Skinning is not supported; displaying undeformed mesh geometry.");
    readMaterialsAndImages(source, output);
    std::vector<std::vector<std::size_t>> meshPrimitives(source.meshes.size());
    for (std::size_t meshIndex = 0; meshIndex < source.meshes.size(); ++meshIndex) {
        for (const auto& primitive : source.meshes[meshIndex].primitives) {
            if (primitive.mode >= 0 && primitive.mode != TINYGLTF_MODE_TRIANGLES &&
                primitive.mode != TINYGLTF_MODE_TRIANGLE_STRIP && primitive.mode != TINYGLTF_MODE_TRIANGLE_FAN) {
                output.warnings.push_back("Skipping a non-triangle primitive in mesh " + std::to_string(meshIndex));
                continue;
            }
            if (!primitive.targets.empty()) output.warnings.push_back("Morph targets are not applied; displaying base mesh geometry.");
            auto converted = readPrimitive(source, primitive);
            if (converted.indices.empty()) continue;
            meshPrimitives[meshIndex].push_back(output.primitives.size());
            output.primitives.push_back(std::move(converted));
        }
    }
    std::vector<int> roots;
    if (!source.scenes.empty()) {
        const int scene = source.defaultScene < 0 ? 0 : source.defaultScene;
        roots = at(source.scenes, scene, "scene").nodes;
    } else {
        std::vector<bool> child(source.nodes.size(), false);
        for (const auto& node : source.nodes) {
            for (const int index : node.children) {
                at(source.nodes, index, "child node");
                child[static_cast<std::size_t>(index)] = true;
            }
        }
        for (std::size_t i = 0; i < source.nodes.size(); ++i) if (!child[i]) roots.push_back(static_cast<int>(i));
    }
    output.boundsMin = glm::vec3(std::numeric_limits<float>::max());
    output.boundsMax = glm::vec3(std::numeric_limits<float>::lowest());
    std::vector<bool> visited(source.nodes.size(), false);
    std::function<void(int, const glm::mat4&, int)> visit;
    visit = [&](int index, const glm::mat4& parent, int depth) {
        const auto& node = at(source.nodes, index, "node");
        if (depth > 512) fail("Node hierarchy exceeds 512 levels");
        if (visited[static_cast<std::size_t>(index)]) fail("Scene node hierarchy contains a cycle or repeated node");
        visited[static_cast<std::size_t>(index)] = true;
        const auto transform = parent * nodeTransform(node);
        for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) {
            if (!std::isfinite(transform[c][r])) fail("World transform exceeds floating-point range");
        }
        if (node.mesh >= 0) {
            at(source.meshes, node.mesh, "node mesh");
            for (const auto primitive : meshPrimitives[static_cast<std::size_t>(node.mesh)]) {
                output.draws.push_back({primitive, transform});
                for (const auto& vertex : output.primitives[primitive].vertices) {
                    const glm::vec3 position(transform * glm::vec4(vertex.position, 1.0f));
                    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) {
                        fail("Transformed vertex exceeds floating-point range");
                    }
                    output.boundsMin = glm::min(output.boundsMin, position);
                    output.boundsMax = glm::max(output.boundsMax, position);
                }
            }
        }
        for (const int child : node.children) visit(child, transform, depth + 1);
    };
    for (const int root : roots) visit(root, glm::mat4(1.0f), 0);
    if (output.draws.empty()) fail("The active scene contains no renderable triangle geometry");
    return output;
}

}  // namespace

Model loadModel(const std::filesystem::path& path) {
    try {
        if (!std::filesystem::is_regular_file(path)) fail("File does not exist or is not a regular file");
        tinygltf::TinyGLTF loader;
        loader.SetImageLoader(decodeImage, nullptr);
        tinygltf::Model source;
        std::string error, warning;
        std::string extension = path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        bool loaded = false;
        if (extension == ".glb") loaded = loader.LoadBinaryFromFile(&source, &error, &warning, path.string());
        else if (extension == ".gltf") loaded = loader.LoadASCIIFromFile(&source, &error, &warning, path.string());
        else fail("Expected a .glb or .gltf file");
        if (!loaded) fail(error.empty() ? "glTF parser could not load the file" : error);
        auto output = convert(source);
        if (!warning.empty()) output.warnings.insert(output.warnings.begin(), warning);
        return output;
    } catch (const std::exception& error) {
        throw std::runtime_error("Failed to load model '" + path.string() + "': " + error.what());
    }
}

}  // namespace viewer
