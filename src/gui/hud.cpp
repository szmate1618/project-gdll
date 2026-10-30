#include "hud.hpp"
#include "hud_render_interface.hpp"
#include "hud_backdrop.hpp"
#include "ui_runtime.hpp"
#include "inventory_view.hpp"
#include "inventory_interaction.hpp"
#include "action_hotbar.hpp"
#include "skills_view.hpp"
#include "loadout_navigation.hpp"
#include "gui/map_view.hpp"
#include "presentation_viewport.hpp"
#include "inventory/player_inventory.hpp"
#include "player_vitals.hpp"
#include <RmlUi/Core.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace viewer {
namespace {
bool hudActive = false; // One gameplay HUD at a time, sharing the application UI runtime.

struct BackdropGroup { const char* id; bool textOnly; };
constexpr std::array<BackdropGroup, 18> backdropGroups{{
    {"compass", false}, {"coordinates", false}, {"map-caption", true}, {"vitals", false},
    {"mode-status", false}, {"movement-status", false}, {"speed-status", false}, {"weapon", false},
    {"prompt", true}, {"capture", true}, {"selection-hint", true}, {"weapon-switch", false},
    {"command-title", true}, {"command-hint", true}, {"command-cancel-hint", true},
    {"command-danger", false}, {"command-move", false}, {"command-backup", false}
}};
static_assert(backdropGroups.size() == HudBackdrop::maxGroups);

// Text labels use their laid-out lines, avoiding a wide panel across an
// otherwise empty centered container. All bounds are framebuffer pixels, Y down.
glm::vec4 backdropBounds(Rml::Element& element, bool textOnly) {
    if (!element.IsVisible(true)) return {};
    if (!textOnly) {
        const auto p = element.GetAbsoluteOffset(Rml::BoxArea::Border);
        const auto size = element.GetBox().GetSize(Rml::BoxArea::Border);
        return {p.x, p.y, size.x, size.y};
    }
    glm::vec2 minimum{0}, maximum{0};
    bool found = false;
    for (int childIndex = 0; childIndex < element.GetNumChildren(); ++childIndex) {
        auto* child = dynamic_cast<Rml::ElementText*>(element.GetChild(childIndex));
        if (!child || child->GetText().empty()) continue;
        const auto absolute = child->GetAbsoluteOffset();
        const auto& metrics = Rml::GetFontEngineInterface()->GetFontMetrics(child->GetFontFaceHandle());
        for (const auto& line : child->GetLines()) {
            // RmlUi fills line.width during rendering, after our underlay.
            // Measure new lines once; subsequent frames reuse the rendered width.
            const float lineWidth = line.width > 0 ? line.width : Rml::ElementUtilities::GetStringWidth(child, line.text);
            const glm::vec2 size{lineWidth, metrics.ascent + metrics.descent};
            if (size.x <= 0 || size.y <= 0) continue;
            const glm::vec2 p{absolute.x + line.position.x, absolute.y + line.position.y - metrics.ascent};
            minimum = found ? glm::min(minimum, p) : p;
            maximum = found ? glm::max(maximum, p + size) : p + size;
            found = true;
        }
    }
    return found ? glm::vec4(minimum, maximum - minimum) : glm::vec4(0);
}

class SatelliteMapElement final : public Rml::Element {
public:
    explicit SatelliteMapElement(const Rml::String& tag) : Rml::Element(tag) {}
    HudRenderInterface* renderer = nullptr; // Shared UI runtime; outlives this element.
protected:
    void OnRender() override {
        if (!renderer) return;
        const auto offset = GetAbsoluteOffset(Rml::BoxArea::Content), size = GetBox().GetSize(Rml::BoxArea::Content);
        renderer->drawMinimap(offset, size);
    }
};

std::string speed(float metresPerSecond) {
    std::ostringstream result;
    result << std::fixed << std::setprecision(1) << std::max(0.f, metresPerSecond) * 3.6f << " km/h";
    return result.str();
}
} // namespace

