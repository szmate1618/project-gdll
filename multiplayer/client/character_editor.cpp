#include "character_editor.hpp"
#include "character_creation.hpp"
#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <array>
#include <GLFW/glfw3.h>
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
    enum class Action { None, New, Select, Back, Discard, Retire };
    enum class Dialog { None, Unsaved, Retire };
    Rml::ElementDocument& document;
    CharacterCreation& creation;
    std::array<Rml::ElementFormControlInput*, 3> inputs{};
    Rml::ElementFormControlInput* name = nullptr;
    bool syncing = false, inputDirty = false, backRequested = false, focusName = false, focusDialog = false;
    Dialog dialog = Dialog::None;
    Action pending = Action::None;
    std::string pendingId, error, rosterKey, dialogError;
    int dialogChoice = 0;

    Impl(Rml::ElementDocument& document, CharacterCreation& creation) : document(document), creation(creation) {
        name = dynamic_cast<Rml::ElementFormControlInput*>(&element("character-name"));
        if (!name) throw std::runtime_error("Missing character name input");
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
    void disable(const char* id, bool disabled) {
        if (disabled) element(id).SetAttribute("disabled", true); else element(id).RemoveAttribute("disabled");
    }
    bool colorsValid() const {
        for (const auto* field : inputs) if (!parseColor(field->GetValue())) return false;
        return true;
    }
    bool valid() const {
        if (!creation.hasCharacter() || !colorsValid()) return false;
        try { CharacterLibrary::checkedName(name->GetValue()); return true; } catch (const std::exception&) { return false; }
    }
    bool dirty() const { return creation.dirty() || inputDirty; }
    void roster() {
        std::string key = creation.selectedId() + ":" + creation.library().activeId();
        for (const auto& record : creation.library().characters()) key += "|" + record.id + ":" + record.name + hex(record.base) + hex(record.ink) + record.face;
        if (key == rosterKey) return;
        rosterKey = std::move(key);
        std::string rows;
        for (const auto& record : creation.library().characters()) {
            const bool selected = record.id == creation.selectedId(), active = record.id == creation.library().activeId();
            rows += "<button class=\"roster-character" + std::string(selected ? " selected" : "") + "\" data-character=\"" + record.id +
                "\"><span class=\"roster-avatar\" style=\"background-color:" + hex(record.base) + ";\"><img style=\"image-color:" + hex(record.ink) +
                ";\" src=\"../data/assets/game-icons/character-faces/" + record.face + ".png\" /><span class=\"roster-avatar-side\"></span></span><span class=\"roster-name\">" +
                Rml::StringUtilities::EncodeRml(record.name) + "</span>" + (active ? "<span class=\"roster-active\">IN USE</span>" : "") + "</button>";
        }
        if (rows.empty()) rows = "<div class=\"roster-empty\">No saved characters yet</div>";
        element("character-list").SetInnerRML(rows);
    }
    void refresh(bool replaceInputs = false) {
        syncing = true;
        const bool has = creation.hasCharacter();
        if (replaceInputs) { name->SetValue(creation.name()); inputDirty = false; }
        for (int role = 0; role < 3; ++role) {
            const auto value = hex(creation.color(targets[role]));
            if (replaceInputs && inputs[role]->GetValue() != value) inputs[role]->SetValue(value);
            inputs[role]->SetClass("invalid", has && !parseColor(inputs[role]->GetValue()));
            disable(fields[role], !has);
            element(samples[role]).SetProperty("background-color", value);
        }
        disable("character-name", !has);
        name->SetClass("invalid", has && dirty() && !valid() && colorsValid());
        for (std::size_t i = 0; i < creation.faces().size(); ++i)
            document.GetElementById("face-option-" + std::to_string(i))->SetClass("selected", i == creation.faceIndex());
        element("character-screen").SetClass("empty", !has);
        element("character-screen").SetClass("has-dialog", dialog != Dialog::None);
        text(element("face-name"), creation.faceName());
        std::string status = error.empty() ? creation.status() : error;
        if (has && !colorsValid()) status = "Use a six-digit hex color, e.g. #46B2BE";
        text(element("character-status"), status);
        element("character-status").SetClass("invalid", !error.empty() || (has && !colorsValid()));
        text(element("character-save"), creation.selectedId().empty() ? "SAVE CHARACTER" : "SAVE CHANGES");
        disable("character-save", !valid());
        disable("character-reset", !has);
        disable("character-cancel", !has);
        disable("character-retire", creation.selectedId().empty());
        const bool active = !creation.selectedId().empty() && creation.selectedId() == creation.library().activeId();
        text(element("character-use"), active ? "IN USE" : "USE CHARACTER");
        disable("character-use", creation.selectedId().empty() || dirty() || active);
        std::string activeName = "Choose a character before playing";
        if (!creation.library().activeId().empty()) activeName = "Next session: " + creation.library().get(creation.library().activeId()).name;
        text(element("active-character"), activeName);
        roster();
        if (dialog != Dialog::None) {
            text(element("dialog-title"), dialog == Dialog::Retire ? "Retire " + creation.name() + "?" : "Save changes to " + (creation.name().empty() ? "this character" : creation.name()) + "?");
            text(element("dialog-message"), dialog == Dialog::Retire ? "Remove this character from the current list. The saved file is kept in the retired folder." : "You have unsaved changes.");
            text(element("dialog-detail"), !dialogError.empty() ? dialogError : dialog == Dialog::Retire && active ? "This character is in use. Choose another before playing." : "");
            text(element("dialog-cancel"), dialog == Dialog::Retire ? "CANCEL" : "KEEP EDITING");
            text(element("dialog-discard"), dialog == Dialog::Retire ? "RETIRE CHARACTER" : "DISCARD CHANGES");
            element("dialog-save").SetClass("hidden", dialog == Dialog::Retire);
            disable("dialog-save", !valid());
        }
        syncing = false;
    }
    void open(Dialog next) {
        dialog = next; dialogChoice = 0; dialogError.clear(); creation.releasePointer();
        if (auto* focus = document.GetContext()->GetFocusElement()) focus->Blur();
        focusDialog = true; refresh();
    }
    bool save() {
        if (!valid()) { error = "Enter a name and valid colors before saving"; refresh(); return false; }
        if (!creation.save()) { error = creation.status(); refresh(); return false; }
        error.clear(); refresh(true); return true;
    }
    void perform(Action action, const std::string& id = {}) {
        error.clear();
        switch (action) {
        case Action::New: creation.newCharacter(); break;
        case Action::Select: creation.selectCharacter(id); break;
        case Action::Back: backRequested = true; break;
        case Action::Discard: creation.discardChanges(); break;
        case Action::Retire: open(Dialog::Retire); return;
        case Action::None: break;
        }
        refresh(true);
        if (action == Action::New) focusName = true;
    }
    void request(Action action, const std::string& id = {}) {
        if (dialog != Dialog::None) return;
        if (dirty()) { pending = action; pendingId = id; open(Dialog::Unsaved); }
        else perform(action, id);
    }
    void resolve(int choice) {
        try {
            if (choice == 0) { dialog = Dialog::None; pending = Action::None; refresh(); return; }
            if (dialog == Dialog::Retire) {
                creation.retireCharacter(); dialog = Dialog::None; refresh(true); return;
            }
            if (choice == 2 && !save()) { dialogError = error; refresh(); return; }
            if (choice == 1) creation.discardChanges();
            const auto action = pending; const auto id = pendingId;
            pending = Action::None; dialog = Dialog::None;
            perform(action, id);
        } catch (const std::exception& exception) { dialogError = exception.what(); refresh(); }
    }
    void ProcessEvent(Rml::Event& event) override {
        if (syncing) return;
        try {
            if (event.GetId() == Rml::EventId::Change && dialog == Dialog::None) {
                if (event.GetTargetElement() == name) { creation.setName(name->GetValue()); error.clear(); refresh(); return; }
                for (int role = 0; role < 3; ++role) if (event.GetTargetElement() == inputs[role]) {
                    inputDirty = true; error.clear();
                    if (const auto color = parseColor(inputs[role]->GetValue())) creation.setColor(targets[role], *color);
                    refresh(); return;
                }
            }
            if (event.GetId() != Rml::EventId::Click) return;
            for (auto* target = event.GetTargetElement(); target && target->GetId() != "character-screen"; target = target->GetParentNode()) {
                const auto& id = target->GetId();
                if (dialog != Dialog::None) {
                    if (id == "dialog-cancel") resolve(0);
                    else if (id == "dialog-discard") resolve(1);
                    else if (id == "dialog-save" && valid()) resolve(2);
                    continue;
                }
                if (id == "character-new" || id == "empty-create") { request(Action::New); return; }
                if (id == "character-back") { request(Action::Back); return; }
                if (id == "character-cancel") { request(Action::Discard); return; }
                if (id == "character-retire") { request(Action::Retire); return; }
                if (id == "character-use") { creation.useCharacter(); error.clear(); refresh(); return; }
                if (id == "character-reset") { creation.reset(); refresh(true); return; }
                if (id == "character-save") { save(); return; }
                if (target->HasAttribute("data-character")) {
                    const auto targetId = target->GetAttribute<Rml::String>("data-character", "");
                    if (targetId != creation.selectedId()) request(Action::Select, targetId);
                    return;
                }
                if (!creation.hasCharacter()) continue;
                if (target->HasAttribute("data-face")) { creation.selectFace(target->GetAttribute<std::size_t>("data-face", 0)); error.clear(); refresh(); return; }
                if (target->HasAttribute("data-color")) {
                    const auto color = parseColor(target->GetAttribute<Rml::String>("data-color", ""));
                    const auto role = target->GetAttribute<int>("data-role", -1);
                    if (color && role >= 0 && role < 3) { creation.setColor(targets[role], *color); error.clear(); refresh(true); }
                    return;
                }
            }
        } catch (const std::exception& exception) { error = exception.what(); refresh(); }
    }
    void key(int key, bool shift) {
        if (key == GLFW_KEY_ESCAPE) { resolve(0); return; }
        const int count = dialog == Dialog::Retire || !valid() ? 2 : 3;
        if (key == GLFW_KEY_TAB || key == GLFW_KEY_LEFT || key == GLFW_KEY_RIGHT) {
            dialogChoice = (dialogChoice + ((shift || key == GLFW_KEY_LEFT) ? count - 1 : 1)) % count;
            const std::array<const char*,3> buttons{"dialog-cancel", "dialog-discard", "dialog-save"};
            element(buttons[dialogChoice]).Focus();
        } else if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
            if (dialogChoice != 2 || valid()) resolve(dialogChoice);
        }
    }
};
CharacterEditor::CharacterEditor(Rml::ElementDocument& document, CharacterCreation& creation)
    : impl_(std::make_unique<Impl>(document, creation)) {}
