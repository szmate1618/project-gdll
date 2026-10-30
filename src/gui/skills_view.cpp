#include "skills_view.hpp"
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/StringUtilities.h>
#include <stdexcept>

namespace viewer {
void populateMeleeSkills(Rml::ElementDocument& document, const std::vector<MeleeSkill>& skills) {
    auto* host = document.GetElementById("melee-skill-entries");
    auto* count = document.GetElementById("skills-count");
    if (!host || !count) throw std::runtime_error("Missing melee skill list");
    count->SetInnerRML(std::to_string(skills.size()) + (skills.size() == 1 ? " ACTION" : " ACTIONS"));
    std::string contents;
    for (const auto& skill : skills)
        contents += "<div class='skill-entry' data-skill='" + skill.id + "'>"
            "<div class='skill-square'>" + std::string(skill.id == "punch" ? "<div class='skill-symbol fists'></div>" : "") + "</div><div class='skill-name'>" +
            Rml::StringUtilities::EncodeRml(skill.name) + "</div></div>";
    if (skills.empty()) contents = "<div class='skills-empty'>No melee skills available</div>";
    host->SetInnerRML(contents);
}
} // namespace viewer
