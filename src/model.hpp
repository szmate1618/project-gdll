#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace viewer {

struct Vertex {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f};
    glm::vec2 uv{0.0f};
    glm::vec4 color{1.0f};
};

// RGBA8 pixels in the glTF image's native orientation (no vertical flip).
struct ImageData {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
};

struct Texture {
    int image = -1;
    int minFilter = 9987;
    int magFilter = 9729;
    int wrapS = 10497;
    int wrapT = 10497;
};

struct Material {
    glm::vec4 baseColor{1.0f};
    int baseColorTexture = -1;
    bool doubleSided = false;
    std::string alphaMode = "OPAQUE";
    float alphaCutoff = 0.5f;
    bool unlit = false;
};

struct Primitive {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    int material = -1;
};

struct DrawInstance {
    std::size_t primitive = 0;
    glm::mat4 transform{1.0f};
};

struct Model {
    std::vector<ImageData> images;
    std::vector<Texture> textures;
    std::vector<Material> materials;
    std::vector<Primitive> primitives;
    std::vector<DrawInstance> draws;
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};
    std::vector<std::string> warnings;
};

// Throws std::runtime_error including the input path on invalid/unsupported data.
Model loadModel(const std::filesystem::path& path);

}  // namespace viewer
