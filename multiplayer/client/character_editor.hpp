#pragma once
#include <memory>
namespace Rml { class ElementDocument; }
namespace multiplayer {
class CharacterCreation;
// Binds the character screen's RmlUi controls to the local preview/model.
class CharacterEditor {
public:
    CharacterEditor(Rml::ElementDocument& document, CharacterCreation& creation);
    ~CharacterEditor();
    void refresh();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace multiplayer
