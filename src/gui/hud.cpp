#include "hud.hpp"
#include "hud_render_interface.hpp"
#include "inventory_view.hpp"
#include "inventory_interaction.hpp"
#include "action_hotbar.hpp"
#include "skills_view.hpp"
#include "inventory/player_inventory.hpp"
#include <RmlUi/Core.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
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

class SatelliteMapElement final : public Rml::Element {
public:
    explicit SatelliteMapElement(const Rml::String& tag) : Rml::Element(tag), fullImage_(tag == "satellite-map") {}
    HudRenderInterface* renderer = nullptr; // Owned by the HUD; outlives this element.
protected:
    void OnRender() override {
        if (!renderer) return;
        const auto offset = GetAbsoluteOffset(Rml::BoxArea::Content), size = GetBox().GetSize(Rml::BoxArea::Content);
        if (fullImage_) renderer->drawSatelliteMap(offset, size);
        else renderer->drawMinimap(offset, size);
    }
private:
    bool fullImage_;
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
    Rml::ElementInstancerGeneric<SatelliteMapElement> mapInstancer;
    // FreeType borrows these bytes until Rml::Shutdown().
    std::vector<Rml::byte> font;
    Rml::Context* context = nullptr;
    Rml::ElementDocument* document = nullptr;
    std::array<Rml::ElementDocument*, 4> screens{};
    PlayerInventory& inventory;
    std::unique_ptr<InventoryInteraction> inventoryInteraction;
    std::vector<InventoryPointerEvent> pointerEvents;
    bool hasSatelliteImage = false;
    std::filesystem::path sourceDirectory;
    HudScreen screen = HudScreen::Gameplay;
    std::unordered_map<std::string, Rml::Element*> elements;
    std::unordered_map<std::string, std::string> text;
    std::array<Rml::Element*, 24> ticks{};
    std::array<Rml::Element*, 6> directions{};
    int width = 0, height = 0, slot = -2, bearing = -1, selectionSlot = -1;
    bool initialized = false;

