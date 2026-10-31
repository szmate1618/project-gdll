#include "host_setup_view.hpp"
#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/Elements/ElementFormControlSelect.h>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace multiplayer {
namespace {
void text(Rml::Element& element, const std::string& value) {
    element.SetInnerRML(Rml::StringUtilities::EncodeRml(value));
}
std::string hourLabel(double hour) {
    const int minutes = int(hour * 60);
    std::ostringstream label;
    label << std::setfill('0') << std::setw(2) << minutes / 60 << ':' << std::setw(2) << minutes % 60;
    return label.str();
}
std::string settingValue(double value) {
    std::ostringstream result;
    result.imbue(std::locale::classic());
    result << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
    return result.str();
}
}
HostSetupView::HostSetupView(Rml::ElementDocument& document, const Options& defaults,
                           const std::vector<std::filesystem::path>& directories) : document_(document), setup_(defaults, directories) {
    std::string rows;
    for (std::size_t i = 0; i < setup_.scenarios().size(); ++i) {
        const auto& scenario = setup_.scenarios()[i];
        rows += "<button class=\"host-scenario\" data-scenario=\"" + std::to_string(i) + "\">" +
            Rml::StringUtilities::EncodeRml(scenario.name) + "<span>" + std::to_string(scenario.count) + " zombies" +
            (scenario.error.empty() ? "" : " / unavailable") + "</span></button>";
    }
    element("host-scenario-list").SetInnerRML(rows);
    auto& time = dynamic_cast<Rml::ElementFormControlSelect&>(element("host-start-time"));
    for (const auto hour : {6., 12., 18., 0.}) time.Add(hourLabel(hour), settingValue(hour));
    if (defaults.dayNight.startHour != 6 && defaults.dayNight.startHour != 12 && defaults.dayNight.startHour != 18 && defaults.dayNight.startHour != 0)
        time.Add(hourLabel(defaults.dayNight.startHour), settingValue(defaults.dayNight.startHour));
    auto& cycle = dynamic_cast<Rml::ElementFormControlSelect&>(element("host-day-cycle"));
    cycle.Add("20-minute day", settingValue(1200.)); cycle.Add("Fixed time", settingValue(0.));
    if (defaults.dayNight.dayLengthSeconds != 0 && defaults.dayNight.dayLengthSeconds != 1200)
        cycle.Add(std::to_string(int(defaults.dayNight.dayLengthSeconds)) + "-second day", settingValue(defaults.dayNight.dayLengthSeconds));
    text(element("host-map"), "Map: " + defaults.map.filename().string());
    syncOptions(); refresh();
    document_.AddEventListener("click", this); document_.AddEventListener("change", this);
}
HostSetupView::~HostSetupView() {
    document_.RemoveEventListener("click", this); document_.RemoveEventListener("change", this);
}
Rml::Element& HostSetupView::element(const char* id) const {
    auto* result = document_.GetElementById(id);
    if (!result) throw std::runtime_error(std::string("Missing host setup element: ") + id);
    return *result;
}
void HostSetupView::syncOptions() {
    dynamic_cast<Rml::ElementFormControlInput&>(element("setup-port")).SetValue(setup_.port);
    dynamic_cast<Rml::ElementFormControlSelect&>(element("host-start-time")).SetValue(settingValue(setup_.startHour));
    dynamic_cast<Rml::ElementFormControlSelect&>(element("host-day-cycle")).SetValue(settingValue(setup_.dayLengthSeconds));
}
void HostSetupView::refresh() {
    const auto& scenario = setup_.scenarios().at(setup_.selected);
    auto& list = element("host-scenario-list");
    for (int i = 0; i < list.GetNumChildren(); ++i) list.GetChild(i)->SetClass("selected", std::size_t(i) == setup_.selected);
    text(element("host-scenario-name"), scenario.name);
    text(element("host-scenario-detail"), scenario.description);
    text(element("host-population"), std::to_string(scenario.count) + " zombies / " +
        (scenario.traveling ? "traveling groups" : "no scripted travel"));
    text(element("host-requirements"), scenario.error.empty() ?
        (scenario.traveling ? "Uses prepared navigation for this map." : "No navigation data required.") : scenario.error);
    element("host-requirements").SetClass("unavailable", !scenario.error.empty());
    text(element("host-birds"), setup_.birds ? "BIRDS: ON" : "BIRDS: OFF");
    element("host-birds").SetClass("active", setup_.birds);
    text(element("host-summary"), scenario.name + " / Birds " + (setup_.birds ? "on" : "off") + " / " + hourLabel(setup_.startHour));
}
void HostSetupView::ProcessEvent(Rml::Event& event) {
    if (event.GetId() == Rml::EventId::Change) {
        const auto& id = event.GetTargetElement()->GetId();
        if (id == "setup-port") setup_.port = dynamic_cast<Rml::ElementFormControlInput&>(element("setup-port")).GetValue();
        else if (id == "host-start-time") setup_.startHour = std::stod(dynamic_cast<Rml::ElementFormControlSelect&>(element("host-start-time")).GetValue());
        else if (id == "host-day-cycle") setup_.dayLengthSeconds = std::stod(dynamic_cast<Rml::ElementFormControlSelect&>(element("host-day-cycle")).GetValue());
        else return;
        refresh(); return;
    }
    if (event.GetId() != Rml::EventId::Click) return;
    for (auto* target = event.GetTargetElement(); target && target != &document_; target = target->GetParentNode()) {
        if (target->HasAttribute("data-scenario")) {
            setup_.selected = std::stoul(target->GetAttribute<Rml::String>("data-scenario", "0"));
            refresh(); return;
        }
        if (target->GetId() == "host-birds") { setup_.birds = !setup_.birds; refresh(); return; }
        if (target->GetId() == "host-reset") { setup_.restoreOptions(); syncOptions(); refresh(); return; }
    }
}
ServerOptions HostSetupView::options() {
    setup_.port = dynamic_cast<Rml::ElementFormControlInput&>(element("setup-port")).GetValue();
    return setup_.options();
}
} // namespace multiplayer