CharacterEditor::~CharacterEditor() = default;
void CharacterEditor::refresh() { impl_->refresh(); }
void CharacterEditor::requestBack() { impl_->request(Impl::Action::Back); }
bool CharacterEditor::takeBackRequested() { const bool result = impl_->backRequested; impl_->backRequested = false; return result; }
bool CharacterEditor::dialogOpen() const { return impl_->dialog != Impl::Dialog::None; }
void CharacterEditor::dialogKey(int key, bool shift) { impl_->key(key, shift); }
void CharacterEditor::save() { impl_->save(); }
void CharacterEditor::newCharacter() { impl_->request(Impl::Action::New); }
bool CharacterEditor::previewInteractive() const { return impl_->creation.hasCharacter() && impl_->dialog == Impl::Dialog::None; }
void CharacterEditor::focusPendingInput() {
    if (!impl_->focusName && !impl_->focusDialog) return;
    // New can reveal a previously hidden name field. Wait until the click has
    // finished dispatching before updating layout and assigning keyboard focus.
    impl_->document.GetContext()->Update();
    if (impl_->focusDialog && impl_->dialog != Impl::Dialog::None) impl_->element("dialog-cancel").Focus();
    else if (impl_->focusName) {
        impl_->name->Focus();
        const int end = int(impl_->name->GetValue().size()); impl_->name->SetSelectionRange(end, end);
    }
    impl_->focusName = impl_->focusDialog = false;
}
} // namespace multiplayer