    Impl(const std::filesystem::path& directory, PlayerInventory& playerInventory)
        : renderer(directory), inventory(playerInventory), sourceDirectory(directory) {
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
            Rml::Factory::RegisterElementInstancer("minimap", &mapInstancer);
            Rml::Factory::RegisterElementInstancer("satellite-map", &mapInstancer);
            document = context->LoadDocument((directory / "hud.rml").generic_string());
            if (!document) throw std::runtime_error("Cannot load HUD layout: " + directory.string());
            for (const char* id : {"heading", "north-coordinate", "east-coordinate", "movement", "mode", "speed",
                                    "equipment", "weapon", "loaded", "reserve", "prompt", "capture", "compass-scale", "map-caption", "switch-fill", "switch-countdown"}) {
                auto* element = document->GetElementById(id);
                if (!element) throw std::runtime_error(std::string("Missing HUD element: ") + id);
                elements.emplace(id, element);
            }
            auto* minimap = dynamic_cast<SatelliteMapElement*>(document->GetElementById("minimap"));
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
        inventoryInteraction.reset();
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
    void showScreen(HudScreen next) {
        if (screen == next) return;
        const auto screenDocument = [&](HudScreen selected) {
            if (selected == HudScreen::Gameplay) return document;
            constexpr std::array<const char*, 4> files{"inventory.rml", "skills.rml", "health.rml", "map.rml"};
            const auto index = static_cast<std::size_t>(selected) - 1;
            auto*& page = screens.at(index);
            if (!page) {
                const char* file = files.at(index);
                page = context->LoadDocument((sourceDirectory / file).generic_string());
                if (!page) throw std::runtime_error(std::string("Cannot load HUD screen: ") + file);
                if (selected == HudScreen::Inventory || selected == HudScreen::Skills) {
                    populateActionHotbar(*page, inventory);
                }
                if (selected == HudScreen::Inventory)
                    inventoryInteraction = std::make_unique<InventoryInteraction>(*page, inventory);
                if (selected == HudScreen::Skills)
                    populateMeleeSkills(*page, loadMeleeSkills(sourceDirectory.parent_path()));
                if (selected == HudScreen::Map) {
                    auto* map = dynamic_cast<SatelliteMapElement*>(page->GetElementById("satellite-overview"));
                    if (!map) throw std::runtime_error("Missing full satellite map element");
                    map->renderer = &renderer;
                    page->SetClass("has-satellite", hasSatelliteImage);
                }
            }
            return page;
        };
        auto* nextDocument = screenDocument(next);
        if (screen == HudScreen::Inventory) {
            inventoryInteraction->cancel();
            pointerEvents.clear();
        }
        screenDocument(screen)->Hide();
        nextDocument->Show();
        screen = next;
    }
    void draw(const HudState& state, int newWidth, int newHeight) {
        if (newWidth <= 0 || newHeight <= 0) return;
        if (width != newWidth || height != newHeight) {
            width = newWidth; height = newHeight;
            context->SetDimensions({width, height});
            // Scale the complete 1280x720 layout together, retaining corner anchors.
            context->SetDensityIndependentPixelRatio(std::min(float(width) / 1280, float(height) / 720));
        }
        showScreen(state.screen);
        if (screen != HudScreen::Gameplay) {
            if (screen == HudScreen::Inventory)
                renderer.render(*context, width, height, [&] { inventoryInteraction->process(pointerEvents, width, height, state.seconds); });
            else if (screen == HudScreen::Skills)
                renderer.render(*context, width, height, [&] {
                    for (const auto& event : pointerEvents) {
                        if (event.action == InventoryPointerAction::Move)
                            context->ProcessMouseMove(static_cast<int>(event.x * width), static_cast<int>(event.y * height), 0);
                        else if (event.action == InventoryPointerAction::Wheel)
                            context->ProcessMouseWheel(-event.x, 0);
                    }
                });
            else renderer.render(*context, width, height);
            pointerEvents.clear();
            return;
        }
        updateCompass(state.yawDegrees);
        document->SetClass("selecting", state.selectionSlot >= 0 && state.selectionSlot < 10);
        if (state.selectionSlot != selectionSlot) {
            selectionSlot = state.selectionSlot;
            if (selectionSlot >= 0 && selectionSlot < 10) populateWeaponSelection(*document, inventory, selectionSlot);
        }
        const bool switching = state.weaponSwitchRemaining > 0 && state.weaponSwitchDuration > 0;
        document->SetClass("switching", switching);
        if (switching) {
            const double progress = std::clamp(1 - state.weaponSwitchRemaining / state.weaponSwitchDuration, 0.0, 1.0);
            elements.at("switch-fill")->SetProperty(Rml::PropertyId::Width, Rml::Property(static_cast<float>(progress * 318), Rml::Unit::DP));
            std::ostringstream countdown;
            countdown << std::fixed << std::setprecision(1) << state.weaponSwitchRemaining << "s";
            setText("switch-countdown", countdown.str());
        }
        pointerEvents.clear();
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

Hud::Hud(const std::filesystem::path& directory, PlayerInventory& inventory)
    : impl_(std::make_unique<Impl>(directory, inventory)) {}
Hud::~Hud() = default;
void Hud::draw(const HudState& state, int width, int height) { impl_->draw(state, width, height); }
void Hud::pointerMove(float x, float y) {
    if (!std::isfinite(x) || !std::isfinite(y)) return;
    // Preserve the pointer path: moving away and back between rendered frames
    // must still be distinguishable from a click without dragging.
    impl_->pointerEvents.push_back({InventoryPointerAction::Move, x, y});
}
void Hud::pointerButton(int button, bool pressed) {
    if (button == 0) impl_->pointerEvents.push_back({pressed ? InventoryPointerAction::Press : InventoryPointerAction::Release});
    else if (button == 1 && pressed) cancelInventoryDrag();
}
void Hud::mouseWheel(float notches) {
    if (std::isfinite(notches)) impl_->pointerEvents.push_back({InventoryPointerAction::Wheel, std::clamp(notches, -32.f, 32.f)});
}
void Hud::cancelInventoryDrag() { impl_->pointerEvents.push_back({InventoryPointerAction::Cancel}); }
void Hud::setMinimap(const MinimapSource& source, unsigned int texture) {
    impl_->renderer.setMinimap(source, texture);
    impl_->hasSatelliteImage = texture != 0;
    if (auto* map = impl_->screens[3]) map->SetClass("has-satellite", impl_->hasSatelliteImage);
    impl_->setText("map-caption", texture ? "" : "NO MAP");
}
void Hud::setMinimapRoads(const Model& model) { impl_->renderer.setMinimapRoads(model); }
} // namespace viewer
