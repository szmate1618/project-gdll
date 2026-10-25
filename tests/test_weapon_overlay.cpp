#include "weapon_overlay.hpp"
#include "renderer.hpp"
#include "player_body.hpp"
#include "player_body_renderer.hpp"
#include "player_presentation.hpp"
#include "physics_world.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <GLFW/glfw3.h>
#include <cmath>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>

int main(int argc, char** argv) {
    const int width = argc > 3 ? std::stoi(argv[2]) : 800;
    const int height = argc > 3 ? std::stoi(argv[3]) : 600;
    if (!glfwInit()) return 77;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    auto* window = glfwCreateWindow(width, height, "weapon overlay check", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 77; }
    glfwMakeContextCurrent(window);
    int result = 0;
    try {
        viewer::initializeOpenGL();
        std::cout << "OpenGL renderer: " << glGetString(GL_RENDERER) << '\n';
        viewer::WeaponOverlay overlay(VIEWER_SHADER_DIR, VIEWER_WEAPON_ASSETS);
        glViewport(0, 0, width, height);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glEnable(GL_DEPTH_TEST); glEnable(GL_BLEND); glEnable(GL_CULL_FACE); glDepthMask(GL_TRUE);
        glCullFace(GL_FRONT); glFrontFace(GL_CW);
        glUseProgram(0); glBindVertexArray(0);
        std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 3);
        std::set<std::size_t> silhouetteCounts;
        for (std::size_t slot = 0; slot < viewer::weaponIds.size(); ++slot) {
            glClearColor(.1f, .2f, .3f, 1);
            // Simulate a wall pressed against the camera. Weapons must use a
            // fresh depth layer while retaining self-occlusion between parts.
            glClearDepth(0);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glDepthFunc(GL_GREATER); glDepthMask(GL_FALSE);
            glDepthRange(.2, .8);
            glEnable(GL_SCISSOR_TEST); glScissor(0, 0, 1, 1);
            overlay.draw(slot, width, height);
            GLint program = -1, vao = -1, front = -1, cull = -1, depthFunction = 0;
            GLboolean depthWrite = GL_FALSE;
            GLdouble clearDepth = 1, depthRange[2]{};
            glGetIntegerv(GL_CURRENT_PROGRAM, &program); glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
            glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWrite);
            glGetIntegerv(GL_FRONT_FACE, &front); glGetIntegerv(GL_CULL_FACE_MODE, &cull);
            glGetIntegerv(GL_DEPTH_FUNC, &depthFunction);
            glGetDoublev(GL_DEPTH_CLEAR_VALUE, &clearDepth); glGetDoublev(GL_DEPTH_RANGE, depthRange);
            if (!glIsEnabled(GL_DEPTH_TEST) || !glIsEnabled(GL_BLEND) || !glIsEnabled(GL_CULL_FACE) ||
                !glIsEnabled(GL_SCISSOR_TEST) || depthWrite || program != 0 || vao != 0 ||
                front != GL_CW || cull != GL_FRONT || depthFunction != GL_GREATER || clearDepth != 0 ||
                std::abs(depthRange[0] - .2) > 1e-6 || std::abs(depthRange[1] - .8) > 1e-6)
                throw std::runtime_error("Weapon draw must restore GL state");
            glDisable(GL_SCISSOR_TEST); glDepthMask(GL_TRUE);
            glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
            std::size_t foreground = 0;
            double foregroundY = 0;
            std::set<unsigned> colors;
            for (std::size_t i = 0; i < pixels.size(); i += 3)
                if (pixels[i] != pixels[0] || pixels[i+1] != pixels[1] || pixels[i+2] != pixels[2]) {
                    ++foreground;
                    foregroundY += (i / 3) / static_cast<std::size_t>(width);
                    colors.insert((unsigned(pixels[i]) << 16) | (unsigned(pixels[i+1]) << 8) | pixels[i+2]);
                }
            if (foreground < 500 || foreground > static_cast<std::size_t>(width) * height / 4 || colors.size() < 3 ||
                !silhouetteCounts.insert(foreground).second || foregroundY / foreground >= height * .5)
                throw std::runtime_error("Each weapon must render a distinct shaded mesh below the crosshair");
            if (argc > 1) {
                std::ofstream capture(std::string(argv[1]) + "-" + std::to_string(slot + 1) + ".ppm", std::ios::binary);
                capture << "P6\n" << width << ' ' << height << "\n255\n";
                for (int y = height - 1; y >= 0; --y)
                    capture.write(reinterpret_cast<const char*>(pixels.data() + y * width * 3), width * 3);
            }
        }
        // Exercise the same motion payload as the game, retaining one draw and
        // the same submitted triangle count at every animation phase.
        viewer::WeaponMotion motion;
        GLuint query; glGenQueries(1, &query);
        const auto render = [&](std::size_t slot, const viewer::WeaponMotionPose& pose, const std::string& phase) {
            glClearDepth(1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glBeginQuery(GL_PRIMITIVES_GENERATED, query);
            overlay.draw(slot, width, height, {}, pose);
            glEndQuery(GL_PRIMITIVES_GENERATED);
            GLuint triangles = 0; glGetQueryObjectuiv(query, GL_QUERY_RESULT, &triangles);
            std::vector<unsigned char> result(pixels.size());
            glReadPixels(0,0,width,height,GL_RGB,GL_UNSIGNED_BYTE,result.data());
            if (argc > 1) {
                std::ofstream capture(std::string(argv[1])+"-"+std::to_string(slot+1)+"-"+phase+".ppm", std::ios::binary);
                capture << "P6\n" << width << ' ' << height << "\n255\n";
                for (int y=height-1;y>=0;--y)
                    capture.write(reinterpret_cast<const char*>(result.data()+y*width*3),width*3);
            }
            return std::make_pair(result,triangles);
        };
        for (std::size_t slot=0;slot<4;++slot) {
            const auto rest = render(slot, {}, "rest");
            motion.fire(slot,10);
            const auto kicked = render(slot, motion.pose(slot,10), "kick");
            if (rest.first == kicked.first || rest.second != kicked.second || rest != render(slot,motion.pose(slot,11),"recovered"))
                throw std::runtime_error("Firearm must recoil and recover without changing geometry");
            const glm::vec3 muzzle{0,.02f,-.4f};
            if (glm::distance(overlay.muzzlePosition(slot,muzzle),overlay.muzzlePosition(slot,muzzle,motion.pose(slot,10))) < .01f)
                throw std::runtime_error("Muzzle light must follow animated barrel");
            // Isolate the pump: the receiver and other weapons must stay fixed.
            viewer::WeaponMotionPose pump;
            pump.pumpTravel = motion.pose(2,10.36).pumpTravel;
            const auto pumped = render(slot,pump,"pump-only");
            std::size_t changed = 0;
            for (std::size_t p=0;p<rest.first.size();p+=3)
                if (rest.first[p]!=pumped.first[p] || rest.first[p+1]!=pumped.first[p+1] || rest.first[p+2]!=pumped.first[p+2]) ++changed;
            if (pumped.second != rest.second || (slot == 2 ? changed < 20 || changed > 3000 : changed != 0))
                throw std::runtime_error("Pump motion must affect only the separate shotgun fore-end");
            if (slot == 2)
                for (const double elapsed : {.12,.24,.36,.50,.66})
                    render(slot,motion.pose(slot,10+elapsed),std::to_string(elapsed));
        }
        glDeleteQueries(1, &query);
        viewer::PlayerBodyRenderer bodyRenderer(VIEWER_SHADER_DIR);
        auto physics = std::make_shared<viewer::PhysicsWorld>();
        viewer::PlayerBody body(physics, 1.7f);
        const glm::vec3 eye(0, 1.7f, 0);
        body.follow(eye, {0, 0, -1}, true);
        viewer::PlayerPresentation presentation;
        presentation.reset(eye, body.pose());
        const auto projection = glm::perspective(glm::radians(60.f), float(width) / height, .01f, 20.f);
        int outboundFrames = 0;
        for (int pose = 0; pose < 5; ++pose) {
            if (pose == 1) {
                body.punch({0, 0, -1});
                const auto start = body.fistPosition(true);
                // Sample by travel rather than time so tuning punch speed
                // cannot accidentally capture the guard or an invisible fist.
                for (int f = 0; f < 120 && glm::distance(start, body.fistPosition(true)) < .40f &&
                                    body.punching(viewer::PlayerBodyPart::rightFist); ++f) {
                    physics->step(1.f / 60, [&](float s) { body.beforeStep(s); }, [&](float s) {
                        body.afterStep(); bodyRenderer.captureBody(body, s); presentation.advance(eye, body.pose());
                    });
                    ++outboundFrames;
                }
                if (!body.punching(viewer::PlayerBodyPart::rightFist))
                    throw std::runtime_error("Punch capture must sample the active swing before guard recovery");
            }
            if (pose == 3) {
                for (int f = 0; f < 120 && body.punching(viewer::PlayerBodyPart::rightFist); ++f) {
                    physics->step(1.f / 60, [&](float s) { body.beforeStep(s); }, [&](float s) {
                        body.afterStep(); bodyRenderer.captureBody(body, s); presentation.advance(eye, body.pose());
                    });
                    ++outboundFrames;
                }
                for (int f = 0; f < 3 * outboundFrames / 2; ++f)
                    physics->step(1.f / 60, [&](float s) { body.beforeStep(s); }, [&](float s) {
                        body.afterStep(); bodyRenderer.captureBody(body, s); presentation.advance(eye, body.pose());
                    });
                if (!body.returning(viewer::PlayerBodyPart::rightFist) || body.punching(viewer::PlayerBodyPart::rightFist))
                    throw std::runtime_error("Recovery capture must sample a non-damaging arm still moving toward guard");
            }
            glDisable(GL_SCISSOR_TEST); glDepthMask(GL_TRUE); glDepthRange(0, 1);
            glClearColor(.1f, .2f, .3f, 1); glClearDepth(pose == 4 ? 0 : 1);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            // The close guard is below the eye camera's field of view. Inspect
            // it externally; the active punch also gets a first-person capture.
            const auto view = pose != 1 ? glm::lookAt(glm::vec3(1.7f, 1.7f, -1.7f), glm::vec3(0, 1.1f, 0), glm::vec3(0, 1, 0)) :
                                         glm::lookAt(eye, eye + glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));
            // Exercise trail blending with non-default caller state, including
            // a fully occluding depth buffer in the final pose.
            glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ZERO, GL_ONE);
            glBlendEquationSeparate(GL_FUNC_REVERSE_SUBTRACT, GL_FUNC_SUBTRACT);
            if (pose == 1)
                bodyRenderer.drawPose(presentation.pose(.5f), projection * view, width, height,
                                      .5f * viewer::PhysicsWorld::fixedStepSeconds);
            else bodyRenderer.drawBody(body, projection * view, width, height);
            GLint srcRgb = 0, dstRgb = 0, srcAlpha = 0, dstAlpha = 0, equationRgb = 0, equationAlpha = 0;
            glGetIntegerv(GL_BLEND_SRC_RGB, &srcRgb); glGetIntegerv(GL_BLEND_DST_RGB, &dstRgb);
            glGetIntegerv(GL_BLEND_SRC_ALPHA, &srcAlpha); glGetIntegerv(GL_BLEND_DST_ALPHA, &dstAlpha);
            glGetIntegerv(GL_BLEND_EQUATION_RGB, &equationRgb); glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &equationAlpha);
            if (srcRgb != GL_ONE || dstRgb != GL_ZERO || srcAlpha != GL_ZERO || dstAlpha != GL_ONE ||
                equationRgb != GL_FUNC_REVERSE_SUBTRACT || equationAlpha != GL_FUNC_SUBTRACT)
                throw std::runtime_error("Punch motion blur must restore blend functions and equations");
            glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
            std::size_t foreground = 0;
            for (std::size_t i = 0; i < pixels.size(); i += 3)
                if (pixels[i] != pixels[0] || pixels[i+1] != pixels[1] || pixels[i+2] != pixels[2]) ++foreground;
            if ((pose < 4 && foreground < 500) || (pose == 4 && foreground != 0))
                throw std::runtime_error("Melee body must render guard/punch and respect scene depth");
            if (argc > 1 && pose < 4) {
                std::ofstream capture(std::string(argv[1]) + "-body-" + std::to_string(pose) + ".ppm", std::ios::binary);
                capture << "P6\n" << width << ' ' << height << "\n255\n";
                for (int y = height - 1; y >= 0; --y)
                    capture.write(reinterpret_cast<const char*>(pixels.data() + y * width * 3), width * 3);
            }
        }
        viewer::Renderer::checkErrors("weapon overlays and melee body");
        std::cout << "All five weapon silhouettes and GL state checks passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; result = 1; }
    glfwDestroyWindow(window); glfwTerminate();
    return result;
}