struct Hud::Impl {
    std::shared_ptr<UiRuntime> runtime;
    HudRenderInterface& renderer;
    Rml::Context* context = nullptr;
    Rml::ElementDocument* document = nullptr;
    std::array<Rml::ElementDocument*, 4> screens{};
    PlayerInventory& inventory;
    std::unique_ptr<InventoryInteraction> inventoryInteraction;
    std::unique_ptr<MapViewUi> mapView;
    std::optional<MinimapSource> mapSource;
    std::unique_ptr<CommandWheelView> commandWheel;
    std::unique_ptr<HudBackdrop> backdrop;
    std::array<Rml::Element*, backdropGroups.size()> backdropElements{};
    std::optional<double> lastBackdropTime;
    Rml::PropertyId backdropColorProperty = Rml::PropertyId::Invalid;
    std::unique_ptr<LoadoutNavigation> navigation;
    std::vector<HudPointerEvent> pointerEvents;
    bool hasSatelliteImage = false;
    std::filesystem::path sourceDirectory;
    HudScreen screen = HudScreen::Gameplay;
    std::unordered_map<std::string, Rml::Element*> elements;
    std::unordered_map<std::string, std::string> text;
    std::array<Rml::Element*, 24> ticks{};
    std::array<Rml::Element*, 6> directions{};
    int width = 0, height = 0, slot = -2, bearing = -1, selectionSlot = -1;

