#pragma once
#include "host_setup.hpp"
#include <RmlUi/Core/EventListener.h>

namespace Rml { class ElementDocument; class Element; class Event; }
namespace multiplayer {
// Local RmlUi controls for one persistent hosting draft; no session lifecycle.
class HostSetupView final : public Rml::EventListener {
public:
    HostSetupView(Rml::ElementDocument& document, const Options& defaults,
                  const std::vector<std::filesystem::path>& scenarioDirectories);
    ~HostSetupView() override;
    void ProcessEvent(Rml::Event& event) override;
    ServerOptions options();
private:
    Rml::Element& element(const char* id) const;
    void refresh();
    void syncOptions();
    Rml::ElementDocument& document_;
    HostSetup setup_;
};
} // namespace multiplayer
