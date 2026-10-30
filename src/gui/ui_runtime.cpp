#include "ui_runtime.hpp"
#include "command_wheel.hpp"
#include "map_view.hpp"
#include <RmlUi/Core.h>
#include <array>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

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

std::filesystem::path systemFont() {
    if (const auto* override = std::getenv("GD_HUD_FONT")) return override;
#ifdef _WIN32
    std::array<wchar_t, MAX_PATH> windows{};
    const auto length = GetWindowsDirectoryW(windows.data(), static_cast<UINT>(windows.size()));
    if (length && length < windows.size()) return std::filesystem::path(windows.data()) / "Fonts/segoeui.ttf";
#else
    for (const char* path : {
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
            "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
            "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
            "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
            "/usr/share/fonts/TTF/DejaVuSans.ttf",
            "/usr/share/fonts/noto/NotoSans-Regular.ttf"})
        if (std::filesystem::is_regular_file(path)) return path;
#endif
    throw std::runtime_error("No HUD system font found. Set GD_HUD_FONT to an installed .ttf font.");
}

} // namespace

struct UiRuntime::Impl {
    HudRenderInterface renderer;
    UiSystem system;
    std::vector<Rml::byte> font, menuFont;
    bool initialized = false;

    explicit Impl(const std::filesystem::path& directory) : renderer(directory) {
        Rml::SetSystemInterface(&system);
        Rml::SetRenderInterface(&renderer);
        try {
            if (!Rml::Initialise()) throw std::runtime_error("Cannot initialize RmlUi");
            initialized = true;
            loadFont(systemFont(), "hud-system", font);
            // Register custom palette properties before any document loads the
            // shared stylesheet, including a main menu opened before the HUD.
            registerCommandWheelElements();
            registerMapViewElement();
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
