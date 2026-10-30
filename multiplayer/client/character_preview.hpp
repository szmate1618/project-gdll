#pragma once
#include <glm/vec2.hpp>
namespace Rml { class ElementDocument; }
namespace viewer { class HudRenderInterface; }
namespace multiplayer {
class CharacterCreation;
void registerCharacterPreview();
void bindCharacterPreview(Rml::ElementDocument& document, CharacterCreation& creation, viewer::HudRenderInterface& renderer);
void updateCharacterPreview(Rml::ElementDocument& document, glm::vec2 framebufferPointer, bool left, bool right, bool interactive);
} // namespace multiplayer
