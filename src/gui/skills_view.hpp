#pragma once

#include "skills/skills_config.hpp"

namespace Rml { class ElementDocument; }
namespace viewer {
void populateMeleeSkills(Rml::ElementDocument& document, const std::vector<MeleeSkill>& skills);
}
