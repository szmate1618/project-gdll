#include "skills_view.hpp"
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/StringUtilities.h>
#include <stdexcept>

namespace viewer {
void populateMeleeSkills(Rml::ElementDocument& document, const std::vector<MeleeSkill>& skills) {
    auto* host = document.GetElementById("melee-skill-entries");
    if (!host) throw std::runtime_error("Missing melee skill list");
    std::string contents;
    for (const auto& skill : skills)
        contents += "<div class='skill-entry' data-skill='" + skill.id + "'>"
            "<div class='skill-square'></div><div class='skill-name'>" +
            Rml::StringUtilities::EncodeRml(skill.name) + "</div></div>";
    if (skills.empty()) contents = "<div class='skills-empty'>No melee skills available</div>";
    host->SetInnerRML(contents);
}
} // namespace viewer