    Impl(const std::filesystem::path& directory, PlayerInventory& playerInventory)
        : runtime(UiRuntime::acquire(directory)), renderer(runtime->renderer()), inventory(playerInventory), sourceDirectory(directory) {
        if (hudActive) throw std::runtime_error("Only one HUD may own RmlUi at a time");
        try {
            context = Rml::CreateContext("game-hud", {1280, 720});
            if (!context) throw std::runtime_error("Cannot create HUD context");
            hudActive = true;
            navigation = std::make_unique<LoadoutNavigation>(*context);
            static Rml::ElementInstancerGeneric<SatelliteMapElement> mapInstancer;
            Rml::Factory::RegisterElementInstancer("minimap", &mapInstancer);
            document = context->LoadDocument((directory / "hud.rml").generic_string());
            if (!document) throw std::runtime_error("Cannot load HUD layout: " + directory.string());
            backdropColorProperty = Rml::StyleSheetSpecification::GetPropertyId("hud-backdrop-color");
            for (const char* id : {"heading", "north-coordinate", "east-coordinate", "movement", "mode", "speed",
                                    "equipment", "weapon", "mode-icon", "movement-icon", "loaded", "reserve", "prompt", "capture", "compass-scale", "map-caption", "switch-fill", "switch-countdown", "switch-label", "health-value", "health-fill", "damage-feedback"}) {
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
            commandWheel = std::make_unique<CommandWheelView>(*document);
            for (std::size_t i = 0; i < backdropGroups.size(); ++i) {
                backdropElements[i] = document->GetElementById(backdropGroups[i].id);
                if (!backdropElements[i]) throw std::runtime_error(std::string("Missing HUD backdrop group: ") + backdropGroups[i].id);
            }
            document->Show();
        } catch (...) {
            shutdown();
            throw;
        }
    }
    ~Impl() { shutdown(); }
    void shutdown() {
        navigation.reset();
        commandWheel.reset();
        mapView.reset();
        inventoryInteraction.reset();
        if (context) {
            Rml::RemoveContext("game-hud"); context = nullptr; hudActive = false;
            renderer.clearMinimap();
        }
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
                    mapView = std::make_unique<MapViewUi>(*page, renderer);
                    mapView->setSource(mapSource);
                    page->SetClass("has-satellite", hasSatelliteImage);
                }
            }
            return page;
        };
        auto* nextDocument = screenDocument(next);
        navigation->cancel();
        if (screen == HudScreen::Inventory) {
            inventoryInteraction->cancel();
            pointerEvents.clear();
        }
        if (screen == HudScreen::Map) { mapView->cancel(); pointerEvents.clear(); }
        screenDocument(screen)->Hide();
        nextDocument->Show();
        screen = next;
    }
    void draw(const HudState& state, int newWidth, int newHeight, int presentationWidth, int presentationHeight) {
        if (newWidth <= 0 || newHeight <= 0) return;
        if (width != newWidth || height != newHeight) {
            width = newWidth; height = newHeight;
            context->SetDimensions({width, height});
            // Scale the complete 1280x720 layout together, retaining corner anchors.
            context->SetDensityIndependentPixelRatio(std::min(float(width) / 1280, float(height) / 720));
        }
        showScreen(state.screen);
        const auto presentation = presentationViewport(width, height,
            presentationWidth > 0 ? presentationWidth : width, presentationHeight > 0 ? presentationHeight : height);
        if (screen != HudScreen::Gameplay) {
            if (backdrop) backdrop->reset();
            lastBackdropTime.reset();
            renderer.render(*context, width, height, [&] {
                const auto events = navigation->process(*screens.at(static_cast<std::size_t>(screen) - 1), screen,
                    pointerEvents, width, height, presentation);
                if (screen == HudScreen::Inventory) inventoryInteraction->process(events, width, height, presentation, state.seconds);
                else if (screen == HudScreen::Map) mapView->process(events, width, height, presentation, state);
            });
            pointerEvents.clear();
            return;
        }
        commandWheel->update(state.commandWheelOpen, state.commandWheelOffset, state.signals);
        updateCompass(state.yawDegrees);
        const bool healthKnown = state.health && std::isfinite(*state.health);
        document->SetClass("health-known", healthKnown);
        float health = 0;
        if (healthKnown) {
            health = std::clamp(*state.health, 0.f, PlayerVitals::maximumHealth);
            std::ostringstream value;
            value << std::fixed << std::setprecision(1) << health;
            auto label = value.str();
            if (label.size() >= 2 && label.substr(label.size() - 2) == ".0") label.resize(label.size() - 2);
            setText("health-value", label);
        } else setText("health-value", "--");
        elements.at("health-fill")->SetProperty(Rml::PropertyId::Width,
            Rml::Property(220.f * health / PlayerVitals::maximumHealth, Rml::Unit::DP));
        constexpr double damagePulseSeconds = .3;
        const double damageAge = state.seconds - state.lastDamageSeconds;
        const float damagePulse = state.lastDamageSeconds >= 0 && std::isfinite(damageAge) && damageAge >= 0 ?
            static_cast<float>(std::clamp(1 - damageAge / damagePulseSeconds, 0.0, 1.0)) : 0.f;
        document->SetClass("damaged", damagePulse > 0);
        elements.at("damage-feedback")->SetProperty(Rml::PropertyId::Opacity,
            Rml::Property(damagePulse * .45f, Rml::Unit::NUMBER));
        document->SetClass("selecting", state.selectionSlot >= 0 && state.selectionSlot < 10);
        if (state.selectionSlot != selectionSlot) {
            selectionSlot = state.selectionSlot;
            if (selectionSlot >= 0 && selectionSlot < 10) populateWeaponSelection(*document, inventory, selectionSlot);
        }
        const bool reloading = state.reloadRemaining > 0 && state.reloadDuration > 0;
        const double remaining = reloading ? state.reloadRemaining : state.weaponSwitchRemaining;
        const double duration = reloading ? state.reloadDuration : state.weaponSwitchDuration;
        const bool progressing = remaining > 0 && duration > 0;
        document->SetClass("switching", progressing);
        if (progressing) {
            setText("switch-label", reloading ? "RELOADING" : "SWITCHING WEAPON");
            const double progress = std::clamp(1 - remaining / duration, 0.0, 1.0);
            elements.at("switch-fill")->SetProperty(Rml::PropertyId::Width, Rml::Property(static_cast<float>(progress * 318), Rml::Unit::DP));
            std::ostringstream countdown;
            countdown << std::fixed << std::setprecision(1) << remaining << "s";
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
        const std::string modeClasses = state.mode == "INTERACT" ? "hud-icon interact" : "hud-icon combat";
        if (elements.at("mode-icon")->GetClassNames() != modeClasses)
            elements.at("mode-icon")->SetClassNames(modeClasses);
        const auto movementClass = state.movement == "CROUCH" ? "crouch" : state.movement == "PRONE" ? "prone" :
            state.movement == "DRIVING" ? "driving" : state.movement == "PASSENGER" ? "passenger" :
            state.movement == "FREE FLY" ? "free-fly" : "stand";
        const std::string movementClasses = std::string("hud-icon ") + movementClass;
        if (elements.at("movement-icon")->GetClassNames() != movementClasses)
            elements.at("movement-icon")->SetClassNames(movementClasses);
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
        renderer.render(*context, width, height, {}, [&] {
            if (!backdrop) renderer.renderExternal([&] { backdrop = std::make_unique<HudBackdrop>(sourceDirectory); });
            HudBackdrop::Bounds bounds{};
            for (std::size_t i = 0; i < backdropGroups.size(); ++i)
                bounds[i] = backdropBounds(*backdropElements[i], backdropGroups[i].textOnly);
            // Use presentation time so fading also works when simulation pauses.
            const double now = Rml::GetSystemInterface()->GetElapsedTime();
            const float delta = lastBackdropTime ? static_cast<float>(std::max(0.0, now - *lastBackdropTime)) : 0.f;
            lastBackdropTime = now;
            const auto color = document->GetProperty(backdropColorProperty)->Get<Rml::Colourb>();
            backdrop->draw(bounds, width, height, delta, glm::vec3(color.red, color.green, color.blue) / 255.f);
        });
    }
};

