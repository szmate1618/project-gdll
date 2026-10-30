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
    void requestBack();
    bool takeBackRequested();
    bool dialogOpen() const;
    void dialogKey(int key, bool shift);
    void save();
    void newCharacter();
    bool previewInteractive() const;
    void focusPendingInput(); // After event dispatch/layout, before the next queued input.
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace multiplayer
