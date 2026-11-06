#include "health_view.hpp"
#include "hud.hpp"
#include "inventory/player_inventory.hpp"

#include <RmlUi/Core.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace viewer {
namespace {
struct BodyRectangle {
    InjuryBodyPart part;
    const char* name;
    const char* label;
    int x, y, width, height;
    bool noteOnLeft;
};
// Front view in design dp. The player's right is on the viewer's left.
constexpr std::array<BodyRectangle, 29> body{{
    {InjuryBodyPart::Head, "Head", "HEAD", 258, 0, 74, 40, true},
    {InjuryBodyPart::Neck, "Neck", "NECK", 272, 46, 46, 18, false},
    {InjuryBodyPart::RightShoulder, "Right shoulder", "SHLDR", 207, 70, 35, 23, true},
    {InjuryBodyPart::LeftShoulder, "Left shoulder", "SHLDR", 348, 70, 35, 23, false},
    {InjuryBodyPart::UpperTorso, "Upper torso", "UPPER TORSO", 248, 70, 94, 65, false},
    {InjuryBodyPart::RightUpperArm, "Right upper arm", "UPPER\nARM", 207, 99, 35, 49, true},
    {InjuryBodyPart::LeftUpperArm, "Left upper arm", "UPPER\nARM", 348, 99, 35, 49, false},
    {InjuryBodyPart::LowerTorso, "Lower torso", "LOWER TORSO", 256, 141, 78, 53, true},
    {InjuryBodyPart::RightElbow, "Right elbow", "ELBOW", 207, 154, 35, 18, true},
    {InjuryBodyPart::LeftElbow, "Left elbow", "ELBOW", 348, 154, 35, 18, false},
    {InjuryBodyPart::RightForearm, "Right forearm", "FORE\nARM", 207, 178, 35, 49, true},
    {InjuryBodyPart::LeftForearm, "Left forearm", "FORE\nARM", 348, 178, 35, 49, false},
    {InjuryBodyPart::Pelvis, "Pelvis", "PELVIS", 248, 200, 94, 30, false},
    {InjuryBodyPart::RightWrist, "Right wrist", "WRIST", 207, 233, 35, 16, true},
    {InjuryBodyPart::LeftWrist, "Left wrist", "WRIST", 348, 233, 35, 16, false},
    {InjuryBodyPart::RightHip, "Right hip", "HIP", 248, 236, 40, 18, true},
    {InjuryBodyPart::LeftHip, "Left hip", "HIP", 302, 236, 40, 18, false},
    {InjuryBodyPart::RightHand, "Right hand", "HAND", 207, 255, 35, 28, true},
    {InjuryBodyPart::LeftHand, "Left hand", "HAND", 348, 255, 35, 28, false},
    {InjuryBodyPart::RightUpperLeg, "Right upper leg", "UPPER\nLEG", 248, 260, 40, 59, true},
    {InjuryBodyPart::LeftUpperLeg, "Left upper leg", "UPPER\nLEG", 302, 260, 40, 59, false},
    {InjuryBodyPart::RightKnee, "Right knee", "KNEE", 248, 325, 40, 18, true},
    {InjuryBodyPart::LeftKnee, "Left knee", "KNEE", 302, 325, 40, 18, false},
    {InjuryBodyPart::RightLowerLeg, "Right lower leg", "LOWER\nLEG", 248, 349, 40, 62, true},
    {InjuryBodyPart::LeftLowerLeg, "Left lower leg", "LOWER\nLEG", 302, 349, 40, 62, false},
    {InjuryBodyPart::RightAnkle, "Right ankle", "ANKLE", 248, 417, 40, 16, true},
    {InjuryBodyPart::LeftAnkle, "Left ankle", "ANKLE", 302, 417, 40, 16, false},
    {InjuryBodyPart::RightFoot, "Right foot", "FOOT", 240, 439, 48, 23, true},
    {InjuryBodyPart::LeftFoot, "Left foot", "FOOT", 302, 439, 48, 23, false}
}};
static_assert(body.size() == static_cast<std::size_t>(InjuryBodyPart::Count));
constexpr std::array<const char*, static_cast<std::size_t>(InjuryType::Count)> injuryNames{
    "Cut", "Abrasion", "Puncture", "Bruise", "Blunt trauma", "Sprain", "Strain",
    "Dislocation", "Fracture", "Crush", "Burn"
};

std::string encode(const std::string& text) { return Rml::StringUtilities::EncodeRml(text); }
std::string percent(float value) {
    std::ostringstream result;
    result.imbue(std::locale::classic());
    result << std::fixed << std::setprecision(0) << std::round(static_cast<double>(value) * 100) << '%';
    return result.str();
}
std::string rect(int x, int y, int width, int height) {
    return "left:" + std::to_string(x) + "dp;top:" + std::to_string(y) + "dp;width:" +
        std::to_string(width) + "dp;height:" + std::to_string(height) + "dp;";
}
std::string injuryFlags(const PlayerInjury& injury) {
    std::string flags;
    for (const auto& condition : {std::pair{injury.modifiers().bleeding, "Bleeding"},
                                  std::pair{injury.modifiers().swelling, "Swelling"},
                                  std::pair{injury.modifiers().infected, "Infected"}}) {
        if (!condition.first) continue;
        if (!flags.empty()) flags += " / ";
        flags += condition.second;
    }
    return flags;
}
// A display scale only: keep even painless records visible. Conditions provide
// a minimum tint; this does not assign damage or medical effects to injury types.
float severity(const PlayerInjury& injury) {
    return std::max({.15f, injury.pain(), injury.weakness(),
        injury.modifiers().bleeding || injury.modifiers().swelling ? .5f : 0.f,
        injury.modifiers().infected ? .75f : 0.f});
}
std::string injuryColor(float score) {
    constexpr std::array<int, 3> green{89, 196, 156}, yellow{231, 205, 101}, red{236, 105, 98};
    const auto& start = score < .5f ? green : yellow;
    const auto& end = score < .5f ? yellow : red;
    const float amount = score < .5f ? score * 2 : (score - .5f) * 2;
    std::string result = "rgb(";
    for (std::size_t i = 0; i < 3; ++i) {
        if (i) result += ",";
        result += std::to_string(static_cast<int>(std::round(start[i] + (end[i] - start[i]) * amount)));
    }
    return result + ")";
}
std::string stat(const char* label, std::optional<float> value, const char* color) {
    const bool known = value && std::isfinite(*value);
    return "<div class='health-stat'><div class='health-stat-label'>" + std::string(label) +
        "</div><div class='health-stat-value' style='color:" + color + "'>" +
        (known ? percent(*value) : "--") + "</div><div class='health-stat-track'>"
        "<div class='health-stat-fill' style='background-color:" + color + ";width:" +
        (known ? std::to_string(std::clamp(*value, 0.f, 1.f) * 100) : "0") + "%'></div></div></div>";
}
std::string bodyMarkup(const std::optional<PlayerHealth>& medical) {
    std::string rectangles, notes, links;
    std::array<int, 2> nextNoteY{};
    for (const auto& part : body) {
        std::vector<const PlayerInjury*> injuries;
        float score = 0;
        if (medical) for (const auto& injury : medical->injuries()) {
            if (injury.bodyPart() != part.part) continue;
            injuries.push_back(&injury);
            score = std::max(score, severity(injury));
        }
        const auto color = medical ? injuryColor(score) : "#f5f6f24d";
        std::string label = part.label;
        const auto newline = label.find('\n');
        if (newline != std::string::npos) label.replace(newline, 1, "<br />");
        rectangles += "<div class='health-part' id='health-part-" + std::to_string(static_cast<int>(part.part)) +
            "' title='" + part.name + "' style='" + rect(part.x, part.y, part.width, part.height) +
            "border-color:" + color + "'><div class='health-part-label'>" + label + "</div></div>";
        if (injuries.empty()) continue;
        std::string entries;
        int noteHeight = 28;
        for (const auto* injury : injuries) {
            const auto flags = injuryFlags(*injury);
            entries += "<div class='health-injury-entry'><div class='health-injury-type'>" +
                std::string(injuryNames.at(static_cast<std::size_t>(injury->type()))) +
                "</div><div class='health-injury-scores'>Pain " + percent(injury->pain()) +
                " / Weakness " + percent(injury->weakness()) + "</div>" +
                (flags.empty() ? "" : "<div class='health-injury-flags'>" + flags + "</div>") + "</div>";
            noteHeight += 38 + (flags.empty() ? 0 : 14);
        }
        auto& next = nextNoteY[part.noteOnLeft ? 0 : 1];
        const int top = std::max(part.y, next);
        const int edge = part.noteOnLeft ? 192 : 394;
        const int anchor = part.y + part.height / 2;
        // Route a short elbow from each note back to its anatomical location.
        const auto link = [&](int x, int y, int width, int height) {
            links += "<div class='health-injury-link' style='" + rect(x, y, width, height) + "'></div>";
        };
        link(part.noteOnLeft ? edge : part.x + part.width, anchor,
            part.noteOnLeft ? part.x - edge : edge - part.x - part.width, 1);
        link(edge, std::min(anchor, top + 12), 1, std::abs(anchor - top - 12) + 1);
        link(part.noteOnLeft ? 184 : edge, top + 12, part.noteOnLeft ? edge - 184 : 400 - edge, 1);
        notes += "<div class='health-injury-note' style='" + rect(part.noteOnLeft ? 4 : 400, top, 180, noteHeight) +
            "border-color:" + color + "'><div class='health-injury-part'>" + part.name + "</div>" + entries + "</div>";
        next = top + noteHeight + 8;
    }
    return "<div style='height:" + std::to_string(std::max({464, nextNoteY[0], nextNoteY[1]})) +
        "dp'>" + links + rectangles + notes + "</div>";
}
std::string medicalItems(const PlayerInventory& inventory, std::size_t& count) {
    std::string result;
    count = 0;
    for (const auto& match : inventory.findByTag("medical")) {
        const auto& item = *match.item;
        const auto quantity = item.inventory().stackable ? inventory.quantity(item.id()) : 1;
        if (quantity <= 0) continue;
        ++count;
        const auto& location = match.location;
        std::string storage = "In hand";
        if (location.placement) {
            const auto& reference = location.placement->storage;
            storage = "Body storage";
            for (const auto& equipment : inventory.storage().equipment)
                for (const auto& slot : equipment.slots)
                    if (bodyStorageReference(equipment.id, slot.id) == reference)
                        storage = equipment.name + " / " + slot.name;
            if (const auto& backpack = inventory.storage().backpack) {
                if (reference == backpackStorageReference(backpack->id)) storage = backpack->name + " / Main";
                for (const auto& slot : backpack->slots)
                    if (backpackStorageReference(backpack->id, slot.id) == reference)
                        storage = backpack->name + " / " + slot.name;
            }
        }
        result += "<div class='health-medical-item'><div class='health-medical-square'></div>"
            "<div class='health-medical-name'>" + encode(item.inventory().name) + " × " + std::to_string(quantity) +
            "</div><div class='health-medical-location'>" + encode(storage) + "</div></div>";
    }
    return count ? result : "<div class='health-empty'>No medical supplies carried.</div>";
}
} // namespace

