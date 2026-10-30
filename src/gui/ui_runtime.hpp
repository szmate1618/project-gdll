#pragma once

#include "hud_render_interface.hpp"
#include <memory>

namespace viewer {
// Shared by menu/loading screens and the HUD on the application GL thread.
// Contexts must be removed before releasing their runtime. The renderer and
// borrowed font bytes remain alive until after Rml::Shutdown().
class UiRuntime {
public:
    static std::shared_ptr<UiRuntime> acquire(const std::filesystem::path& directory);
    ~UiRuntime();
    HudRenderInterface& renderer();
    void loadMenuFont(const std::filesystem::path& path);
private:
    explicit UiRuntime(const std::filesystem::path& directory);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace viewer
