#include "hud.hpp"
#include "hud_render_interface.hpp"
#include <RmlUi/Core.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#ifdef _WIN32
#include <windows.h>
#endif

namespace viewer {
namespace {
bool hudActive = false; // The application owns RmlUi on its graphics thread.

class HudSystem final : public Rml::SystemInterface {
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

std::string coordinates(glm::vec3 position) {
    std::ostringstream result;
    // North is -Z in the runtime frame.
    result << "E " << std::lround(position.x) << " / N " << std::lround(-position.z);
    return result.str();
}

std::string heading(float yaw) {
    const int bearing = (static_cast<int>(std::lround(yaw + 90)) % 360 + 360) % 360;
    constexpr std::array<const char*, 8> directions{"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    std::ostringstream result;
    result << directions[((bearing + 22) / 45) % 8] << "  " << std::setfill('0') << std::setw(3) << bearing;
    return result.str();
}
} // namespace

struct Hud::Impl {
    HudRenderInterface renderer;
    HudSystem system;
    // FreeType borrows these bytes until Rml::Shutdown().
    std::vector<Rml::byte> font;
    Rml::Context* context = nullptr;
    Rml::ElementDocument* document = nullptr;
    std::unordered_map<std::string, Rml::Element*> elements;
    std::unordered_map<std::string, std::string> text;
    int width = 0, height = 0, slot = -2;
    bool initialized = false;

    explicit Impl(const std::filesystem::path& directory) : renderer(directory) {
        if (hudActive) throw std::runtime_error("Only one HUD may own RmlUi at a time");
        Rml::SetSystemInterface(&system);
        Rml::SetRenderInterface(&renderer);
        try {
            if (!Rml::Initialise()) throw std::runtime_error("Cannot initialize RmlUi");
            initialized = true;
            hudActive = true;
            const auto path = systemFont();
            std::ifstream input(path, std::ios::binary);
            if (!input) throw std::runtime_error("Cannot read HUD system font: " + path.string());
            font.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
            if (!Rml::LoadFontFace(Rml::Span<const Rml::byte>(font.data(), font.size()), "hud-system",
                                   Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal))
                throw std::runtime_error("Cannot load HUD system font: " + path.string());
            context = Rml::CreateContext("game-hud", {1280, 720});
            if (!context) throw std::runtime_error("Cannot create HUD context");
            document = context->LoadDocument((directory / "hud.rml").generic_string());
            if (!document) throw std::runtime_error("Cannot load HUD layout: " + directory.string());
            for (const char* id : {"heading", "coordinates", "movement", "mode", "equipment", "loaded", "reserve",
                                    "ammo-label", "prompt", "flashlight", "capture", "slot-0", "slot-1", "slot-2",
                                    "slot-3", "slot-4", "slot-5"}) {
                auto* element = document->GetElementById(id);
                if (!element) throw std::runtime_error(std::string("Missing HUD element: ") + id);
                elements.emplace(id, element);
            }
            document->Show();
            std::cout << "HUD system font: " << path << '\n';
        } catch (...) {
            shutdown();
            throw;
        }
    }
    ~Impl() { shutdown(); }
    void shutdown() {
        if (initialized) { Rml::Shutdown(); initialized = false; hudActive = false; }
        Rml::SetRenderInterface(nullptr);
        Rml::SetSystemInterface(nullptr);
    }
    void setText(const std::string& id, const std::string& value) {
        // Avoid invalidating text geometry/layout when the displayed value is unchanged.
        auto [entry, inserted] = text.emplace(id, value);
        if (!inserted && entry->second == value) return;
        entry->second = value;
        elements.at(id)->SetInnerRML(Rml::StringUtilities::EncodeRml(value));
    }
    void draw(const HudState& state, int newWidth, int newHeight) {
        if (newWidth <= 0 || newHeight <= 0) return;
        if (width != newWidth || height != newHeight) {
            width = newWidth; height = newHeight;
            context->SetDimensions({width, height});
            // Scale the complete 1280x720 layout together, retaining corner anchors.
            context->SetDensityIndependentPixelRatio(std::min(float(width) / 1280, float(height) / 720));
        }
        setText("heading", heading(state.yawDegrees));
        setText("coordinates", coordinates(state.position));
        setText("movement", state.movement);
        setText("mode", state.mode);
        setText("equipment", state.equipment);
        setText("loaded", state.loaded ? std::to_string(*state.loaded) : "--");
        setText("reserve", state.reserve ? std::to_string(*state.reserve) : "--");
        setText("ammo-label", state.slot == 5 ? "MELEE" : "LOADED / RESERVE");
        setText("flashlight", state.flashlight ? "LIGHT ON" : "LIGHT OFF");
        setText("capture", state.mouseCaptured ? "" : "CURSOR FREE");
        setText("prompt", state.prompt);
        if (slot != state.slot) {
            slot = state.slot;
            for (int index = 0; index < 6; ++index)
                elements.at("slot-" + std::to_string(index))->SetClass("selected", index == slot);
        }
        renderer.render(*context, width, height);
    }
};

Hud::Hud(const std::filesystem::path& directory) : impl_(std::make_unique<Impl>(directory)) {}
Hud::~Hud() = default;
void Hud::draw(const HudState& state, int width, int height) { impl_->draw(state, width, height); }
} // namespace viewer