void HealthView::markup(const char* id, const std::string& value) {
    auto [entry, inserted] = contents_.emplace(id, value);
    if (!inserted && entry->second == value) return;
    auto* element = document_.GetElementById(id);
    if (!element) throw std::runtime_error(std::string("Missing health element: ") + id);
    element->SetInnerRML(value);
    entry->second = value;
}

void HealthView::update(const HudState& state, const PlayerInventory& inventory) {
    markup("health-stats", stat("HEALTH", state.health ? std::optional<float>(*state.health / PlayerHealth::maximumHealth) : std::nullopt, "#59c49c") +
        stat("STAMINA", state.stamina, "#65a9e7") + stat("STRAIN", state.strain, "#ec6962") +
        stat("WEARINESS", state.weariness, "#e7cd65") +
        stat("PAIN TOLERANCE", state.medical ? std::optional<float>(state.medical->painTolerance()) : std::nullopt, "#f5f6f2"));
    markup("health-body", bodyMarkup(state.medical));
    const auto count = state.medical ? state.medical->injuries().size() : 0;
    markup("health-injury-count", state.medical ? std::to_string(count) + (count == 1 ? " INJURY" : " INJURIES") : "--");
    markup("health-record-status", !state.medical ? "Medical records unavailable" :
        count == 0 ? "No recorded injuries" : "Pain and weakness shown before pain tolerance");
    std::size_t itemCount = 0;
    markup("health-medical-items", medicalItems(inventory, itemCount));
    markup("health-medical-count", std::to_string(itemCount) + (itemCount == 1 ? " ITEM" : " ITEMS"));
}
} // namespace viewer
