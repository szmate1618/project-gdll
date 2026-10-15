#pragma once
#include "protocol.hpp"
#include "camera.hpp"
#include <GL/glcorearb.h>

namespace multiplayer {
class PlayerRenderer {
public:
    PlayerRenderer();
    ~PlayerRenderer();
    void draw(const viewer::Camera& camera, const std::vector<PlayerState>& players,
              std::uint32_t self, int width, int height);
private:
    GLuint program_ = 0, vao_ = 0, vbo_ = 0;
    GLint matrix_ = -1, color_ = -1;
};
} // namespace multiplayer
