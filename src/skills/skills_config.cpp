#include "skills_config.hpp"
#include <toml++/toml.hpp>
#include <algorithm>
#include <stdexcept>

namespace viewer {
std::vector<MeleeSkill> loadMeleeSkills(const std::filesystem::path& root) {
    const auto file = root / "skills.toml";
    try {
        const auto config = toml::parse_file(file.string());
        const auto* melee = config["melee"].as_table();
        if (!melee) throw std::runtime_error(file.string() + ": expected [melee] table");
        std::vector<MeleeSkill> result;
        for (const auto& [key, node] : *melee) {
            const std::string id(key.str());
            const auto name = node.value<std::string>();
            if (id.empty() || !std::all_of(id.begin(), id.end(), [](char c) {
                return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
            })) throw std::runtime_error(file.string() + ": invalid melee skill ID " + id);
            if (!name || name->empty())
                throw std::runtime_error(file.string() + ": " + id + " must have a nonempty display name");
            // Validate the registered definition even while its contents are placeholders.
            const auto definition = root / "skills/melee" / (id + ".skill.toml");
            (void)toml::parse_file(definition.string());
            result.push_back({id, *name});
        }
        return result;
    } catch (const toml::parse_error& error) {
        throw std::runtime_error(std::string(error.what()));
    }
}
} // namespace viewer
