#pragma once

#include "camera.hpp"
#include "grip_preview.hpp"
#include "model.hpp"
#include "opengl.hpp"
#include <filesystem>
#include <vector>

namespace viewer {

// The model is already in the palm-contact frame. Instances share one streamed
// batch, with ordinary world depth and 50% opacity, without writing scene depth.
class GripRenderer {
public:
    GripRenderer(const std::filesystem::path& shaderDirectory, const Model& model);
    ~GripRenderer();
    GripRenderer(const GripRenderer&) = delete;
    GripRenderer& operator=(const GripRenderer&) = delete;

    void draw(const Camera& camera, float aspect, const std::vector<GripPreviewPose>& poses);

private:
    struct Vertex { glm::vec3 position, normal; };
    struct Triangle { Vertex vertices[3]; float depth; };
    void release() noexcept;
    GLuint program_ = 0, vao_ = 0, buffer_ = 0;
    GLint viewProjectionLocation_ = -1, viewDirectionLocation_ = -1;
    std::vector<Vertex> model_, batch_;
    std::vector<Triangle> triangles_;
};

} // namespace viewer
