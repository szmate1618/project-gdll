#pragma once

#include <GL/glcorearb.h>

namespace viewer {

// Owns the depth-only sun target. The renderer restores its framebuffer,
// viewport, program uniforms, and polygon-offset state after the caster pass.
class SunShadowMap {
public:
    SunShadowMap();
    ~SunShadowMap();
    SunShadowMap(const SunShadowMap&) = delete;
    SunShadowMap& operator=(const SunShadowMap&) = delete;
    void begin() const;
    void bind(GLenum textureUnit) const;

private:
    GLuint framebuffer_ = 0, depth_ = 0;
};

} // namespace viewer
