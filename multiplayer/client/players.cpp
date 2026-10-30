#include "players.hpp"
#include "player_dimensions.hpp"
#include "resources.hpp"
#include "shader_program.hpp"
#include "weapon_overlay.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <array>
#include <cstddef>
#include <stdexcept>

namespace multiplayer {
PlayerRenderer::PlayerRenderer(viewer::WeaponOverlay* sharedWeapon) : weapon_(sharedWeapon) {
    struct Vertex { glm::vec3 position; glm::vec2 uv; float face; };
    struct Side { glm::vec3 origin, across, down; };
    // Unit-cube equivalents of CharacterCanvas's six authored surfaces.
    const std::array<Side, 6> sides{{
        {{-.5f,.5f,.5f},{1,0,0},{0,-1,0}}, {{.5f,.5f,-.5f},{-1,0,0},{0,-1,0}},
        {{.5f,.5f,.5f},{0,0,-1},{0,-1,0}}, {{-.5f,.5f,-.5f},{0,0,1},{0,-1,0}},
        {{-.5f,.5f,-.5f},{1,0,0},{0,0,1}}, {{-.5f,-.5f,.5f},{1,0,0},{0,0,-1}}}};
    const std::array<glm::vec2, 4> corners{{{0,0},{1,0},{1,1},{0,1}}};
    std::vector<Vertex> vertices;
    for (int face = 0; face < 6; ++face) for (const int corner : {0,2,1,0,3,2}) {
        const auto uv = corners[corner]; const auto& side = sides[face];
        vertices.push_back({side.origin + side.across * uv.x + side.down * uv.y, uv, float(face)});
    }
    const auto path = resourceRoot() / "multiplayer/client";
    program_ = viewer::createShaderProgram(path / "players.vert", path / "players.frag");
    glGenVertexArrays(1, &vao_); glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_); glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)), vertices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), nullptr);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, uv)));
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, face)));
    glBindVertexArray(0); glBindBuffer(GL_ARRAY_BUFFER, 0);
    matrix_ = glGetUniformLocation(program_, "uMatrix"); color_ = glGetUniformLocation(program_, "uColor");
    textured_ = glGetUniformLocation(program_, "uTextured");
}
PlayerRenderer::~PlayerRenderer() {
    for (const auto& [id, appearance] : appearances_) { (void)id; glDeleteTextures(1, &appearance.texture); }
    glDeleteBuffers(1, &vbo_); glDeleteVertexArrays(1, &vao_); glDeleteProgram(program_);
}
void PlayerRenderer::setAppearance(std::uint32_t player, const CharacterAppearance& appearance) {
    if (!player || appearance.rgb.size() != characterTextureBytes || (!appearances_.count(player) && appearances_.size() >= maxPlayers))
        throw std::runtime_error("Invalid player appearance texture");
    GLint active = 0, texture = 0, buffer = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active); glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &texture); glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &buffer);
    constexpr std::array<GLenum, 6> settings{GL_UNPACK_ALIGNMENT, GL_UNPACK_ROW_LENGTH, GL_UNPACK_SKIP_PIXELS,
        GL_UNPACK_SKIP_ROWS, GL_UNPACK_IMAGE_HEIGHT, GL_UNPACK_SKIP_IMAGES};
    std::array<GLint, 6> previous{};
    for (std::size_t i = 0; i < settings.size(); ++i) { glGetIntegerv(settings[i], &previous[i]); glPixelStorei(settings[i], i == 0 ? 1 : 0); }
    auto& stored = appearances_[player];
    if (!stored.texture) glGenTextures(1, &stored.texture);
    stored.base = glm::vec3(appearance.rgb[0], appearance.rgb[1], appearance.rgb[2]) / 255.f;
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0); glBindTexture(GL_TEXTURE_2D_ARRAY, stored.texture);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGB8, characterTextureResolution, characterTextureResolution,
                 characterTextureFaces, 0, GL_RGB, GL_UNSIGNED_BYTE, appearance.rgb.data());
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    for (std::size_t i = 0; i < settings.size(); ++i) glPixelStorei(settings[i], previous[i]);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, buffer); glBindTexture(GL_TEXTURE_2D_ARRAY, texture); glActiveTexture(active);
}
void PlayerRenderer::removeAppearance(std::uint32_t player) {
    const auto it = appearances_.find(player);
    if (it != appearances_.end()) { glDeleteTextures(1, &it->second.texture); appearances_.erase(it); }
}
void PlayerRenderer::receiveShots(const std::vector<SoundEvent>& shots, const std::vector<PlayerState>& players,
                                  double serverSeconds) {
    weapons_.receiveShots(shots, players, serverSeconds);
}
std::vector<viewer::MuzzleFlashLight> PlayerRenderer::muzzleLights(double seconds) const {
    return weapons_.muzzleLights(seconds);
}
void PlayerRenderer::draw(const viewer::Camera& camera, const std::vector<PlayerState>& players,
                          std::uint32_t self, int width, int height, double serverSeconds) {
    if (width <= 0 || height <= 0) return;
    weapons_.receiveShots({}, players, serverSeconds);
    GLint program = 0, vao = 0, front = 0, active = 0, texture = 0, sampler = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &program); glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
    glGetIntegerv(GL_FRONT_FACE, &front); glFrontFace(GL_CCW);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active); glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &texture); glGetIntegerv(GL_SAMPLER_BINDING, &sampler); glBindSampler(0, 0);
    const auto vp = camera.projection(static_cast<float>(width) / height) * camera.view();
    glUseProgram(program_); glBindVertexArray(vao_);
    for (const auto& p : players) {
        if (p.id == self || p.vehicle != noVehicle) continue;
        const auto custom = appearances_.find(p.id);
        const bool textured = custom != appearances_.end();
        glUniform1i(textured_, textured);
        if (textured) glBindTexture(GL_TEXTURE_2D_ARRAY, custom->second.texture);
        glUniform3f(color_, .15f + .12f * (p.id % 4), .65f, .95f);
        if (p.melee.enabled && !textured) {
            for (const auto& part : p.melee.parts) {
                const auto m = vp * part;
                glUniformMatrix4fv(matrix_,1,GL_FALSE,glm::value_ptr(m));
                glDrawArrays(GL_TRIANGLES,0,36);
            }
            continue;
        }
        const float heightMeters = p.movement.stance == viewer::PlayerStance::Standing ? standingPlayerSize.y :
            p.movement.stance == viewer::PlayerStance::Crouched ? 1.1f : .6f;
        const auto model = glm::translate(glm::mat4(1), p.movement.feet + glm::vec3(0, heightMeters * .5f, 0)) *
            glm::rotate(glm::mat4(1), glm::radians(textured ? 90.f - p.yaw : -p.yaw), glm::vec3(0, 1, 0)) *
            glm::scale(glm::mat4(1), glm::vec3(standingPlayerSize.x, heightMeters, standingPlayerSize.z));
        const auto m = vp * model;
        glUniformMatrix4fv(matrix_, 1, GL_FALSE, glm::value_ptr(m));
        glUniform3f(color_, .15f + .12f * (p.id % 4), .65f, .95f);
        glDrawArrays(GL_TRIANGLES, 0, 36);
        if (p.melee.enabled) {
            // Keep the authored box (including face/head and lower body) while
            // physical arms/fists retain their authoritative animation.
            glUniform1i(textured_, false);
            glUniform3fv(color_, 1, glm::value_ptr(custom->second.base));
            for (std::size_t part = static_cast<std::size_t>(viewer::PlayerBodyPart::leftUpperArm); part < p.melee.parts.size(); ++part) {
                const auto arm = vp * p.melee.parts[part];
                glUniformMatrix4fv(matrix_, 1, GL_FALSE, glm::value_ptr(arm)); glDrawArrays(GL_TRIANGLES, 0, 36);
            }
        }
    }
    if (weapon_) for (const auto& player : players) {
        if (player.id == self) continue;
        if (const auto sample = weapons_.sample(player, serverSeconds))
            weapon_->drawWorld(sample->slot, camera, static_cast<float>(width) / height,
                sample->eyeToWorld, sample->flash, sample->motion);
    }
    glFrontFace(static_cast<GLenum>(front));
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D_ARRAY, texture); glBindSampler(0, sampler); glActiveTexture(active);
    glBindVertexArray(static_cast<GLuint>(vao)); glUseProgram(static_cast<GLuint>(program));
}
} // namespace multiplayer
