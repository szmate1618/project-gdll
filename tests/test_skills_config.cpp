#include "skills/skills_config.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
struct Fixture {
    std::filesystem::path root = std::filesystem::current_path() /
        ("skills-fixture-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Fixture() { std::filesystem::create_directories(root / "skills/melee"); }
    ~Fixture() { std::filesystem::remove_all(root); }
    void config(const char* contents) const { std::ofstream(root / "skills.toml") << contents; }
    void rejects() const {
        try { (void)viewer::loadMeleeSkills(root); }
        catch (const std::runtime_error&) { return; }
        throw std::runtime_error("Invalid skill registration was accepted");
    }
};
}
int main() {
    try {
        const auto defaults = viewer::loadMeleeSkills(VIEWER_SKILL_ROOT);
        require(defaults.size() == 1 && defaults[0].id == "punch" && defaults[0].name == "Punch",
                "Starter melee catalog must contain Punch");
        Fixture fixture;
        std::ofstream(fixture.root / "skills/melee/punch.skill.toml");
        std::ofstream(fixture.root / "skills/melee/kick.skill.toml");
        fixture.config("[melee]\npunch='Punch'\n");
        require(viewer::loadMeleeSkills(fixture.root).size() == 1,
                "Unregistered skill files must not appear in the catalog");
        fixture.config("[melee]\npunch='Punch'\nkick='Kick'\n");
        const auto added = viewer::loadMeleeSkills(fixture.root);
        require(added.size() == 2 && added[0].id == "kick" && added[0].name == "Kick" && added[1].id == "punch",
                "Registered empty definitions must load with configured labels");
        fixture.config("[melee]\n");
        require(viewer::loadMeleeSkills(fixture.root).empty(), "An empty catalog must remain empty");
        fixture.config("[melee]\nmissing='Missing'\n"); fixture.rejects();
        fixture.config("[melee]\npunch=8\n"); fixture.rejects();
        fixture.config("[melee]\npunch=''\n"); fixture.rejects();
        fixture.config("[melee]\n'../punch'='Punch'\n"); fixture.rejects();
        fixture.config("melee=[]\n"); fixture.rejects();
        fixture.config("[melee]\npunch='Punch'\n");
        std::ofstream(fixture.root / "skills/melee/punch.skill.toml") << "invalid=[";
        fixture.rejects();
        std::cout << "Skill catalog checks passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
