#pragma once

#include "opengl.hpp"
#include "visibility.hpp"
#include <vector>

namespace viewer {
struct Model;

// Owns one static road VBO. BVH-selected cell ranges are submitted together;
// movement changes uniforms and CPU ranges, never the uploaded geometry.
class MinimapRoadRenderer {
public:
    explicit MinimapRoadRenderer(const Model& model);
    ~MinimapRoadRenderer();
    MinimapRoadRenderer(const MinimapRoadRenderer&) = delete;
    MinimapRoadRenderer& operator=(const MinimapRoadRenderer&) = delete;
    // Current shader must map roads into a target-sized viewport at origin.
    // Returns a premultiplied RGBA texture, with OpenGL's bottom-up orientation.
    // Framebuffer, viewport and scissor enable are restored before returning.
    GLuint render(glm::vec2 center, float spanMeters, glm::ivec2 size);
private:
    void resizeTarget(glm::ivec2 size);
    struct Range { GLint first; GLsizei count; };
    GLuint vao_ = 0, buffer_ = 0;
    GLuint multisampleFramebuffer_ = 0, multisampleColor_ = 0;
    GLuint resolvedFramebuffer_ = 0, resolvedColor_ = 0;
    glm::ivec2 size_{0};
    VisibilityIndex index_;
    std::vector<Range> ranges_;
    std::vector<std::size_t> visible_;
    std::vector<GLint> first_;
    std::vector<GLsizei> count_;
    glm::vec2 lastCenter_{0};
    float lastSpan_ = 0;
    bool queried_ = false;
};
} // namespace viewer
