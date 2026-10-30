#include "character_editor.hpp"
#include "character_creation.hpp"
#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <array>
#include <charconv>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace multiplayer {
namespace {
constexpr std::array<const char*, 3> fields{"base-color", "brush-color", "face-color"};
constexpr std::array<const char*, 3> palettes{"base-palette", "brush-palette", "face-palette"};
constexpr std::array<const char*, 3> samples{"base-sample", "brush-sample", "face-sample"};
constexpr std::array<CharacterColorTarget, 3> targets{CharacterColorTarget::Base, CharacterColorTarget::Brush, CharacterColorTarget::Face};
constexpr std::array<const char*, 8> presets{"46B2BE", "EB4370", "121C24", "F5F6F2", "59C49C", "E7CD65", "8F979E", "B29A85"};
std::optional<CharacterCanvas::Color> parseColor(std::string text) {
    if (!text.empty() && text.front() == '#') text.erase(0, 1);
    if (text.size() != 6) return {};
    unsigned value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value, 16);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) return {};
    return CharacterCanvas::Color{std::uint8_t(value >> 16), std::uint8_t(value >> 8), std::uint8_t(value), 255};
}
std::string hex(CharacterCanvas::Color color) {
    std::ostringstream text; text << '#' << std::hex << std::uppercase << std::setfill('0');
    for (int i = 0; i < 3; ++i) text << std::setw(2) << int(color[i]);
    return text.str();
}
void text(Rml::Element& element, const std::string& value) {
    const auto encoded = Rml::StringUtilities::EncodeRml(value);
    if (element.GetInnerRML() != encoded) element.SetInnerRML(encoded);
}
} // namespace

struct CharacterEditor::Impl final : Rml::EventListener {
    Rml::ElementDocument& document;
    CharacterCreation& creation;
    std::array<Rml::ElementFormControlInput*, 3> inputs{};
    bool syncing = false;
    Impl(Rml::ElementDocument& document, CharacterCreation& creation) : document(document), creation(creation) {
        for (int role = 0; role < 3; ++role) {
            inputs[role] = dynamic_cast<Rml::ElementFormControlInput*>(&element(fields[role]));
            if (!inputs[role]) throw std::runtime_error("Missing character color input");
            std::string buttons;
            for (const auto* preset : presets)
                buttons += "<button class=\"color-chip\" data-role=\"" + std::to_string(role) +
                    "\" data-color=\"" + preset + "\" title=\"#" + preset + "\" style=\"background-color:#" + preset + ";\"></button>";
            element(palettes[role]).SetInnerRML(buttons);
        }
        std::string icons;
        for (std::size_t i = 0; i < creation.faces().size(); ++i) {
            const auto& face = creation.faces()[i];
            icons += "<button id=\"face-option-" + std::to_string(i) + "\" class=\"face-option\" data-face=\"" + std::to_string(i) +
                "\" title=\"" + Rml::StringUtilities::EncodeRml(face.name) + "\"><img src=\"../data/assets/game-icons/character-faces/" + face.id + ".png\" /></button>";
        }
        element("face-options").SetInnerRML(icons);
        refresh(true);
        element("character-screen").AddEventListener("click", this);
        element("character-screen").AddEventListener("change", this);
    }
    ~Impl() override {
        element("character-screen").RemoveEventListener("click", this);
        element("character-screen").RemoveEventListener("change", this);
    }
    Rml::Element& element(const char* id) const {
        auto* result = document.GetElementById(id);
        if (!result) throw std::runtime_error(std::string("Missing character editor element: ") + id);
        return *result;
    }
    bool valid() const {
        for (const auto* field : inputs) if (!parseColor(field->GetValue())) return false;
        return true;
    }
    void refresh(bool replaceInputs = false) {
        syncing = true;
        for (int role = 0; role < 3; ++role) {
            const auto value = hex(creation.color(targets[role]));
            if (replaceInputs)
                if (inputs[role]->GetValue() != value) inputs[role]->SetValue(value);
            inputs[role]->SetClass("invalid", !parseColor(inputs[role]->GetValue()));
            element(samples[role]).SetProperty("background-color", value);
        }
        for (std::size_t i = 0; i < creation.faces().size(); ++i)
            document.GetElementById("face-option-" + std::to_string(i))->SetClass("selected", i == creation.faceIndex());
        text(element("face-name"), creation.faceName());
        const bool correct = valid();
        text(element("character-status"), correct ? creation.status() : "Use a six-digit hex color, e.g. #46B2BE");
        element("character-status").SetClass("invalid", !correct);
        if (correct) element("character-save").RemoveAttribute("disabled");
        else element("character-save").SetAttribute("disabled", true);
        syncing = false;
    }
    void ProcessEvent(Rml::Event& event) override {
        if (syncing) return;
        try {
            if (event.GetId() == Rml::EventId::Change) {
                for (int role = 0; role < 3; ++role) if (event.GetTargetElement() == inputs[role]) {
                    if (const auto color = parseColor(inputs[role]->GetValue())) creation.setColor(targets[role], *color);
                    refresh(); return;
                }
            }
            if (event.GetId() != Rml::EventId::Click) return;
            for (auto* target = event.GetTargetElement(); target && target->GetId() != "character-screen"; target = target->GetParentNode()) {
                if (target->GetId() == "character-reset") { creation.reset(); refresh(true); return; }
                if (target->GetId() == "character-save") { if (valid()) creation.save(); refresh(); return; }
                if (target->HasAttribute("data-face")) {
                    creation.selectFace(target->GetAttribute<std::size_t>("data-face", 0)); refresh(); return;
                }
                if (target->HasAttribute("data-color")) {
                    const auto color = parseColor(target->GetAttribute<Rml::String>("data-color", ""));
                    const auto role = target->GetAttribute<int>("data-role", -1);
                    if (color && role >= 0 && role < 3) { creation.setColor(targets[role], *color); refresh(true); }
                    return;
                }
            }
        } catch (const std::exception& error) { text(element("character-status"), error.what()); }
    }
};
CharacterEditor::CharacterEditor(Rml::ElementDocument& document, CharacterCreation& creation)
    : impl_(std::make_unique<Impl>(document, creation)) {}
CharacterEditor::~CharacterEditor() = default;
void CharacterEditor::refresh() { impl_->refresh(); }
} // namespace multiplayer
