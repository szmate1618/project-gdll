#include "ui_runtime.hpp"
#include "command_wheel.hpp"
#include "map_view.hpp"
#include <RmlUi/Core.h>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace viewer {
namespace {
class UiSystem final : public Rml::SystemInterface {
public:
    double GetElapsedTime() override {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
    }
    bool LogMessage(Rml::Log::Type, const Rml::String& message) override {
        std::cerr << "RmlUi: " << message << '\n';
        return true;
    }
private:
    std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
};

std::filesystem::path contentFont(const std::filesystem::path& directory) {
    if (const auto* override = std::getenv("GD_HUD_FONT")) return override;
    return directory / "fonts/plex/IBMPlexSans-Regular.ttf";
}

} // namespace

struct UiRuntime::Impl {
    HudRenderInterface renderer;
    UiSystem system;
    std::vector<Rml::byte> font, headingFont, numberFont, menuFont;
    bool initialized = false;

    explicit Impl(const std::filesystem::path& directory) : renderer(directory) {
        Rml::SetSystemInterface(&system);
        Rml::SetRenderInterface(&renderer);
        try {
            if (!Rml::Initialise()) throw std::runtime_error("Cannot initialize RmlUi");
            initialized = true;
            loadFont(contentFont(directory), "hud-system", font);
            loadFont(directory / "fonts/plex/IBMPlexSansCondensed-Medium.ttf", "plex-heading", headingFont);
            loadFont(directory / "fonts/plex/IBMPlexMono-Regular.ttf", "plex-mono", numberFont);
            // Register custom palette properties before any document loads the
            // shared stylesheet, including a main menu opened before the HUD.
            registerCommandWheelElements();
            registerMapViewElement();
            Rml::StyleSheetSpecification::RegisterProperty("hud-backdrop-color", "#2b2d2f", false).AddParser("color");
        } catch (...) { shutdown(); throw; }
    }
    ~Impl() { shutdown(); }
    void shutdown() {
        if (initialized) { Rml::Shutdown(); initialized = false; }
        Rml::SetRenderInterface(nullptr);
        Rml::SetSystemInterface(nullptr);
    }
    static void loadFont(const std::filesystem::path& path, const char* family, std::vector<Rml::byte>& bytes) {
        std::ifstream input(path, std::ios::binary);
        if (!input) throw std::runtime_error("Cannot read UI font: " + path.string());
        bytes.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        if (!Rml::LoadFontFace(Rml::Span<const Rml::byte>(bytes.data(), bytes.size()), family,
                              Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal))
            throw std::runtime_error("Cannot load UI font: " + path.string());
        std::cout << "UI font: " << path << '\n';
    }
};

std::shared_ptr<UiRuntime> UiRuntime::acquire(const std::filesystem::path& directory) {
    static std::weak_ptr<UiRuntime> active;
    if (auto runtime = active.lock()) return runtime;
    auto runtime = std::shared_ptr<UiRuntime>(new UiRuntime(directory));
    active = runtime;
    return runtime;
}
UiRuntime::UiRuntime(const std::filesystem::path& directory) : impl_(std::make_unique<Impl>(directory)) {}
UiRuntime::~UiRuntime() = default;
HudRenderInterface& UiRuntime::renderer() { return impl_->renderer; }
void UiRuntime::loadMenuFont(const std::filesystem::path& path) {
    if (impl_->menuFont.empty()) Impl::loadFont(path, "menu-pixel", impl_->menuFont);
}
} // namespace viewer
