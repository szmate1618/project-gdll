#include "menu_view.hpp"
#include "gui/ui_runtime.hpp"
#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <stdexcept>
#include <vector>

namespace multiplayer {
namespace {
constexpr std::array<const char*, 5> actionIds{"play-single", "play-host", "play-join", "create-character", "quit-game"};
constexpr std::array<MenuCommand, 5> commands{MenuCommand::SinglePlayer, MenuCommand::Host, MenuCommand::Join,
                                           MenuCommand::Character, MenuCommand::Quit};
int modifiers(int mods) {
    return ((mods & GLFW_MOD_SHIFT) ? Rml::Input::KM_SHIFT : 0) |
           ((mods & GLFW_MOD_CONTROL) ? Rml::Input::KM_CTRL : 0) |
           ((mods & GLFW_MOD_ALT) ? Rml::Input::KM_ALT : 0) |
           ((mods & GLFW_MOD_SUPER) ? Rml::Input::KM_META : 0);
}
Rml::Input::KeyIdentifier inputKey(int code) {
    using namespace Rml::Input;
    if (code >= GLFW_KEY_A && code <= GLFW_KEY_Z) return KeyIdentifier(KI_A + code - GLFW_KEY_A);
    if (code >= GLFW_KEY_0 && code <= GLFW_KEY_9) return KeyIdentifier(KI_0 + code - GLFW_KEY_0);
    switch (code) {
    case GLFW_KEY_BACKSPACE: return KI_BACK;
    case GLFW_KEY_DELETE: return KI_DELETE;
    case GLFW_KEY_LEFT: return KI_LEFT;
    case GLFW_KEY_RIGHT: return KI_RIGHT;
    case GLFW_KEY_HOME: return KI_HOME;
    case GLFW_KEY_END: return KI_END;
    case GLFW_KEY_SPACE: return KI_SPACE;
    default: return KI_UNKNOWN;
    }
}
void setText(Rml::Element& element, const std::string& value) {
    const auto encoded = Rml::StringUtilities::EncodeRml(value);
    if (element.GetInnerRML() != encoded) element.SetInnerRML(encoded);
}
} // namespace

struct MenuView::Impl final : Rml::EventListener {
    std::shared_ptr<viewer::UiRuntime> runtime;
    Rml::Context* context = nullptr;
    Rml::ElementDocument* document = nullptr;
    Rml::ElementFormControlInput *port = nullptr, *address = nullptr;
    MenuScreen screen = MenuScreen::Choose;
    MenuCommand command = MenuCommand::None;
    int selected = 0, width = 0, height = 0;
    bool modern = false;
    struct Input {
        enum Kind { Key, Character, Move, Button, Blur } kind;
        int code = 0, action = 0, mods = 0;
        unsigned int codepoint = 0;
        float x = 0, y = 0;
    };
    std::vector<Input> inputs;

