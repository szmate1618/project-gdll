#pragma once

#include "opengl.hpp"

namespace viewer {

// Owns one depth-only comparison target, usable by directional or spot lights.
// The renderer restores its framebuffer, viewport, program uniforms, and
// polygon-offset state after the caster pass.
class ShadowMap {
public:
    explicit ShadowMap(int resolution);
    ~ShadowMap();
    ShadowMap(const ShadowMap&) = delete;
    ShadowMap& operator=(const ShadowMap&) = delete;
    void begin() const;
    void bind(GLenum textureUnit) const;

private:
    int resolution_;
    GLuint framebuffer_ = 0, depth_ = 0;
};

} // namespace viewer
