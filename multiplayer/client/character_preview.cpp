#include "character_preview.hpp"
#include "character_creation.hpp"
#include "gui/hud_render_interface.hpp"
#include <RmlUi/Core.h>
#include <algorithm>
#include <stdexcept>

namespace multiplayer {
namespace {
class CharacterPreview final : public Rml::Element {
public:
    explicit CharacterPreview(const Rml::String& tag) : Rml::Element(tag) {}
    CharacterCreation* creation = nullptr;
    viewer::HudRenderInterface* renderer = nullptr;
    glm::vec2 pointer{-1};
    bool left = false, right = false, interactive = false;
protected:
    void OnRender() override {
        if (!creation || !renderer || !creation->hasCharacter()) return;
        const auto offset = GetAbsoluteOffset(Rml::BoxArea::Content), size = GetBox().GetSize(Rml::BoxArea::Content);
        const auto height = GetContext()->GetDimensions().y;
        const glm::ivec4 viewport{int(offset.x), height - int(offset.y + size.y), int(size.x), int(size.y)};
        if (viewport.z <= 0 || viewport.w <= 0) return;
        const glm::vec2 local{(pointer.x - offset.x) / size.x, (pointer.y - offset.y) / size.y};
        renderer->renderExternal([&] {
            glEnable(GL_SCISSOR_TEST); glScissor(viewport.x, viewport.y, viewport.z, viewport.w);
            glDepthMask(GL_TRUE); glClearDepth(1); glClear(GL_DEPTH_BUFFER_BIT);
            creation->draw(viewport, interactive ? local : glm::vec2(-1), interactive && left, interactive && right);
        });
    }
};
CharacterPreview& preview(Rml::ElementDocument& document) {
    auto* element = dynamic_cast<CharacterPreview*>(document.GetElementById("character-preview"));
    if (!element) throw std::runtime_error("Missing character preview element");
    return *element;
}
}
void registerCharacterPreview() {
    static Rml::ElementInstancerGeneric<CharacterPreview> instancer;
    Rml::Factory::RegisterElementInstancer("character-preview", &instancer);
}
void bindCharacterPreview(Rml::ElementDocument& document, CharacterCreation& creation, viewer::HudRenderInterface& renderer) {
    auto& element = preview(document); element.creation = &creation; element.renderer = &renderer;
}
void updateCharacterPreview(Rml::ElementDocument& document, glm::vec2 pointer, bool left, bool right, bool interactive) {
    auto& element = preview(document); element.pointer = pointer; element.left = left; element.right = right; element.interactive = interactive;
}
} // namespace multiplayer