    explicit Impl(const std::filesystem::path& directory) : runtime(viewer::UiRuntime::acquire(directory)) {
        runtime->loadMenuFont(directory / "fonts/menu-pixel.ttf");
        context = Rml::CreateContext("main-menu", {960, 600});
        if (!context) throw std::runtime_error("Cannot create main menu context");
        try {
            document = context->LoadDocument((directory / "main-menu.rml").generic_string());
            if (!document) throw std::runtime_error("Cannot load main menu layout");
            port = dynamic_cast<Rml::ElementFormControlInput*>(&element("host-port"));
            address = dynamic_cast<Rml::ElementFormControlInput*>(&element("join-address"));
            if (!port || !address) throw std::runtime_error("Missing main menu connection inputs");
            document->AddEventListener("click", this);
            document->AddEventListener("mouseover", this);
            document->Show();
        } catch (...) { Rml::RemoveContext("main-menu"); context = nullptr; throw; }
    }
    ~Impl() override {
        // Remove listeners while their owner is still alive.
        document->RemoveEventListener("click", this);
        document->RemoveEventListener("mouseover", this);
        Rml::RemoveContext("main-menu");
    }
    Rml::Element& element(const char* id) const {
        auto* result = document->GetElementById(id);
        if (!result) throw std::runtime_error(std::string("Missing main menu element: ") + id);
        return *result;
    }
    void select(int row) {
        selected = row;
        for (int i = 0; i < int(actionIds.size()); ++i) element(actionIds[i]).SetClass("selected", i == row);
    }
    void theme(bool next) {
        modern = next;
        document->SetClass("classic", !modern);
        document->SetClass("modern", modern);
        element("theme-classic").SetClass("active", !modern);
        element("theme-modern").SetClass("active", modern);
    }
    void ProcessEvent(Rml::Event& event) override {
        for (auto* target = event.GetTargetElement(); target && target != document; target = target->GetParentNode()) {
            const auto& id = target->GetId();
            if (event.GetId() == Rml::EventId::Click) {
                if (id == "theme-classic") { theme(false); return; }
                if (id == "theme-modern") { theme(true); return; }
                if (id == "character-back") { command = MenuCommand::Back; return; }
                if (id == "loading-cancel") { command = MenuCommand::Cancel; return; }
            }
            if (screen != MenuScreen::Choose) continue;
            for (int i = 0; i < int(actionIds.size()); ++i) if (id == actionIds[i]) {
                select(i);
                if (event.GetId() == Rml::EventId::Click) command = commands[i];
                return;
            }
        }
    }
    void blur() {
        if (auto* focused = context->GetFocusElement()) focused->Blur();
    }
    bool editing() const {
        const auto* focus = context->GetFocusElement();
        return focus == port || focus == address;
    }
    void focusField(Rml::ElementFormControlInput& field) {
        field.Focus();
        const int end = int(field.GetValue().size());
        field.SetSelectionRange(end, end);
    }
    void key(const Input& input) {
        const int code = input.code, mods = modifiers(input.mods);
        const bool pressed = input.action != GLFW_RELEASE;
        if (pressed && code == GLFW_KEY_F6) { if (input.action == GLFW_PRESS) theme(!modern); return; }
        if (screen != MenuScreen::Choose) {
            if (pressed && code == GLFW_KEY_ESCAPE)
                command = screen == MenuScreen::Character ? MenuCommand::Back : MenuCommand::Cancel;
            return;
        }
        if (pressed && code == GLFW_KEY_TAB) {
            auto* focus = context->GetFocusElement();
            if (input.mods & GLFW_MOD_SHIFT) {
                if (focus == port) blur(); else if (focus == address) focusField(*port); else focusField(*address);
            } else {
                if (focus == port) focusField(*address); else if (focus == address) blur(); else focusField(*port);
            }
            return;
        }
        if (editing()) {
            if (pressed && (code == GLFW_KEY_ESCAPE || code == GLFW_KEY_ENTER || code == GLFW_KEY_KP_ENTER)) { blur(); return; }
            const auto translated = inputKey(code);
            if (translated != Rml::Input::KI_UNKNOWN) {
                if (pressed) context->ProcessKeyDown(translated, mods); else context->ProcessKeyUp(translated, mods);
            }
            return;
        }
        if (!pressed) return;
        if (code == GLFW_KEY_ESCAPE) command = MenuCommand::Quit;
        else if (code == GLFW_KEY_UP) select((selected + 4) % 5);
        else if (code == GLFW_KEY_DOWN) select((selected + 1) % 5);
        else if (code == GLFW_KEY_ENTER || code == GLFW_KEY_KP_ENTER) command = commands[selected];
        else if (code >= GLFW_KEY_1 && code <= GLFW_KEY_5) command = commands[code - GLFW_KEY_1];
    }
    void processInput() {
        for (const auto& input : inputs) {
            switch (input.kind) {
            case Input::Key: key(input); break;
            case Input::Character:
                if (screen == MenuScreen::Choose && editing() && input.codepoint >= 32 && input.codepoint <= 126)
                    context->ProcessTextInput(Rml::Character(input.codepoint));
                break;
            case Input::Move: context->ProcessMouseMove(int(input.x * width), int(input.y * height), 0); break;
            case Input::Button:
                if (input.action) context->ProcessMouseButtonDown(input.code, modifiers(input.mods));
                else context->ProcessMouseButtonUp(input.code, modifiers(input.mods));
                break;
            case Input::Blur: blur(); context->ProcessMouseLeave(); break;
            }
        }
        inputs.clear();
    }
};

MenuView::MenuView(const std::filesystem::path& directory) : impl_(std::make_unique<Impl>(directory)) {}
MenuView::~MenuView() = default;
void MenuView::connection(const std::string& port, const std::string& address) {
    impl_->port->SetValue(port); impl_->address->SetValue(address);
}
std::string MenuView::port() const { return impl_->port->GetValue(); }
std::string MenuView::address() const { return impl_->address->GetValue(); }
void MenuView::draw(MenuScreen screen, const std::string& message, const std::string& face, int width, int height) {
    auto& self = *impl_;
    if (self.screen != screen) { self.blur(); self.screen = screen; }
    self.document->SetClass("creating", screen == MenuScreen::Character);
    self.document->SetClass("loading", screen == MenuScreen::Loading);
    setText(self.element("menu-title"), screen == MenuScreen::Character ? "CHARACTER CREATION" : "GODOLLO");
    setText(self.element("menu-subtitle"), screen == MenuScreen::Character ? "YOUR LOCAL CHARACTER PREVIEW" : "CHOOSE YOUR NEXT SESSION");
    setText(self.element("menu-message"), message);
    setText(self.element("loading-message"), message);
    setText(self.element("face-name"), face);
    if (self.width != width || self.height != height) {
        self.width = width; self.height = height;
        self.context->SetDimensions({width, height});
        self.context->SetDensityIndependentPixelRatio(std::min(float(width) / 960, float(height) / 600));
    }
    self.runtime->renderer().render(*self.context, width, height, [&] { self.processInput(); });
}
void MenuView::key(int key, int action, int modifiers) { impl_->inputs.push_back({Impl::Input::Key, key, action, modifiers}); }
void MenuView::character(unsigned int codepoint) {
    Impl::Input input{Impl::Input::Character}; input.codepoint = codepoint; impl_->inputs.push_back(input);
}
void MenuView::pointer(float x, float y) {
    Impl::Input input{Impl::Input::Move}; input.x = x; input.y = y; impl_->inputs.push_back(input);
}
void MenuView::button(int button, bool pressed, int modifiers) { impl_->inputs.push_back({Impl::Input::Button, button, int(pressed), modifiers}); }
void MenuView::focusLost() { impl_->inputs.push_back({Impl::Input::Blur}); }
MenuCommand MenuView::takeCommand() { const auto command = impl_->command; impl_->command = MenuCommand::None; return command; }
glm::ivec4 MenuView::previewViewport() const {
    const auto& self = *impl_;
    auto& preview = self.element("character-preview");
    const auto offset = preview.GetAbsoluteOffset(Rml::BoxArea::Content), size = preview.GetBox().GetSize(Rml::BoxArea::Content);
    return {int(offset.x), self.height - int(offset.y + size.y), int(size.x), int(size.y)};
}
} // namespace multiplayer
