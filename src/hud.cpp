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

class MinimapElement final : public Rml::Element {
public:
    explicit MinimapElement(const Rml::String& tag) : Rml::Element(tag) {}
    HudRenderInterface* renderer = nullptr; // Owned by the HUD; outlives this element.
protected:
    void OnRender() override {
        if (renderer) renderer->drawMinimap(GetAbsoluteOffset(Rml::BoxArea::Content), GetBox().GetSize(Rml::BoxArea::Content));
    }
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

std::string speed(float metresPerSecond) {
    std::ostringstream result;
    result << std::fixed << std::setprecision(1) << std::max(0.f, metresPerSecond) * 3.6f << " km/h";
    return result.str();
}
} // namespace

struct Hud::Impl {
    HudRenderInterface renderer;
    HudSystem system;
    Rml::ElementInstancerGeneric<MinimapElement> minimapInstancer;
    // FreeType borrows these bytes until Rml::Shutdown().
    std::vector<Rml::byte> font;
    Rml::Context* context = nullptr;
    Rml::ElementDocument* document = nullptr;
    std::unordered_map<std::string, Rml::Element*> elements;
    std::unordered_map<std::string, std::string> text;
    std::array<Rml::Element*, 24> ticks{};
    std::array<Rml::Element*, 6> directions{};
    int width = 0, height = 0, slot = -2, bearing = -1;
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
            Rml::Factory::RegisterElementInstancer("minimap", &minimapInstancer);
            document = context->LoadDocument((directory / "hud.rml").generic_string());
            if (!document) throw std::runtime_error("Cannot load HUD layout: " + directory.string());
            for (const char* id : {"heading", "north-coordinate", "east-coordinate", "movement", "mode", "speed",
                                    "equipment", "weapon", "loaded", "reserve", "prompt", "capture", "compass-scale", "map-caption"}) {
                auto* element = document->GetElementById(id);
                if (!element) throw std::runtime_error(std::string("Missing HUD element: ") + id);
                elements.emplace(id, element);
            }
            auto* minimap = dynamic_cast<MinimapElement*>(document->GetElementById("minimap"));
            if (!minimap) throw std::runtime_error("Missing HUD minimap element");
            minimap->renderer = &renderer;
            std::string scale = "<div class=\"compass-rule\"></div>";
            for (std::size_t i = 0; i < ticks.size(); ++i)
                scale += "<div class=\"compass-tick\" id=\"tick-" + std::to_string(i) + "\"></div>";
            for (std::size_t i = 0; i < directions.size(); ++i)
                scale += "<div class=\"compass-label\" id=\"direction-" + std::to_string(i) + "\"></div>";
            elements.at("compass-scale")->SetInnerRML(scale);
            for (std::size_t i = 0; i < ticks.size(); ++i)
                ticks[i] = document->GetElementById("tick-" + std::to_string(i));
            for (std::size_t i = 0; i < directions.size(); ++i) {
                directions[i] = document->GetElementById("direction-" + std::to_string(i));
                elements.emplace("direction-" + std::to_string(i), directions[i]);
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
    void updateCompass(float yaw) {
        const int next = (static_cast<int>(std::lround(yaw + 90)) % 360 + 360) % 360;
        if (next == bearing) return;
        bearing = next;
        setText("heading", std::to_string(bearing) + "°");
        // Keep unwrapped angles for placement so crossing north scrolls smoothly.
        const int firstTick = static_cast<int>(std::floor((bearing - 111) / 10.f)) * 10;
        for (std::size_t i = 0; i < ticks.size(); ++i) {
            const int angle = firstTick + static_cast<int>(i) * 10;
            ticks[i]->SetProperty(Rml::PropertyId::Left, Rml::Property(222.f + (angle - bearing) * 2.f, Rml::Unit::DP));
            ticks[i]->SetClass("major", angle % 30 == 0);
        }
        constexpr std::array<const char*, 8> names{"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
        const int firstDirection = static_cast<int>(std::floor((bearing - 111) / 45.f));
        for (std::size_t i = 0; i < directions.size(); ++i) {
            const int index = firstDirection + static_cast<int>(i);
            const int offset = index * 45 - bearing;
            directions[i]->SetProperty(Rml::PropertyId::Left, Rml::Property(222.f + offset * 2.f, Rml::Unit::DP));
            directions[i]->SetClass("covered", std::abs(offset) < 20);
            setText("direction-" + std::to_string(i), names[(index % 8 + 8) % 8]);
        }
    }
    void draw(const HudState& state, int newWidth, int newHeight) {
        if (newWidth <= 0 || newHeight <= 0) return;
        if (width != newWidth || height != newHeight) {
            width = newWidth; height = newHeight;
            context->SetDimensions({width, height});
            // Scale the complete 1280x720 layout together, retaining corner anchors.
            context->SetDensityIndependentPixelRatio(std::min(float(width) / 1280, float(height) / 720));
        }
        updateCompass(state.yawDegrees);
        renderer.setMinimapPosition({state.position.x, state.position.z});
        // Coordinates remain local metres; the reference's latitude/longitude
        // cannot be inferred from a runtime position without map georeferencing.
        setText("north-coordinate", "N " + std::to_string(std::lround(-state.position.z)) + " m");
        setText("east-coordinate", "E " + std::to_string(std::lround(state.position.x)) + " m");
        setText("movement", state.movement);
        setText("mode", state.mode);
        setText("speed", speed(state.speedMetersPerSecond));
        setText("equipment", state.equipment);
        const auto ammo = [](std::optional<int> count) {
            if (!count) return std::string("--");
            return (*count < 10 ? "0" : "") + std::to_string(*count);
        };
        setText("loaded", ammo(state.loaded));
        setText("reserve", ammo(state.reserve));
        setText("capture", state.mouseCaptured ? "" : "CURSOR FREE");
        setText("prompt", state.prompt);
        if (slot != state.slot) {
            slot = state.slot;
            constexpr std::array<const char*, 6> classes{"pistol", "smg", "shotgun", "sniper", "grenade", "fists"};
            elements.at("weapon")->SetClassNames(slot >= 0 && slot < 6 ? classes[slot] : "unconfirmed");
        }
        renderer.render(*context, width, height);
    }
};

Hud::Hud(const std::filesystem::path& directory) : impl_(std::make_unique<Impl>(directory)) {}
Hud::~Hud() = default;
void Hud::draw(const HudState& state, int width, int height) { impl_->draw(state, width, height); }
void Hud::setMinimap(const MinimapSource& source, unsigned int texture) {
    impl_->renderer.setMinimap(source, texture);
    impl_->setText("map-caption", texture ? "200 x 200 m" : "NO MAP");
}
} // namespace viewer