Hud::Hud(const std::filesystem::path& directory, PlayerInventory& inventory)
    : impl_(std::make_unique<Impl>(directory, inventory)) {}
Hud::~Hud() = default;
void Hud::draw(const HudState& state, int width, int height, int presentationWidth, int presentationHeight) {
    impl_->draw(state, width, height, presentationWidth, presentationHeight);
}
void Hud::pointerMove(float x, float y) {
    if (!std::isfinite(x) || !std::isfinite(y)) return;
    // Preserve the pointer path: moving away and back between rendered frames
    // must still be distinguishable from a click without dragging.
    impl_->pointerEvents.push_back({HudPointerAction::Move, x, y});
}
void Hud::pointerButton(int button, bool pressed) {
    if (button == 0) impl_->pointerEvents.push_back({pressed ? HudPointerAction::Press : HudPointerAction::Release});
    else if (button == 1 && pressed) impl_->pointerEvents.push_back({HudPointerAction::RightPress});
}
void Hud::mouseWheel(float notches) {
    if (std::isfinite(notches)) impl_->pointerEvents.push_back({HudPointerAction::Wheel, std::clamp(notches, -32.f, 32.f)});
}
void Hud::cancelInventoryDrag() { cancelPointerInteraction(); }
void Hud::cancelPointerInteraction() { impl_->pointerEvents.push_back({HudPointerAction::Cancel}); }
std::optional<HudScreen> Hud::takeScreenRequest() { return impl_->navigation->takeRequest(); }
std::vector<InventoryDragAction> Hud::takeInventoryActions() {
    return impl_->inventoryInteraction ? impl_->inventoryInteraction->takeActions() : std::vector<InventoryDragAction>{};
}
bool Hud::inventoryDragging() const {
    return impl_->inventoryInteraction && impl_->inventoryInteraction->dragging();
}
void Hud::refreshInventoryLayout() {
    if (impl_->inventoryInteraction) impl_->inventoryInteraction->refreshLayout();
}
void Hud::setMinimap(const MinimapSource& source, unsigned int texture) {
    impl_->renderer.setMinimap(source, texture);
    impl_->hasSatelliteImage = texture != 0;
    impl_->mapSource = texture ? std::optional<MinimapSource>(source) : std::nullopt;
    if (impl_->mapView) impl_->mapView->setSource(impl_->mapSource);
    if (auto* map = impl_->screens[3]) map->SetClass("has-satellite", impl_->hasSatelliteImage);
    impl_->setText("map-caption", texture ? "" : "NO MAP");
}
void Hud::setMinimapRoads(const Model& model) { impl_->renderer.setMinimapRoads(model); }
} // namespace viewer
